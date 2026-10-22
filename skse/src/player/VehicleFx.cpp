#include "player/VehicleFx.h"

#include "player/Projectiles.h"
#include "vehicle_damage.h"

#include <algorithm>
#include <format>
#include <vector>

namespace AhShitRim::VehicleFx
{
    using namespace VehicleDamage;

    namespace
    {
        // Vanilla effects (Skyrim - Meshes), spawned as Skyrim's temporary particles.
        constexpr const char* kLightSmokeModel = "Effects\\Ambient\\FXSmokeChimney01.nif";  // a chimney's grey smoke
        constexpr const char* kFireModel = "Effects\\FXFire02New.nif";                     // a small fire, as on CJ while he burns
        constexpr float       kBlastLift = 40.0f;       // Skyrim units: the fireball goes off this far over the vehicle's middle
        constexpr float       kWreckFireLift = 30.0f;

        struct Slot
        {
            Watch blowUp;
            Stage stage = Stage::Fine;
            float smoke = 0.0f;  // seconds since the last puff of smoke
            float fire = 0.0f;   // and of fire
        };

        struct State
        {
            Slot                                                 slots[Protocol::kVehicleMax];
            std::vector<RE::NiPointer<RE::BSTempEffectParticle>> untinted;  // dark puffs whose 3D hasn't come in yet
            RE::BSEffectShaderMaterial*                          dark = nullptr;  // the chimney smoke's material, near black; kept for good
            bool                                                 tintLogged = false;
        } state;

        const char* ModelOf(Effect a_effect) { return a_effect == Effect::Fire ? kFireModel : kLightSmokeModel; }

        // Tints a dark puff's smoke: each of its effect shaders gets its own copy of the chimney smoke's material
        // with the colour near black (SetMaterial with unique copies it, as ModelView's do). False while its 3D
        // is still being made.
        bool Tint(RE::BSTempEffectParticle* a_puff)
        {
            auto* root = a_puff->particleObject.get();
            if (!root) {
                return false;
            }
            int tinted = 0;
            RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
                auto* shader = netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
                auto* material = shader ? static_cast<RE::BSEffectShaderMaterial*>(shader->material) : nullptr;
                if (!material) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                if (!state.dark) {
                    state.dark = static_cast<RE::BSEffectShaderMaterial*>(material->Create());
                    state.dark->CopyMembers(material);
                    state.dark->baseColor.red *= kDarkTint;
                    state.dark->baseColor.green *= kDarkTint;
                    state.dark->baseColor.blue *= kDarkTint;
                }
                shader->SetMaterial(state.dark, true);
                tinted++;
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            if (!state.tintLogged) {
                state.tintLogged = true;
                SKSE::log::info("vehicle fx: the dark smoke is the chimney smoke tinted to {:.2f} ({} effect shaders)", kDarkTint, tinted);
            }
            return true;
        }

        // Dark puffs are tinted as soon as their 3D is in (often the frame they are made).
        void TintPending()
        {
            std::erase_if(state.untinted, [](const RE::NiPointer<RE::BSTempEffectParticle>& a_puff) { return Tint(a_puff.get()); });
        }

        // a_puff at a_at once a_since has reached its interval.
        void Puff(RE::PlayerCharacter* a_player, const VehicleDamage::Puff& a_puff, float& a_since, const RE::NiPoint3& a_at)
        {
            if (a_since < a_puff.every) {
                return;
            }
            a_since = 0.0f;
            auto* cell = a_player->GetParentCell();
            if (!cell) {
                return;
            }
            auto* puff = RE::BSTempEffectParticle::Spawn(cell, a_puff.life, ModelOf(a_puff.effect), RE::NiPoint3(0.0f, 0.0f, 1.0f), a_at, a_puff.scale, 7, nullptr);
            if (puff && a_puff.effect == Effect::DarkSmoke && !Tint(puff) && state.untinted.size() < 64) {
                state.untinted.emplace_back(puff);
            }
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
        if (a_slot == 0) {
            TintPending();
        }
        Slot& slot = state.slots[a_slot];
        const bool wrecked = a_pose && (a_pose->flags & Protocol::kBodyWrecked) != 0;
        if (slot.blowUp.BlewUp(a_pose != nullptr, a_handover, wrecked)) {
            // CJ in it is killed by San Andreas; next to it Skyrim's blast hurts him (San Andreas's doesn't).
            Projectiles::VehicleBlast(a_player, a_feet + RE::NiPoint3(0.0f, 0.0f, kBlastLift), (a_pose->flags & Protocol::kBodyDriven) == 0);
        }
        if (!a_pose) {
            slot.stage = Stage::Fine;
            return;
        }
        const Stage stage = StageOf(a_pose->flags, a_pose->health);
        if (stage != slot.stage) {
            SKSE::log::info("vehicle fx: the vehicle in slot {} {} (health {:.0f})", a_slot, NameOf(stage), a_pose->health);
            slot.stage = stage;
            slot.smoke = slot.fire = 1e3f;  // the new stage shows at once
        }
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        slot.smoke += dt;
        slot.fire += dt;
        const RE::NiPoint3 engine = a_feet + RE::NiPoint3(a_pose->engine[0], a_pose->engine[1], a_pose->engine[2]);
        switch (stage) {
        case Stage::Smoke:
            Puff(a_player, SmokePuff(SmokeLevel(a_pose->health)), slot.smoke, engine);
            break;
        case Stage::Fire:
            Puff(a_player, kEngineFire, slot.fire, engine);
            Puff(a_player, SmokePuff(1.0f), slot.smoke, engine);
            break;
        case Stage::Wreck:
            if (a_pose->flags & Protocol::kBodyBurning) {
                Puff(a_player, kWreckFire, slot.fire, a_feet + RE::NiPoint3(0.0f, 0.0f, kWreckFireLift));
                Puff(a_player, SmokePuff(1.0f), slot.smoke, engine);
            }
            break;
        default: break;
        }
    }

    void Clear()
    {
        for (auto& slot : state.slots) {
            slot = Slot{};
        }
        state.untinted.clear();
    }
}
