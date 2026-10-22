#include "player/VehicleFx.h"

#include "body/ModelView.h"
#include "player/Projectiles.h"
#include "smoke_model.h"
#include "vehicle_damage.h"

#include <algorithm>
#include <format>

namespace AhShitRim::VehicleFx
{
    using namespace VehicleDamage;

    namespace
    {
        // Vanilla effects (Skyrim - Meshes), spawned as Skyrim's temporary particles.
        constexpr const char*   kWhiteSmokeModel = "Effects\\Ambient\\FXSmokeChimney01.nif";  // a chimney's white smoke
        constexpr const char*   kFireModel = "Effects\\FXFire02New.nif";                     // a small fire, as on CJ while he burns
        constexpr float         kBlastLift = 40.0f;  // Skyrim units: the fireball goes off this far over the vehicle's middle
        constexpr float         kWreckFireLift = 30.0f;
        constexpr std::uint32_t kSmokeModelId = 0xB5A00000;  // + 2 per slot: even, so never an id San Andreas sends

        struct Slot
        {
            Watch             blowUp;
            Stage             stage = Stage::Fine;
            float             smoke = 0.0f;  // seconds since the last puff of white smoke
            float             fire = 0.0f;   // and of fire
            SmokeModel::Trail black;
            std::uint32_t     frame = 0;
        };

        struct State
        {
            Slot slots[Protocol::kVehicleMax];
            bool smokeFailed = false;
        } state;

        const char* ModelOf(Effect a_effect) { return a_effect == Effect::Fire ? kFireModel : kWhiteSmokeModel; }

        // a_puff at a_at once a_since has reached its interval.
        void Puff(RE::PlayerCharacter* a_player, const VehicleDamage::Puff& a_puff, float& a_since, const RE::NiPoint3& a_at)
        {
            if (a_since < a_puff.every) {
                return;
            }
            a_since = 0.0f;
            if (auto* cell = a_player->GetParentCell()) {
                RE::BSTempEffectParticle::Spawn(cell, a_puff.life, ModelOf(a_puff.effect), RE::NiPoint3(0.0f, 0.0f, 1.0f), a_at, a_puff.scale, 7, nullptr);
            }
        }

        // The black smoke's model for slot a_slot, built the first time it is needed.
        bool EnsureSmoke(std::uint32_t a_slot)
        {
            const std::uint32_t id = kSmokeModelId + a_slot * 2;
            if (ModelView::Has(id)) {
                return true;
            }
            if (state.smokeFailed) {
                return false;
            }
            if (!ModelView::AddModel(id, SkinModel::Serialize(SmokeModel::Build()))) {
                state.smokeFailed = true;
                SKSE::log::warn("vehicle fx: the black smoke model did not build, no black smoke will show");
                return false;
            }
            SKSE::log::info("vehicle fx: the black smoke model for slot {} built ({} puffs)", a_slot, SmokeModel::kBones);
            return true;
        }

        // The slot's black smoke, a trail of puffs in the world drawn around the vehicle's position.
        void DrawBlack(std::uint32_t a_index, Slot& a_slot, const RE::NiPoint3& a_origin)
        {
            if (a_slot.black.Empty() || !EnsureSmoke(a_index)) {
                return;
            }
            Protocol::BodyState pose{};
            pose.frame = ++a_slot.frame;
            pose.flags = Protocol::kBodyValid;
            pose.model = kSmokeModelId + a_index * 2;
            pose.cameraForward[1] = 1.0f;
            a_slot.black.Pose({ a_origin.x, a_origin.y, a_origin.z }, pose);
            ModelView::Show(pose, a_origin);
        }

        const char* NameOf(Stage a_stage)
        {
            switch (a_stage) {
            case Stage::Smoke: return "smokes";
            case Stage::Fire: return "burns";
            case Stage::Wreck: return "is a wreck";
            default: return "is fine";
            }
        }
    }

    void Show(RE::PlayerCharacter* a_player, std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet,
        std::uint32_t a_handover, float a_delta)
    {
        if (a_slot >= Protocol::kVehicleMax) {
            return;
        }
        Slot& slot = state.slots[a_slot];
        const bool wrecked = a_pose && (a_pose->flags & Protocol::kBodyWrecked) != 0;
        if (slot.blowUp.BlewUp(a_pose != nullptr, a_handover, wrecked)) {
            // CJ in it is killed by San Andreas; next to it Skyrim's blast hurts him (San Andreas's doesn't).
            Projectiles::VehicleBlast(a_player, a_feet + RE::NiPoint3(0.0f, 0.0f, kBlastLift), (a_pose->flags & Protocol::kBodyDriven) == 0);
        }
        if (!a_pose) {
            slot.stage = Stage::Fine;
            slot.black.Clear();
            return;
        }
        const Stage stage = StageOf(a_pose->flags, a_pose->health);
        const float level = SmokeLevel(a_pose->health);
        const bool  black = BlackSmoke(stage, level, (a_pose->flags & Protocol::kBodyBurning) != 0);
        if (stage != slot.stage) {
            SKSE::log::info("vehicle fx: the vehicle in slot {} {}{} (health {:.0f})", a_slot, NameOf(stage), black ? ", black smoke" : "", a_pose->health);
            slot.stage = stage;
            slot.smoke = slot.fire = 1e3f;  // the new stage shows at once
        }
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        slot.smoke += dt;
        slot.fire += dt;
        const RE::NiPoint3 engine = a_feet + RE::NiPoint3(a_pose->engine[0], a_pose->engine[1], a_pose->engine[2]);
        if (WhiteSmoke(stage, level)) {
            Puff(a_player, SmokePuff(level), slot.smoke, engine);
        }
        if (stage == Stage::Fire) {
            Puff(a_player, kEngineFire, slot.fire, engine);
        } else if (stage == Stage::Wreck && (a_pose->flags & Protocol::kBodyBurning)) {
            Puff(a_player, kWreckFire, slot.fire, a_feet + RE::NiPoint3(0.0f, 0.0f, kWreckFireLift));
        }
        slot.black.Step(dt, black, { engine.x, engine.y, engine.z });
        DrawBlack(a_slot, slot, a_feet);
    }

    void Clear()
    {
        for (auto& slot : state.slots) {
            slot = Slot{};
        }
    }
}
