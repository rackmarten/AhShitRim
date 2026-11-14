#include "player/Headlights.h"

#include "Ini.h"
#include "body/ModelView.h"
#include "body/SceneLights.h"
#include "corona_model.h"
#include "skin_model.h"
#include "vehicle_hits.h"
#include "vehicle_lights.h"

#include <charconv>
#include <map>

namespace AhShitRim::Headlights
{
    namespace
    {
        constexpr int kEnds = 2;  // a point light for the headlights, one for the tail lights (VehicleLights::LightOf)
        constexpr int kLogsPerModel = 8;  // the lamps of each model logged this many times as they change
        constexpr std::uint32_t kCoronaModel = 0xC0A00000;  // + 2 per slot: even, so never an id San Andreas sends
        constexpr float         kCoronaFront = 40.0f;       // Skyrim units from a headlight corona's middle to its edge (57 cm)
        constexpr float         kCoronaRear = 28.0f;        // a tail light's

        struct State
        {
            RE::NiPointer<RE::NiPointLight> lights[Protocol::kVehicleMax][kEnds];
            std::uint8_t                    lit[Protocol::kVehicleMax] = {};
            std::uint8_t                    lampState[Protocol::kVehicleMax] = {};
            std::uint32_t                   model[Protocol::kVehicleMax] = {};
            std::map<std::uint32_t, int>    logged;  // by model
            bool                            failed = false;
            bool                            coronasFailed = false;
            std::uint32_t                   coronaFrame = 0;
            bool                            tuned = false;
            VehicleLights::Tuning           tuning;
        } state;

        // [Headlights]' keys: each value's field and its scale from the ini's units (metres for the distances).
        struct Key
        {
            const char*                  name;
            float VehicleLights::Tuning::* field;
            float                        scale;
        };
        constexpr Key kKeys[] = {
            { "Ahead", &VehicleLights::Tuning::beamAhead, Pose::kUnitsPerMeter },
            { "Raise", &VehicleLights::Tuning::beamRaise, Pose::kUnitsPerMeter },
            { "Radius", &VehicleLights::Tuning::beamRadius, Pose::kUnitsPerMeter },
            { "Intensity", &VehicleLights::Tuning::beamIntensity, 1.0f },
            { "TailRadius", &VehicleLights::Tuning::tailRadius, Pose::kUnitsPerMeter },
            { "TailIntensity", &VehicleLights::Tuning::tailIntensity, 1.0f },
            { "BrakeRadius", &VehicleLights::Tuning::brakeRadius, Pose::kUnitsPerMeter },
            { "BrakeIntensity", &VehicleLights::Tuning::brakeIntensity, 1.0f },
            { "TailDrop", &VehicleLights::Tuning::tailDrop, Pose::kUnitsPerMeter },
            { "TailBack", &VehicleLights::Tuning::tailBack, Pose::kUnitsPerMeter },
            { "LampGlow", &VehicleLights::Tuning::lampGlow, 1.0f },
            { "BrakeGlow", &VehicleLights::Tuning::brakeGlow, 1.0f },
        };

        // [Headlights] in AhShitRim.ini, each optional: Ahead, Raise, Radius (metres) and Intensity for the headlights'
        // light on the road; TailDrop, TailBack, TailRadius, TailIntensity, BrakeRadius, BrakeIntensity for the tail lights';
        // LampGlow and BrakeGlow for the lamps' meshes. Read once.
        const VehicleLights::Tuning& Tuning()
        {
            if (state.tuned) {
                return state.tuning;
            }
            state.tuned = true;
            auto& t = state.tuning;
            for (const auto& key : kKeys) {
                const auto text = Ini::Value("Headlights", key.name);
                float      value = 0.0f;
                if (!text.empty() && std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{} && std::isfinite(value) && value >= 0.0f) {
                    t.*key.field = value * key.scale;
                }
            }
            ModelView::SetLampTuning(t);
            SKSE::log::info("headlights: beam at least {:.1f} m ahead, {:.1f} m up, {:.1f} m round at {:.2f}; tail lights {:.1f} m down, {:.1f} m round at {:.2f}, "
                            "braking {:.1f} m at {:.2f}; lamps glow {:.2f}, {:.1f} times braking",
                t.beamAhead / Pose::kUnitsPerMeter, t.beamRaise / Pose::kUnitsPerMeter, t.beamRadius / Pose::kUnitsPerMeter, t.beamIntensity,
                t.tailDrop / Pose::kUnitsPerMeter, t.tailRadius / Pose::kUnitsPerMeter, t.tailIntensity, t.brakeRadius / Pose::kUnitsPerMeter,
                t.brakeIntensity, t.lampGlow, t.brakeGlow);
            return t;
        }

