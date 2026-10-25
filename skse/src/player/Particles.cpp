#include "player/Particles.h"

#include "Ini.h"
#include "body/ModelView.h"
#include "fx_model.h"
#include "txd.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace AhShitRim::Particles
{
    using FxModel::Vec3;

    namespace
    {
        constexpr std::uint32_t kModelBase = 0xB5C00000;  // + 2 per stream: even, so never an id San Andreas sends
        constexpr float         kJumpSpeed = 60.0f * FxModel::kMeter;  // faster than this between frames is a re-anchor, not a speed

        // One prim's sprites and the model they are drawn with.
        struct Stream
        {
            FxModel::Emitter emitter;
            FxModel::Mult    mult;
            std::uint32_t    id = 0;
            std::uint32_t    frame = 0;
            bool             failed = false;
        };

        enum VehiclePrim
        {
            kExhaust,
            kNitroCooler,
            kNitroFlame,
            kNitroCore,
            kDust,
            kVehiclePrims
        };

        struct Vehicle
        {
            Stream       streams[kVehiclePrims];
            RE::NiPoint3 feet;
            bool         seen = false;
        };

        // A one-off system (tank_fire, gunflash): its prims and the seconds since it last started.
        struct OneOff
        {
            Stream          streams[2];
            int             count = 1;
            float           time = 1e3f;
            FxModel::Source source;
        };

        struct State
        {
            Vehicle                                                     vehicles[Protocol::kVehicleMax];
            Stream                                                      jets[2];
            RE::NiPoint3                                                jetAt;  // the first jet last frame, for the flames to go along with it
            bool                                                        jetSeen = false;
            OneOff                                                      tank;
            OneOff                                                      gun;
            bool                                                        read = false;
            std::map<std::string, std::optional<SkinModel::TextureIn>> textures;
            std::vector<Txd::Texture>                                   txd;

            State()
            {
                std::uint32_t next = kModelBase;
                const auto    set = [&](Stream& a_stream, const FxModel::Prim& a_prim, const FxModel::Mult& a_mult = {}) {
                    a_stream.emitter = FxModel::Emitter(a_prim);
                    a_stream.mult = a_mult;
                    a_stream.id = next;
                    next += 2;
                };
                for (auto& vehicle : vehicles) {
                    set(vehicle.streams[kExhaust], FxModel::kExhaust, FxModel::kExhaustMult);
                    set(vehicle.streams[kNitroCooler], FxModel::kNitroCooler);
                    set(vehicle.streams[kNitroFlame], FxModel::kNitroFlame);
                    set(vehicle.streams[kNitroCore], FxModel::kNitroCore);
                    set(vehicle.streams[kDust], FxModel::kRotorDust);
                }
                set(jets[0], FxModel::kJetFlame);
                set(jets[1], FxModel::kJetGas);
                set(tank.streams[0], FxModel::kTankSmoke);
                set(tank.streams[1], FxModel::kTankFlash, FxModel::kFlashMult);
                tank.count = 2;
                set(gun.streams[0], FxModel::kGunFlash, FxModel::kFlashMult);
            }
        } state;

        Vec3         V(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
        RE::NiPoint3 Ni(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

        // A sprite from San Andreas's effectsPC.txd, read once from the player's own install (never written anywhere).
        const SkinModel::TextureIn* Texture(const char* a_name)
        {
            if (!state.read) {
                state.read = true;
                const std::filesystem::path     sa = Ini::Value("SanAndreas", "Path");
                const auto                      path = sa / "models" / "effectsPC.txd";
                std::ifstream                   file(path, std::ios::binary);
                const std::vector<std::uint8_t> data = file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
                state.txd = Txd::Read(data.data(), data.size());
                SKSE::log::info("particles: {} textures in {}", state.txd.size(), path.string());
            }
            auto& slot = state.textures[a_name];
            if (!slot) {
                const auto* found = Txd::Find(state.txd, a_name);
                if (!found) {
                    return nullptr;
                }
                slot = SkinModel::TextureIn{ found->width, found->height, found->rgba };
            }
            return &*slot;
        }

        bool Ensure(Stream& a_stream)
        {
            if (ModelView::Has(a_stream.id)) {
                return true;
            }
            if (a_stream.failed) {
                return false;
            }
            const auto& prim = a_stream.emitter.PrimOf();
            const auto* sprite = Texture(prim.texture);
            if (!sprite || !ModelView::AddModel(a_stream.id, SkinModel::Serialize(FxModel::Build(prim, *sprite, a_stream.mult)))) {
                a_stream.failed = true;
                SKSE::log::warn("particles: the {} model did not build ({} in San Andreas's effectsPC.txd: {}), it won't show", prim.name, prim.texture,
                    sprite ? "found" : "missing");
                return false;
            }
            SKSE::log::info("particles: the {} model built ({} sprites of San Andreas's {})", prim.name, FxModel::kBones, prim.texture);
            return true;
        }

        void Draw(Stream& a_stream)
        {
            auto*       camera = RE::PlayerCamera::GetSingleton();
            auto*       root = camera ? camera->cameraRoot.get() : nullptr;
            const auto& sprites = a_stream.emitter.Sprites();
            if (!root || sprites.empty() || !Ensure(a_stream)) {
                return;
            }
            const auto&         eye = root->world.translate;
            const auto&         middle = sprites[sprites.size() / 2].at;
            Protocol::BodyState pose{};
            pose.frame = ++a_stream.frame;
            pose.flags = Protocol::kBodyValid;
            pose.model = a_stream.id;
            pose.cameraForward[1] = 1.0f;
            a_stream.emitter.Pose(middle, { eye.x, eye.y, eye.z }, pose);
            ModelView::Show(pose, Ni(middle));
        }

        bool Has(const float (&a_along)[3]) { return a_along[0] != 0.0f || a_along[1] != 0.0f || a_along[2] != 0.0f; }

        // The fx block's sources (exhausts, jets), in Skyrim's world.
        int SourcesOf(const Protocol::BodyFx& a_fx, const RE::NiPoint3& a_feet, FxModel::Source (&a_out)[2])
        {
            int count = 0;
            for (int i = 0; i < 2; ++i) {
                if (Has(a_fx.along[i])) {
                    a_out[count++] = { { a_feet.x + a_fx.at[i][0], a_feet.y + a_fx.at[i][1], a_feet.z + a_fx.at[i][2] },
                        Body::Normalize({ a_fx.along[i][0], a_fx.along[i][1], a_fx.along[i][2] }, { 0.0f, 0.0f, -1.0f }) };
                }
            }
            return count;
        }

        void Fire(OneOff& a_system, const RE::NiPoint3& a_muzzle, const RE::NiPoint3& a_dir)
        {
            if (!std::isfinite(a_muzzle.x + a_muzzle.y + a_muzzle.z + a_dir.x + a_dir.y + a_dir.z)) {
                return;
            }
            a_system.time = 0.0f;
            a_system.source = { V(a_muzzle), Body::Normalize(V(a_dir), { 0.0f, 1.0f, 0.0f }) };
        }

        void Step(OneOff& a_system, float a_dt)
        {
            for (int i = 0; i < a_system.count; ++i) {
                auto& stream = a_system.streams[i];
                stream.emitter.Move(a_dt);
                if (a_system.time < FxModel::kOneOffLength) {
                    stream.emitter.Emit(a_dt, a_system.time, std::span(&a_system.source, 1));
                }
                Draw(stream);
            }
            a_system.time += a_dt;
        }
    }

    void ShowVehicle(std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet, float a_delta)
    {
        if (a_slot >= Protocol::kVehicleMax) {
            return;
        }
        Vehicle& vehicle = state.vehicles[a_slot];
        if (!a_pose) {
            for (auto& stream : vehicle.streams) {
                stream.emitter.Clear();
            }
            vehicle.seen = false;
            return;
        }
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        Vec3        shift{};
        Vec3        velocity{};
        if (vehicle.seen && dt > 0.0f) {
            const RE::NiPoint3 moved = a_feet - vehicle.feet;
            if (moved.Length() <= kJumpSpeed * dt) {
                shift = V(moved);
                velocity = Body::Scale(shift, 1.0f / dt);
            }
        }
        vehicle.feet = a_feet;
        vehicle.seen = true;

        const auto&     fx = a_pose->fx;
        FxModel::Source exhausts[2];
        const int       count = SourcesOf(fx, a_feet, exhausts);
        const auto      sources = std::span<const FxModel::Source>(exhausts, static_cast<std::size_t>(count));
        const float     gas = FxModel::GasFromByte(fx.gas);

        // The exhaust smoke goes its own way once out; the nitro's flames and the rotor dust go along with the vehicle.
        auto& smoke = vehicle.streams[kExhaust].emitter;
        smoke.Move(dt);
        if (count > 0) {
            const float speed = std::sqrt(Body::Dot(velocity, velocity)) / FxModel::kMeter;
            const float rate = FxModel::ExhaustRate(gas, (fx.flags & Protocol::kFxLowGear) != 0) * FxModel::ExhaustScale(speed) * static_cast<float>(count);
            const Vec3  forward = Body::Scale(exhausts[0].along, -1.0f);
            smoke.Puff(dt, rate, sources, FxModel::ExhaustVelocity(velocity, forward), FxModel::ExhaustLife(speed));
        }
        const float nitro = FxModel::FromByte(fx.level);
        for (const int prim : { kNitroCooler, kNitroFlame, kNitroCore }) {
            auto& emitter = vehicle.streams[prim].emitter;
            emitter.Move(dt, shift);
            emitter.Emit(dt, nitro, nitro > 0.0f ? sources : std::span<const FxModel::Source>{});
        }
        auto& dust = vehicle.streams[kDust].emitter;
        dust.Move(dt, shift);
        if (fx.dust > 0) {
            const FxModel::Source ground{ { a_feet.x, a_feet.y, a_feet.z + fx.ground }, { 0.0f, 0.0f, 1.0f } };
            dust.Emit(dt, FxModel::FromByte(fx.dust), std::span(&ground, 1));
        } else {
            dust.Emit(dt, 0.0f, {});
        }
        for (auto& stream : vehicle.streams) {
            Draw(stream);
        }
    }

    void ShowGear(const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet, float a_delta)
    {
        const float     dt = std::clamp(a_delta, 0.0f, 0.25f);
        FxModel::Source jets[2];
        const int       count = a_pose && (a_pose->flags & Protocol::kBodyJetpack) ? SourcesOf(a_pose->fx, a_feet, jets) : 0;
        Vec3            shift{};
        if (count > 0) {
            const RE::NiPoint3 at = Ni(jets[0].at);
            if (state.jetSeen && (at - state.jetAt).Length() <= kJumpSpeed * dt) {
                shift = V(at - state.jetAt);
            }
            state.jetAt = at;
        }
        state.jetSeen = count > 0;
        const float level = count > 0 ? FxModel::FromByte(a_pose->fx.level) : 0.0f;
        for (auto& stream : state.jets) {
            stream.emitter.Move(dt, shift);
            stream.emitter.Emit(dt, level, std::span<const FxModel::Source>(jets, static_cast<std::size_t>(count)));
            Draw(stream);
        }
    }

    void TankFire(const RE::NiPoint3& a_muzzle, const RE::NiPoint3& a_dir) { Fire(state.tank, a_muzzle, a_dir); }

    void GunFlash(const RE::NiPoint3& a_muzzle, const RE::NiPoint3& a_dir) { Fire(state.gun, a_muzzle, a_dir); }

    void Update(float a_delta)
    {
        const float dt = std::clamp(a_delta, 0.0f, 0.25f);
        Step(state.tank, dt);
        Step(state.gun, dt);
    }

    void Clear()
    {
        for (auto& vehicle : state.vehicles) {
            for (auto& stream : vehicle.streams) {
                stream.emitter.Clear();
            }
            vehicle.seen = false;
        }
        for (auto& stream : state.jets) {
            stream.emitter.Clear();
        }
        state.jetSeen = false;
        for (auto* system : { &state.tank, &state.gun }) {
            for (auto& stream : system->streams) {
                stream.emitter.Clear();
            }
            system->time = 1e3f;
        }
    }
}
