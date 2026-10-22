#include "player/VehicleFx.h"

#include "Ini.h"
#include "body/ModelView.h"
#include "player/Projectiles.h"
#include "smoke_model.h"
#include "txd.h"
#include "vehicle_damage.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>

namespace AhShitRim::VehicleFx
{
    using namespace VehicleDamage;

    namespace
    {
        constexpr const char*   kFireModel = "Effects\\FXFire02New.nif";  // Skyrim's small fire, as on CJ while he burns
        constexpr float         kBlastLift = 40.0f;  // Skyrim units: the fireball goes off this far over the vehicle's middle
        constexpr float         kWreckFireLift = 30.0f;
        constexpr std::uint32_t kSmokeModelId = 0xB5A00000;  // + 2 per slot and prim: even, so never an id San Andreas sends
        constexpr int           kPrims = 2;                  // San Andreas's white and black smoke

        const SmokeModel::Prim& PrimOf(int a_prim) { return a_prim == 0 ? SmokeModel::kWhite : SmokeModel::kBlack; }
        std::uint32_t           SmokeIdOf(std::uint32_t a_slot, int a_prim) { return kSmokeModelId + (a_slot * kPrims + a_prim) * 2; }

        struct Slot
        {
            Watch             blowUp;
            Stage             stage = Stage::Fine;
            float             fire = 0.0f;  // seconds since the last fire was spawned
            SmokeModel::Trail smoke[kPrims] = { SmokeModel::Trail(SmokeModel::kWhite), SmokeModel::Trail(SmokeModel::kBlack) };
            std::uint32_t     frame = 0;
            RE::NiPoint3      engine;  // where the engine was last frame, for its speed
            bool              moved = false;
        };

        struct State
        {
            Slot                                slots[Protocol::kVehicleMax];
            bool                                smokeFailed = false;
            bool                                spriteRead = false;
            std::optional<SkinModel::TextureIn> sprite;
        } state;

        // a_fire at a_at once a_since has reached its interval: its layers stacked up from there.
        void SpawnFire(RE::PlayerCharacter* a_player, const Fire& a_fire, float& a_since, const RE::NiPoint3& a_at)
        {
            if (a_since < a_fire.every) {
                return;
            }
            a_since = 0.0f;
            auto* cell = a_player->GetParentCell();
            if (!cell) {
                return;
            }
            for (int layer = 0; layer < a_fire.layers; ++layer) {
                RE::BSTempEffectParticle::Spawn(cell, a_fire.life, kFireModel, RE::NiPoint3(0.0f, 0.0f, 1.0f), a_at + RE::NiPoint3(0.0f, 0.0f, a_fire.HeightOf(layer)),
                    a_fire.ScaleOf(layer), 7, nullptr);
            }
        }

        // San Andreas's smoke sprite, read once from the player's own install (never written anywhere).
        const SkinModel::TextureIn* Sprite()
        {
            if (!state.spriteRead) {
                state.spriteRead = true;
                const std::filesystem::path     sa = Ini::Value("SanAndreas", "Path");
                const auto                      path = sa / "models" / "effectsPC.txd";
                std::ifstream                   file(path, std::ios::binary);
                const std::vector<std::uint8_t> data = file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
                const auto                      textures = Txd::Read(data.data(), data.size());
                if (const auto* found = Txd::Find(textures, SmokeModel::kTextureName)) {
                    state.sprite = SkinModel::TextureIn{ found->width, found->height, found->rgba };
                    SKSE::log::info("vehicle fx: the smoke is San Andreas's {} ({}x{}) from {}", SmokeModel::kTextureName, found->width, found->height,
                        path.string());
                } else {
                    SKSE::log::warn("vehicle fx: no {} in {} ({} bytes), no smoke will show", SmokeModel::kTextureName, path.string(), data.size());
                }
            }
            return state.sprite ? &*state.sprite : nullptr;
        }