        RE::NiNode* Scene()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* root = player ? player->Get3D() : nullptr;
            return root ? root->parent : nullptr;
        }

        // The point light for one end of a vehicle, made the first time it burns (as the muzzle flash's), and kept among
        // the renderer's lights (a load door drops them).
        RE::NiPointLight* Ensure(std::uint32_t a_slot, int a_end)
        {
            auto& light = state.lights[a_slot][a_end];
            if (light || state.failed) {
                return light && SceneLights::Keep(light.get()) ? light.get() : nullptr;
            }
            auto* scene = Scene();
            if (!scene) {
                return nullptr;  // not up yet: tried again next frame
            }
            auto* made = RE::NiPointLight::Create();
            if (!made) {
                state.failed = true;
                SKSE::log::warn("headlights: no point light could be made, the vehicles' lights won't light the scene");
                return nullptr;
            }
            made->name = a_end == 0 ? "AhShitRimHeadlight" : "AhShitRimTailLight";
            // Its colour and reach before it joins the renderer, which weighs a light by them as it's added (a light added
            // black had luminance 0 there).
            const auto& tuning = Tuning();
            const float radius = a_end == 0 ? tuning.beamRadius : tuning.brakeRadius;
            auto&       data = made->GetLightRuntimeData();
            data.ambient = RE::NiColor(0.0f, 0.0f, 0.0f);
            data.diffuse = a_end == 0 ? RE::NiColor(1.0f, 0.92f, 0.7f) : RE::NiColor(1.0f, 0.06f, 0.03f);
            data.radius = RE::NiPoint3(radius, radius, radius);
            made->SetLightAttenuation(radius);
            data.fade = 0.0f;
            light.reset(made);
            scene->AttachChild(made, true);
            return SceneLights::Keep(made) ? made : nullptr;
        }

        // A vehicle slot's coronas (CoronaModel), built the first time its lamps burn.
        bool EnsureCoronas(std::uint32_t a_id)
        {
            if (ModelView::Has(a_id)) {
                return true;
            }
            if (state.coronasFailed) {
                return false;
            }
            if (!ModelView::AddModel(a_id, SkinModel::Serialize(CoronaModel::Build(kCoronaFront, kCoronaRear)))) {
                state.coronasFailed = true;
                SKSE::log::warn("headlights: the coronas' model did not build, the lamps show no corona");
                return false;
            }
            return true;
        }

        // The coronas over a vehicle's burning lamps, facing the camera, glowing as its lamps do.
        void ShowCoronas(std::uint32_t a_slot, const Protocol::BodyState& a_pose, const VehicleHits::Frame& a_frame, const RE::NiPoint3& a_feet,
            std::uint8_t a_lit)
        {
            const auto* camera = RE::Main::WorldRootCamera();
            const auto  id = kCoronaModel + a_slot * 2;
            if (!camera || !EnsureCoronas(id)) {
                return;
            }
            Protocol::BodyState coronas{};
            coronas.frame = ++state.coronaFrame;
            coronas.flags = Protocol::kBodyValid;
            coronas.model = id;
            coronas.lights = a_pose.lights;
            coronas.lights.lit = a_lit;
            coronas.cameraForward[1] = 1.0f;
            const auto& eye = camera->world.translate;
            if (CoronaModel::Pose(coronas.lights, a_frame, { eye.x, eye.y, eye.z }, { a_feet.x, a_feet.y, a_feet.z }, coronas) > 0) {
                ModelView::Show(coronas, a_feet);  // not shown, it's hidden at the frame's end
            }
        }

        void Out(std::uint32_t a_slot)
        {
            for (auto& light : state.lights[a_slot]) {
                if (light) {
                    light->GetLightRuntimeData().fade = 0.0f;
                }
            }
        }

