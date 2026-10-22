// A damaged vehicle's black smoke (protocol v18), as San Andreas draws it: its overheat_car system's
// BlackSmoke (models/effects.fxp), the bullethitsmoke sprite from models/effectsPC.txd drawn black, read
// from the player's own San Andreas install at startup. Skyrim's own smoke can't be turned black, so the
// sprites are drawn as camera-facing quads on bones of their own, through ModelView. Header-only and free
// of game types, so the protocol tests cover it.
//
// BlackSmoke's numbers, as they are: from none to 60 sprites a second from the engine as the smoke's strength
// (SmokeLevel, overheat_car's time) goes from 0.3 to 1, 0.8 s each, let out at 1.5 to
// 2.5 m/s up to 40 degrees off straight up, slowed by friction 0.96 a step, pushed up by 3 m/s², with the
// vehicle's own speed added (SetVelAdd); each grows from 0.2 to 1.0 m in its first fifth and on to 1.2 m,
// its alpha going 0, 128 at a fifth of its life, 0; it turns 30 degrees a second from a random start.
//
// A bone can't scale, so the model holds kAges sizes of sprite, kPerAge of each, every size in a mesh of its
// own with its own alpha: a sprite is drawn on a bone of the size and fade its age has reached. Unused bones
// go far below the ground.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "pose.h"
#include "skin_model.h"

namespace AhShitRim::SmokeModel
{
    using Pose::Vec3;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;
    inline constexpr int   kAges = 8;    // sizes a sprite goes through
    inline constexpr int   kPerAge = 8;  // sprites of each size drawn at once
    inline constexpr int   kBones = kAges * kPerAge;
    inline constexpr const char* kTextureName = "bullethitsmoke";  // in models/effectsPC.txd

    inline constexpr float kLife = 0.8f;                   // seconds a sprite lasts
    inline constexpr float kRateMax = 60.0f;               // sprites a second at full strength
    inline constexpr float kEvery = 1.0f / kRateMax;       // seconds between sprites then
    inline constexpr float kSpeedMin = 1.5f * kMeter;      // units a second it is let out at
    inline constexpr float kSpeedMax = 2.5f * kMeter;
    inline constexpr float kConeDegrees = 40.0f;           // off straight up at most
    inline constexpr float kFriction = 0.96f;              // of the speed kept each 1/50 s
    inline constexpr float kLift = 3.0f * kMeter;          // units a second², up
    inline constexpr float kSpin = 30.0f * 0.0174532925f;  // radians a second
    inline constexpr float kHidden = -100000.0f;           // where an unused bone goes

    static_assert(kBones <= static_cast<int>(Protocol::kBodyBoneMax));
    static_assert(kLife / kEvery / kAges <= kPerAge);

    // BlackSmoke's keys over a sprite's life t (0..1): half its size in metres, and its alpha (0..255).
    inline float HalfSizeAt(float a_t)
    {
        const float t = std::clamp(a_t, 0.0f, 1.0f);
        const float size = t < 0.2f ? 0.2f + (1.0f - 0.2f) * t / 0.2f : 1.0f + (1.2f - 1.0f) * (t - 0.2f) / 0.8f;
        return 0.5f * size;
    }

    inline float AlphaAt(float a_t)
    {
        const float t = std::clamp(a_t, 0.0f, 1.0f);
        return t < 0.2f ? 128.0f * t / 0.2f : 128.0f * (1.0f - (t - 0.2f) / 0.8f);
    }

    // BlackSmoke's RATE keys: none up to a strength of 0.3, then up to 60 a second at 1.
    inline float RateAt(float a_level)
    {
        const float level = std::clamp(a_level, 0.0f, 1.0f);
        return level <= 0.3f ? 0.0f : kRateMax * (level - 0.3f) / 0.7f;
    }

    // An age bucket's sprite is the keys at the bucket's middle.
    inline float BucketMiddle(int a_age) { return (static_cast<float>(a_age) + 0.5f) / kAges; }
    inline float SizeOf(int a_age) { return HalfSizeAt(BucketMiddle(a_age)) * kMeter; }
    inline std::uint8_t AlphaOf(int a_age) { return static_cast<std::uint8_t>(std::lround(AlphaAt(BucketMiddle(a_age)))); }
    inline int AgeBucket(float a_age) { return std::clamp(static_cast<int>(a_age / kLife * kAges), 0, kAges - 1); }

    // A small hash for the sprites' spread, start angle and speed, the same every run.
    inline float Hash(std::uint32_t a_n)
    {
        a_n = (a_n ^ 61u) ^ (a_n >> 16);
        a_n *= 9u;
        a_n ^= a_n >> 4;
        a_n *= 0x27d4eb2du;
        a_n ^= a_n >> 15;
        return static_cast<float>(a_n & 0xFFFFu) / 65535.0f;
    }

