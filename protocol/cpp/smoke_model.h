// A damaged vehicle's smoke (protocol v18), as San Andreas draws it: its overheat_car system's two prims
// (models/effects.fxp), WhiteSmoke and BlackSmoke, both the bullethitsmoke sprite from models/effectsPC.txd,
// read from the player's own San Andreas install at startup. Skyrim's own smoke can't be turned black or
// follow these numbers, so the sprites are drawn as camera-facing quads on bones of their own, through
// ModelView. Header-only and free of game types, so the protocol tests cover it.
//
// San Andreas drives overheat_car with the smoke's strength (SmokeLevel: 0 at 650 health, 1 at 250) as the
// system's time, and each prim's keys over it say how many sprites a second it lets out, how long they last
// and how fast they go; each sprite's own keys over its life say its size and alpha. Both are let out at the
// engine up to 40 degrees off straight up, slowed by friction 0.96 a step, pushed up by 3 m/s², with the
// vehicle's own speed added (SetVelAdd), and turn 30 to 60 degrees a second from a random start:
//  - WhiteSmoke: none, up to 60 a second at a strength of 0.5, none again at 1; 0.4 to 0.6 s; 1.5 to 2 m/s;
//    0.1 m growing to 0.8 m in its first tenth; white.
//  - BlackSmoke: none up to 0.3, then up to 60 a second at 1; 0.4 to 0.8 s; 1.5 to 2.5 m/s; 0.2 m growing to
//    1 m in its first fifth and on to 1.2 m; black.
// Both fade in to alpha 128 at a fifth of their life, then out.
//
// A bone can't scale, so a prim's model holds kAges sizes of sprite, kPerAge of each, every size in a mesh
// of its own with its own alpha: a sprite is drawn on a bone of the size and fade its age has reached.
// Unused bones go far below the ground.

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

    inline constexpr float       kMeter = Pose::kUnitsPerMeter;
    inline constexpr int         kAges = 8;    // sizes a sprite goes through
    inline constexpr int         kPerAge = 8;  // sprites of each size drawn at once
    inline constexpr int         kBones = kAges * kPerAge;
    inline constexpr const char* kTextureName = "bullethitsmoke";  // in models/effectsPC.txd
    inline constexpr float       kRateMax = 60.0f;                 // sprites a second, either prim at its most
    inline constexpr float       kConeDegrees = 40.0f;              // off straight up at most
    inline constexpr float       kFriction = 0.96f;                 // of the speed kept each 1/50 s
    inline constexpr float       kLift = 3.0f * kMeter;             // units a second², up
    inline constexpr float       kSpinMin = 30.0f * 0.0174532925f;  // radians a second
    inline constexpr float       kSpinMax = 60.0f * 0.0174532925f;
    inline constexpr float       kHidden = -100000.0f;              // where an unused bone goes

    static_assert(kBones <= static_cast<int>(Protocol::kBodyBoneMax));

    // Three keys of effects.fxp: linear between them, flat past the ends.
    struct Keys
    {
        float t[3];
        float v[3];

        constexpr float At(float a_t) const
        {
            if (a_t <= t[0]) {
                return v[0];
            }
            for (int i = 1; i < 3; ++i) {
                if (a_t <= t[i]) {
                    return v[i - 1] + (v[i] - v[i - 1]) * (a_t - t[i - 1]) / (t[i] - t[i - 1]);
                }
            }
            return v[2];
        }
    };

    struct Prim
    {
        const char*  name;
        std::uint8_t shade;      // its colour, grey: 255 white, 0 black
        Keys         rate;       // sprites a second, over the strength
        Keys         life;       // seconds, over the strength
        Keys         speed;      // metres a second, over the strength
        Keys         speedBias;  // and how far a sprite's speed strays either way
        Keys         size;       // metres across, over a sprite's life
        Keys         alpha;      // 0..255, over a sprite's life
    };

    inline constexpr Prim kWhite{ "WhiteSmoke", 255, { { 0.0f, 0.5f, 1.0f }, { 0.0f, 60.0f, 0.0f } }, { { 0.0f, 0.5f, 1.0f }, { 0.4f, 0.5f, 0.6f } },
        { { 0.0f, 0.5f, 1.0f }, { 1.5f, 1.75f, 2.0f } }, { { 0.0f, 0.5f, 1.0f }, { 0.2f, 0.25f, 0.3f } }, { { 0.0f, 0.1f, 1.0f }, { 0.1f, 0.8f, 0.8f } },
        { { 0.0f, 0.2f, 1.0f }, { 0.0f, 128.0f, 0.0f } } };
    inline constexpr Prim kBlack{ "BlackSmoke", 0, { { 0.0f, 0.3f, 1.0f }, { 0.0f, 0.0f, 60.0f } }, { { 0.0f, 0.5f, 1.0f }, { 0.4f, 0.6f, 0.8f } },
        { { 0.0f, 0.5f, 1.0f }, { 1.5f, 2.0f, 2.5f } }, { { 0.0f, 0.5f, 1.0f }, { 0.2f, 0.25f, 0.3f } }, { { 0.0f, 0.2f, 1.0f }, { 0.2f, 1.0f, 1.2f } },
        { { 0.0f, 0.2f, 1.0f }, { 0.0f, 128.0f, 0.0f } } };

    // At most 60 a second for 0.8 s fit kPerAge to an age bucket.
    static_assert(kRateMax * 0.8f / kAges <= kPerAge);

    inline float RateAt(const Prim& a_prim, float a_level) { return a_prim.rate.At(std::clamp(a_level, 0.0f, 1.0f)); }

    // An age bucket's sprite is the keys at the bucket's middle: half its size in Skyrim units, its alpha.
    inline float BucketMiddle(int a_age) { return (static_cast<float>(a_age) + 0.5f) / kAges; }
    inline float SizeOf(const Prim& a_prim, int a_age) { return 0.5f * a_prim.size.At(BucketMiddle(a_age)) * kMeter; }
    inline std::uint8_t AlphaOf(const Prim& a_prim, int a_age) { return static_cast<std::uint8_t>(std::lround(a_prim.alpha.At(BucketMiddle(a_age)))); }
    inline int AgeBucket(float a_t) { return std::clamp(static_cast<int>(a_t * kAges), 0, kAges - 1); }

    // A small hash for the sprites' spread, speed, start angle and spin, the same every run.
    inline float Hash(std::uint32_t a_n)
    {
        a_n = (a_n ^ 61u) ^ (a_n >> 16);
        a_n *= 9u;
        a_n ^= a_n >> 4;
        a_n *= 0x27d4eb2du;
        a_n ^= a_n >> 15;
        return static_cast<float>(a_n & 0xFFFFu) / 65535.0f;
    }

    // a_prim's kBones sprites, one quad each in its bone's XZ plane facing -Y: bone b is of age bucket
    // b / kPerAge, each age bucket a blended mesh in the prim's colour and of its own alpha. a_sprite is San
    // Andreas's bullethitsmoke.
    inline SkinModel::ModelIn Build(const Prim& a_prim, const SkinModel::TextureIn& a_sprite)
    {
        SkinModel::ModelIn model;
        model.name = a_prim.name;
        model.kind = Protocol::kSkinModelVehicle;  // a vehicle's bounds: the smoke rises above its origin
        model.textures.push_back(a_sprite);
        for (int age = 0; age < kAges; ++age) {
            SkinModel::MeshIn mesh;
            mesh.texture = 0;
            mesh.flags = Protocol::kSkinMeshBlend;
            mesh.color[0] = mesh.color[1] = mesh.color[2] = a_prim.shade;
            mesh.color[3] = AlphaOf(a_prim, age);
            const float h = SizeOf(a_prim, age);
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

    // One prim of one vehicle's smoke: sprites let out at the engine, moving on their own from there, and
    // drawn until they are gone.
    class Trail
    {
    public:
        struct Sprite
        {
            Vec3          at;
            Vec3          speed;  // units a second
            float         age = 0.0f;
            float         life = 1.0f;
            float         roll = 0.0f;
            float         spin = 0.0f;
            std::uint32_t n = 0;
        };

        explicit Trail(const Prim& a_prim = kBlack) : _prim(&a_prim) {}

        // a_dt seconds on at strength a_level (SmokeLevel; negative lets nothing out): the prim's sprites come
        // out at a_engine, which moves at a_carry units a second (the vehicle's speed, added to theirs).
        void Step(float a_dt, float a_level, const Vec3& a_engine, const Vec3& a_carry = {})
        {
            const float dt = std::clamp(a_dt, 0.0f, 0.25f);
            const float keep = std::pow(kFriction, dt * 50.0f);
            for (auto& sprite : _sprites) {
                sprite.age += dt;
                sprite.speed = Body::Scale(sprite.speed, keep);
                sprite.speed.z += kLift * dt;
                sprite.at = Body::Add(sprite.at, Body::Scale(sprite.speed, dt));
                sprite.roll += sprite.spin * dt;
            }
            std::erase_if(_sprites, [](const Sprite& a_sprite) { return a_sprite.age >= a_sprite.life; });
            const float rate = a_level < 0.0f ? 0.0f : RateAt(*_prim, a_level);
            if (!(rate > 0.0f)) {
                _since = 1.0f;  // the first sprite comes at once when it starts again
                return;
            }
            const float level = std::clamp(a_level, 0.0f, 1.0f);
            const float every = 1.0f / (std::min)(rate, kRateMax);
            _since = (std::min)(_since, every) + dt;
            while (_since >= every && _sprites.size() < static_cast<std::size_t>(kBones)) {
                _since -= every;
                const std::uint32_t n = _count++;
                const float         heading = Hash(n * 5u) * 6.2831853f;
                const float         tilt = Hash(n * 5u + 1u) * kConeDegrees * 0.0174532925f;
                const float         bias = _prim->speedBias.At(level);
                const float         speed = (_prim->speed.At(level) + bias * (2.0f * Hash(n * 5u + 2u) - 1.0f)) * kMeter;
                const Vec3          dir{ std::sin(tilt) * std::cos(heading), std::sin(tilt) * std::sin(heading), std::cos(tilt) };
                Sprite              sprite;
                sprite.at = a_engine;
                sprite.speed = Body::Add(Body::Scale(dir, speed), a_carry);
                sprite.age = _since;
                sprite.life = _prim->life.At(level);
                sprite.roll = Hash(n * 5u + 3u) * 6.2831853f;
                sprite.spin = kSpinMin + (kSpinMax - kSpinMin) * Hash(n * 5u + 4u);
                sprite.n = n;
                _sprites.push_back(sprite);
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
                const int age = AgeBucket(sprite.age / sprite.life);
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
        const Prim*         _prim;
        std::vector<Sprite> _sprites;
        float               _since = 1.0f;  // seconds since the last sprite
        std::uint32_t       _count = 0;
    };
}