        void Place(std::uint32_t a_slot, int a_end, const RE::NiPoint3& a_at, const VehicleLights::Glow& a_glow)
        {
            auto* light = Ensure(a_slot, a_end);
            if (!light) {
                return;
            }
            if (auto* scene = Scene(); scene && light->parent != scene) {  // the player's 3D was rebuilt (a load door)
                if (light->parent) {
                    light->parent->DetachChild(light);
                }
                scene->AttachChild(light, true);
            }
            auto& data = light->GetLightRuntimeData();
            data.diffuse = RE::NiColor(a_glow.colour[0], a_glow.colour[1], a_glow.colour[2]);
            data.radius = RE::NiPoint3(a_glow.radius, a_glow.radius, a_glow.radius);
            data.fade = a_glow.intensity;
            light->SetLightAttenuation(a_glow.radius);
            light->local.translate = a_at;
            RE::NiUpdateData update{};
            light->Update(update);
        }
    }

    void Sky(Protocol::InputState& a_input)
    {
        Tuning();  // read once, before any lamp burns
        const auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) {
            return;  // no sky sent: San Andreas keeps its own clock
        }
        bool wet = false;
        if (const auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) {
            wet = sky->currentWeather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy, RE::TESWeather::WeatherDataFlag::kSnow);
        }
        const auto* player = RE::PlayerCharacter::GetSingleton();
        const auto* cell = player ? player->GetParentCell() : nullptr;
        a_input.sky = VehicleLights::SkyOf(wet, cell && cell->IsInteriorCell());
        a_input.minuteOfDay = VehicleLights::MinuteOfDay(calendar->GetHour());
    }

    void Show(std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet)
    {
        if (a_slot >= Protocol::kVehicleMax) {
            return;
        }
        const std::uint8_t lit = a_pose && (a_pose->flags & Protocol::kBodyWrecked) == 0 ? a_pose->lights.lit : 0;
        const std::uint8_t lampState = a_pose ? a_pose->lights.state : 0;
        // Each model's lamps logged as they change (a few times each): which burn, and where each end's light goes.
        bool log = false;
        if (a_pose && (lit != state.lit[a_slot] || lampState != state.lampState[a_slot] || a_pose->model != state.model[a_slot])) {
            auto& count = state.logged[a_pose->model];
            log = count < kLogsPerModel;
            count += log ? 1 : 0;
            if (log) {
                SKSE::log::info("headlights: {} in slot {} has lamps {:X} lit{} (headlight dummy at {}, {}, {}, tail light dummy at {}, {}, {})",
                    ModelView::NameOf(a_pose->model), a_slot, lit, (lampState & Protocol::kLightBraking) != 0 ? ", braking" : "",
                    a_pose->lights.front[0], a_pose->lights.front[1], a_pose->lights.front[2], a_pose->lights.rear[0], a_pose->lights.rear[1],
                    a_pose->lights.rear[2]);
            }
            state.model[a_slot] = a_pose->model;
        }
        state.lit[a_slot] = lit;
        state.lampState[a_slot] = lampState;
        if (lit == 0) {
            Out(a_slot);
            return;
        }
        const auto& tuning = Tuning();
        const auto  frame = VehicleHits::FrameOf(a_pose->vehicleFrame, { a_feet.x, a_feet.y, a_feet.z });
        ShowCoronas(a_slot, *a_pose, frame, a_feet, lit);
        for (int end = 0; end < kEnds; end++) {
            VehicleLights::Glow glow;
            if (!VehicleLights::LightOf(a_pose->lights, end, tuning, glow)) {
                if (state.lights[a_slot][end]) {
                    state.lights[a_slot][end]->GetLightRuntimeData().fade = 0.0f;
                }
                continue;
            }
            const auto at = VehicleHits::ToWorld(frame, glow.at);
            Place(a_slot, end, RE::NiPoint3(at.x, at.y, at.z), glow);
            if (log) {
                SKSE::log::info("headlights: {}'s {} light placed at {:.0f}, {:.0f}, {:.0f} in the vehicle (world {:.0f}, {:.0f}, {:.0f}), {:.1f} m round at {:.2f}{}",
                    ModelView::NameOf(a_pose->model), end == 0 ? "headlights'" : "tail lights'", glow.at.x, glow.at.y, glow.at.z, at.x, at.y, at.z,
                    glow.radius / Pose::kUnitsPerMeter, glow.intensity, state.lights[a_slot][end] ? "" : " (no light made)");
            }
        }
    }

    bool DebugTune(std::string_view a_key, float a_value)
    {
        Tuning();
        for (const auto& key : kKeys) {
            if (a_key == key.name && std::isfinite(a_value) && a_value >= 0.0f) {
                state.tuning.*key.field = a_value * key.scale;
                ModelView::SetLampTuning(state.tuning);
                return true;
            }
        }
        return false;
    }

    std::vector<std::pair<std::string, float>> DebugTuning()
    {
        Tuning();
        std::vector<std::pair<std::string, float>> out;
        for (const auto& key : kKeys) {
            out.emplace_back(key.name, state.tuning.*key.field / key.scale);
        }
        return out;
    }

    void Clear()
    {
        for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
            Out(i);
            state.lit[i] = 0;
        }
    }
}