        // A smoke prim's model for slot a_slot, built the first time it is needed.
        bool EnsureSmoke(std::uint32_t a_slot, int a_prim)
        {
            const std::uint32_t id = SmokeIdOf(a_slot, a_prim);
            if (ModelView::Has(id)) {
                return true;
            }
            const auto* sprite = state.smokeFailed ? nullptr : Sprite();
            if (!sprite) {
                return false;
            }
            if (!ModelView::AddModel(id, SkinModel::Serialize(SmokeModel::Build(PrimOf(a_prim), *sprite)))) {
                state.smokeFailed = true;
                SKSE::log::warn("vehicle fx: the smoke model did not build, no smoke will show");
                return false;
            }
            SKSE::log::info("vehicle fx: the {} model for slot {} built ({} sprites)", PrimOf(a_prim).name, a_slot, SmokeModel::kBones);
            return true;
        }

        // The slot's smoke: each prim's sprites in the world, drawn around the vehicle's position, facing the camera.
        void DrawSmoke(std::uint32_t a_index, Slot& a_slot, const RE::NiPoint3& a_origin)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* root = camera ? camera->cameraRoot.get() : nullptr;
            if (!root) {
                return;
            }
            const auto& eye = root->world.translate;
            for (int prim = 0; prim < kPrims; ++prim) {
                if (a_slot.smoke[prim].Empty() || !EnsureSmoke(a_index, prim)) {
                    continue;
                }
                Protocol::BodyState pose{};
                pose.frame = ++a_slot.frame;
                pose.flags = Protocol::kBodyValid;
                pose.model = SmokeIdOf(a_index, prim);
                pose.cameraForward[1] = 1.0f;
                a_slot.smoke[prim].Pose({ a_origin.x, a_origin.y, a_origin.z }, { eye.x, eye.y, eye.z }, pose);
                ModelView::Show(pose, a_origin);
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
        Slot&      slot = state.slots[a_slot];
        const bool wrecked = a_pose && (a_pose->flags & Protocol::kBodyWrecked) != 0;
        if (slot.blowUp.BlewUp(a_pose != nullptr, a_handover, wrecked)) {
            // CJ in it is killed by San Andreas; next to it Skyrim's blast hurts him (San Andreas's doesn't).
            Projectiles::VehicleBlast(a_player, a_feet + RE::NiPoint3(0.0f, 0.0f, kBlastLift), (a_pose->flags & Protocol::kBodyDriven) == 0);
        }
        if (!a_pose) {
            slot.stage = Stage::Fine;
            for (auto& trail : slot.smoke) {
                trail.Clear();
            }
            slot.moved = false;
            return;
        }
        const Stage stage = StageOf(a_pose->flags, a_pose->health);
        const bool  burning = (a_pose->flags & Protocol::kBodyBurning) != 0;
        if (stage != slot.stage) {
            SKSE::log::info("vehicle fx: the vehicle in slot {} {} (health {:.0f})", a_slot, NameOf(stage), a_pose->health);
            slot.stage = stage;
            slot.fire = 1e3f;  // the new stage shows at once
        }
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        slot.fire += dt;
        const RE::NiPoint3 engine = a_feet + RE::NiPoint3(a_pose->engine[0], a_pose->engine[1], a_pose->engine[2]);
        if (stage == Stage::Fire) {
            SpawnFire(a_player, kEngineFire, slot.fire, engine);
        } else if (stage == Stage::Wreck && burning) {
            SpawnFire(a_player, kWreckFire, slot.fire, a_feet + RE::NiPoint3(0.0f, 0.0f, kWreckFireLift));
        }
        // San Andreas's own smoke at its strength, carried along at the vehicle's speed as SetVelAdd does.
        RE::NiPoint3 carry;
        if (slot.moved && dt > 0.0f) {
            carry = (engine - slot.engine) / dt;
            if (carry.Length() > 60.0f * SmokeModel::kMeter) {
                carry = {};  // a jump (a re-anchor), not a speed
            }
        }
        slot.engine = engine;
        slot.moved = true;
        const Smoke  smoke = SmokeOf(stage, SmokeLevel(a_pose->health), burning);
        const float  levels[kPrims] = { smoke.white, smoke.black };
        const Vec3   at{ engine.x, engine.y, engine.z };
        const Vec3   along{ carry.x, carry.y, carry.z };
        for (int prim = 0; prim < kPrims; ++prim) {
            slot.smoke[prim].Step(dt, levels[prim], at, along);
        }
        DrawSmoke(a_slot, slot, a_feet);
    }

    void Clear()
    {
        for (auto& slot : state.slots) {
            slot = Slot{};
        }
    }
}
