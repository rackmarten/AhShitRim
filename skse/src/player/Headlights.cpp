#include "player/Headlights.h"

#include "vehicle_hits.h"
#include "vehicle_lights.h"

namespace AhShitRim::Headlights
{
    namespace
    {
        constexpr int kEnds = 2;  // the headlights' beam, the tail lights' glow

        struct State
        {
            RE::NiPointer<RE::NiPointLight> lights[Protocol::kVehicleMax][kEnds];
            std::uint8_t                    lit[Protocol::kVehicleMax] = {};
            bool                            failed = false;
            int                             logged = 0;
        } state;

        RE::NiNode* Scene()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* root = player ? player->Get3D() : nullptr;
            return root ? root->parent : nullptr;
        }

        // A point light for one end of a vehicle, made the first time it burns (as the muzzle flash's).
        RE::NiPointLight* Ensure(std::uint32_t a_slot, int a_end)
        {
            auto& light = state.lights[a_slot][a_end];
            if (light || state.failed) {
                return light.get();
            }
            auto* scene = Scene();
            auto* shadowScene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
            if (!scene || !shadowScene) {
                return nullptr;  // not up yet: tried again next frame
            }
            auto* made = RE::NiPointLight::Create();
            if (!made) {
                state.failed = true;
                SKSE::log::warn("headlights: no point light could be made, the vehicles' lights won't light the scene");
                return nullptr;
            }
            made->name = a_end == 0 ? "AhShitRimHeadlight" : "AhShitRimTailLight";
            auto& data = made->GetLightRuntimeData();
            data.ambient = RE::NiColor(0.0f, 0.0f, 0.0f);
            data.fade = 0.0f;
            light.reset(made);
            scene->AttachChild(made, true);
            RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
            params.dynamic = true;
            params.shadowLight = false;
            params.portalStrict = false;
            params.affectLand = true;
            params.affectWater = true;
            params.neverFades = true;
            params.falloff = 1.0f;
            shadowScene->AddLight(made, params);
            return made;
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
            data.fade = 1.0f;
            light->SetLightAttenuation(a_glow.radius);
            light->local.translate = a_at;
            RE::NiUpdateData update{};
            light->Update(update);
        }
    }

    void Sky(Protocol::InputState& a_input)
    {
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
        if (a_pose && lit != state.lit[a_slot] && state.logged < 24) {
            state.logged++;
            SKSE::log::info("headlights: the vehicle in slot {} has lamps {:X} lit (front at {}, {}, {})", a_slot, lit, a_pose->lights.front[0],
                a_pose->lights.front[1], a_pose->lights.front[2]);
        }
        state.lit[a_slot] = lit;
        if (lit == 0) {
            Out(a_slot);
            return;
        }
        const auto frame = VehicleHits::FrameOf(a_pose->vehicleFrame, { a_feet.x, a_feet.y, a_feet.z });
        for (int end = 0; end < kEnds; end++) {
            VehicleLights::Glow glow;
            if (!VehicleLights::EndOf(a_pose->lights, end == 0, glow)) {
                if (state.lights[a_slot][end]) {
                    state.lights[a_slot][end]->GetLightRuntimeData().fade = 0.0f;
                }
                continue;
            }
            const auto at = VehicleHits::ToWorld(frame, glow.at);
            Place(a_slot, end, RE::NiPoint3(at.x, at.y, at.z), glow);
        }
    }

    void Clear()
    {
        for (std::uint32_t i = 0; i < Protocol::kVehicleMax; i++) {
            Out(i);
            state.lit[i] = 0;
        }
    }
}