    // kBones sprites, one quad each in its bone's XZ plane facing -Y: bone b is of age bucket b / kPerAge,
    // each age bucket a blended mesh, black, of its own alpha. a_sprite is San Andreas's bullethitsmoke.
    inline SkinModel::ModelIn Build(const SkinModel::TextureIn& a_sprite)
    {
        SkinModel::ModelIn model;
        model.name = "BlackSmoke";
        model.kind = Protocol::kSkinModelVehicle;  // a vehicle's bounds: the smoke rises above its origin
        model.textures.push_back(a_sprite);
        for (int age = 0; age < kAges; ++age) {
            SkinModel::MeshIn mesh;
            mesh.texture = 0;
            mesh.flags = Protocol::kSkinMeshBlend;
            mesh.color[0] = mesh.color[1] = mesh.color[2] = 0;
            mesh.color[3] = AlphaOf(age);
            const float h = SizeOf(age);
            for (int k = 0; k < kPerAge; ++k) {
                const auto  bone = static_cast<std::uint32_t>(age * kPerAge + k);
                const auto  base = static_cast<std::uint16_t>(mesh.vertices.size());
                const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                model.boneIds.push_back(bone);
                for (const auto& corner : corners) {
                    Protocol::SkinVertex v{};
                    v.position[0] = corner[0] * h;
                    v.position[2] = corner[1] * h;
                    v.normal[1] = -1.0f;
                    v.uv[0] = corner[0] * 0.5f + 0.5f;
                    v.uv[1] = 0.5f - corner[1] * 0.5f;
                    v.bones[0] = static_cast<std::uint8_t>(bone);
                    v.weights[0] = 1.0f;
                    mesh.vertices.push_back(v);
                }
                for (const std::uint16_t i : { 0, 1, 2, 0, 2, 3 }) {
                    mesh.indices.push_back(static_cast<std::uint16_t>(base + i));
                }
            }
            model.meshes.push_back(std::move(mesh));
        }
        return model;
    }

    // The turn that faces a sprite's quad (its -Y) at a_camera from a_at, rolled a_roll radians about the line between.
    inline Body::Quat Facing(const Vec3& a_at, const Vec3& a_camera, float a_roll)
    {
        const Vec3 away = Body::Normalize(Body::Sub(a_at, a_camera), { 0.0f, 1.0f, 0.0f });
        const Vec3 side = Body::Normalize(Body::Cross(away, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
        const Vec3 up = Body::Cross(side, away);
        const Vec3 right = Body::Add(Body::Scale(side, std::cos(a_roll)), Body::Scale(up, std::sin(a_roll)));
        return Body::FromAxes(right, away);
    }

    // One vehicle's black smoke: sprites let out at the engine while it smokes black, moving on their own
    // from there, and drawn until they are gone.
    class Trail
    {
    public:
        struct Sprite
        {
            Vec3          at;
            Vec3          speed;  // units a second
            float         age = 0.0f;
            float         roll = 0.0f;
            std::uint32_t n = 0;
        };

        // a_dt seconds on; a_rate sprites a second come out at a_engine (none: 0), which moves at a_carry units a
        // second (the vehicle's speed, added to theirs).
        void Step(float a_dt, float a_rate, const Vec3& a_engine, const Vec3& a_carry = {})
        {
            const float dt = std::clamp(a_dt, 0.0f, 0.25f);
            const float keep = std::pow(kFriction, dt * 50.0f);
            for (auto& sprite : _sprites) {
                sprite.age += dt;
                sprite.speed = Body::Scale(sprite.speed, keep);
                sprite.speed.z += kLift * dt;
                sprite.at = Body::Add(sprite.at, Body::Scale(sprite.speed, dt));
                sprite.roll += kSpin * dt;
            }
            std::erase_if(_sprites, [](const Sprite& a_sprite) { return a_sprite.age >= kLife; });
            if (!(a_rate > 0.0f)) {
                _since = 1.0f;  // the first sprite comes at once when it starts again
                return;
            }
            const float every = 1.0f / (std::min)(a_rate, kRateMax);
            _since = (std::min)(_since, every) + dt;
            while (_since >= every && _sprites.size() < static_cast<std::size_t>(kBones)) {
                _since -= every;
                const std::uint32_t n = _count++;
                const float         heading = Hash(n * 3u) * 6.2831853f;
                const float         tilt = Hash(n * 3u + 1u) * kConeDegrees * 0.0174532925f;
                const float         speed = kSpeedMin + (kSpeedMax - kSpeedMin) * Hash(n * 3u + 2u);
                const Vec3          dir{ std::sin(tilt) * std::cos(heading), std::sin(tilt) * std::sin(heading), std::cos(tilt) };
                _sprites.push_back({ a_engine, Body::Add(Body::Scale(dir, speed), a_carry), _since, Hash(n * 7u + 5u) * 6.2831853f, n });
            }
        }

        bool Empty() const { return _sprites.empty(); }
        const std::vector<Sprite>& Sprites() const { return _sprites; }

        // Every bone relative to a_origin: each sprite on a free bone of its age's size, facing a_camera; the
        // rest hidden.
        void Pose(const Vec3& a_origin, const Vec3& a_camera, Protocol::BodyState& a_out) const
        {
            std::array<int, kAges> used{};
            a_out.boneCount = kBones;
            for (int b = 0; b < kBones; ++b) {
                auto& bone = a_out.bones[b];
                bone.rotation[0] = bone.rotation[1] = bone.rotation[2] = 0.0f;
                bone.rotation[3] = 1.0f;
                bone.translation[0] = bone.translation[1] = 0.0f;
                bone.translation[2] = kHidden;
            }
            for (const auto& sprite : _sprites) {
                const int age = AgeBucket(sprite.age);
                if (used[age] >= kPerAge) {
                    continue;
                }
                auto&            bone = a_out.bones[age * kPerAge + used[age]++];
                const Body::Quat q = Facing(sprite.at, a_camera, sprite.roll);
                bone.rotation[0] = q.x;
                bone.rotation[1] = q.y;
                bone.rotation[2] = q.z;
                bone.rotation[3] = q.w;
                bone.translation[0] = sprite.at.x - a_origin.x;
                bone.translation[1] = sprite.at.y - a_origin.y;
                bone.translation[2] = sprite.at.z - a_origin.z;
            }
        }

        void Clear()
        {
            _sprites.clear();
            _since = 1.0f;
        }

    private:
        std::vector<Sprite> _sprites;
        float               _since = 1.0f;  // seconds since the last sprite
        std::uint32_t       _count = 0;
    };
}
