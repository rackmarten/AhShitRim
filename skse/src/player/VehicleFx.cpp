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

        // San Andreas's black smoke sprite, read once from the player's own install (never written anywhere).
        const SkinModel::TextureIn* Sprite()
        {
            if (!state.spriteRead) {
                state.spriteRead = true;
                const std::filesystem::path sa = Ini::Value("SanAndreas", "Path");
                const auto                  path = sa / "models" / "effectsPC.txd";
                std::ifstream               file(path, std::ios::binary);
                const std::vector<std::uint8_t> data = file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
                const auto                  textures = Txd::Read(data.data(), data.size());
                if (const auto* found = Txd::Find(textures, SmokeModel::kTextureName)) {
                    state.sprite = SkinModel::TextureIn{ found->width, found->height, found->rgba };
                    SKSE::log::info("vehicle fx: the black smoke is San Andreas's {} ({}x{}) from {}", SmokeModel::kTextureName, found->width, found->height,
                        path.string());
                } else {
                    SKSE::log::warn("vehicle fx: no {} in {} ({} bytes), no black smoke will show", SmokeModel::kTextureName, path.string(), data.size());
                }
            }
            return state.sprite ? &*state.sprite : nullptr;
        }

        // The black smoke's model for slot a_slot, built the first time it is needed.
        bool EnsureSmoke(std::uint32_t a_slot)
        {
            const std::uint32_t id = kSmokeModelId + a_slot * 2;
            if (ModelView::Has(id)) {
                return true;
            }
            const auto* sprite = state.smokeFailed ? nullptr : Sprite();
            if (!sprite) {
                return false;
            }
            if (!ModelView::AddModel(id, SkinModel::Serialize(SmokeModel::Build(*sprite)))) {
                state.smokeFailed = true;
                SKSE::log::warn("vehicle fx: the black smoke model did not build, no black smoke will show");
                return false;
            }
            SKSE::log::info("vehicle fx: the black smoke model for slot {} built ({} sprites)", a_slot, SmokeModel::kBones);
            return true;
        }

        // The slot's black smoke: its sprites in the world drawn around the vehicle's position, facing the camera.
        void DrawBlack(std::uint32_t a_index, Slot& a_slot, const RE::NiPoint3& a_origin)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* root = camera ? camera->cameraRoot.get() : nullptr;
            if (a_slot.black.Empty() || !root || !EnsureSmoke(a_index)) {
                return;
            }
            const auto&         eye = root->world.translate;
            Protocol::BodyState pose{};
            pose.frame = ++a_slot.frame;
            pose.flags = Protocol::kBodyValid;
            pose.model = kSmokeModelId + a_index * 2;
            pose.cameraForward[1] = 1.0f;
            a_slot.black.Pose({ a_origin.x, a_origin.y, a_origin.z }, { eye.x, eye.y, eye.z }, pose);
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
            slot.moved = false;
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
        // San Andreas's own black smoke, at its own rate for the strength (full through the fire), carried
        // along at the vehicle's speed as SetVelAdd does.
        const float rate = !black ? 0.0f : stage == Stage::Smoke ? SmokeModel::RateAt(level) : SmokeModel::kRateMax;
        RE::NiPoint3 carry;
        if (slot.moved && dt > 0.0f) {
            carry = (engine - slot.engine) / dt;
            if (carry.Length() > 60.0f * SmokeModel::kMeter) {
                carry = {};  // a jump (a re-anchor), not a speed
            }
        }
        slot.engine = engine;
        slot.moved = true;
        slot.black.Step(dt, rate, { engine.x, engine.y, engine.z }, { carry.x, carry.y, carry.z });
        DrawBlack(a_slot, slot, a_feet);
    }

    void Clear()
    {
        for (auto& slot : state.slots) {
            slot = Slot{};
        }
    }
}
