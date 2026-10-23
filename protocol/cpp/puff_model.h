// The spray can's and the fire extinguisher's clouds, as San Andreas draws them: the prims of their
// spraycan and extinguisher systems (models/effects.fxp, played by CWeapon::DoWeaponEffect at system time 1),
// each a sprite from models/effectsPC.txd read from the player's own San Andreas install. They are drawn
// like the vehicles' smoke (smoke_model.h): camera-facing quads on bones of their own, through ModelView.
// Header-only and free of game types, so the protocol tests cover it.
//
// Each prim lets its sprites out along the nozzle's line, up to a few degrees off it, at a rate a second,
// a speed and a life of its own; a force (San Andreas's FORCEZ) pulls them down, and their size, colour and
// alpha follow keys over their life:
//  - SprayCloud (spraycan's ParticleEmitter): the "smoke" sprite, 70 a second at 1.5 m/s for 0.8 s, 0.1 m
//    growing to 2.5 m, green (17, 73, 21) fading to nothing, added onto what's behind.
//  - HoseFroosh (extinguisher's froosh): "collisionsmoke", 50 a second at 3 m/s for 1 s, pulled down up to
//    5 m/s², 0.1 m growing to 1.3 m by half its life and 1.8 m at the end, white fading out, blended.
//  - HoseChunks (extinguisher's chunks): "sphere", 20 a second at 3 m/s for 1 s, up to 20 degrees off the
//    line, pulled down 2 m/s², 5 cm, grey, added.
// San Andreas has no effect of its own where a fire goes out: a short puff of the froosh rising from it
// (Burst) stands in for one.

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
#include "smoke_model.h"

namespace AhShitRim::PuffModel
{
    using Pose::Vec3;
    using SmokeModel::Keys;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;
    inline constexpr int   kAges = 8;    // sizes and shades a sprite goes through
    inline constexpr int   kPerAge = 8;  // sprites of each drawn at once
    inline constexpr int   kBones = kAges * kPerAge;
    inline constexpr float kHidden = -100000.0f;
    inline constexpr float kSpinMin = 30.0f * 0.0174532925f;  // radians a second
    inline constexpr float kSpinMax = 60.0f * 0.0174532925f;

    static_assert(kBones <= static_cast<int>(Protocol::kBodyBoneMax));

    struct Prim
    {
        const char*  name;
        const char*  texture;   // in models/effectsPC.txd
        bool         additive;  // added onto what's behind (SRCBLEND 4, DSTBLEND 1), else blended
        float        rate;      // sprites a second
        float        life;      // seconds
        float        speed;     // metres a second
        float        angle;     // degrees off the line at most
        Keys         force;     // metres a second², down, over a sprite's life
        Keys         size;      // metres across, over a sprite's life
        Keys         alpha;     // 0..255, over a sprite's life
        std::uint8_t start[3];  // colour when let out
        std::uint8_t end[3];    // and at the end of its life
    };

    inline constexpr Prim kSprayCloud{ "SprayCloud", "smoke", true, 70.0f, 0.8f, 1.5f, 0.0f, { { 0.0f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.5f, 1.0f }, { 0.1f, 1.3f, 2.5f } }, { { 0.0f, 0.5f, 1.0f }, { 255.0f, 127.5f, 0.0f } }, { 17, 73, 21 }, { 0, 0, 0 } };
    inline constexpr Prim kHoseFroosh{ "HoseFroosh", "collisionsmoke", false, 50.0f, 1.0f, 3.0f, 1.0f, { { 0.0f, 0.5f, 1.0f }, { 0.0f, 2.5f, 5.0f } },
        { { 0.0f, 0.5f, 1.0f }, { 0.1f, 1.3f, 1.8f } }, { { 0.0f, 0.5f, 1.0f }, { 255.0f, 127.5f, 0.0f } }, { 255, 255, 255 }, { 255, 255, 255 } };
    inline constexpr Prim kHoseChunks{ "HoseChunks", "sphere", true, 20.0f, 1.0f, 3.0f, 20.0f, { { 0.0f, 0.5f, 1.0f }, { 2.0f, 2.0f, 2.0f } },
        { { 0.0f, 0.5f, 1.0f }, { 0.05f, 0.05f, 0.05f } }, { { 0.0f, 0.5f, 1.0f }, { 255.0f, 255.0f, 255.0f } }, { 128, 128, 128 }, { 128, 128, 128 } };

    // Where a fire goes out: the froosh's sprite rising slowly, a burst of kPutOutCount.
    inline constexpr Prim kPutOutPuff{ "PutOutPuff", "collisionsmoke", false, 1.0f, 1.2f, 1.0f, 35.0f, { { 0.0f, 0.5f, 1.0f }, { -1.0f, -1.0f, -1.0f } },
        { { 0.0f, 0.5f, 1.0f }, { 0.3f, 1.0f, 1.5f } }, { { 0.0f, 0.3f, 1.0f }, { 200.0f, 140.0f, 0.0f } }, { 255, 255, 255 }, { 255, 255, 255 } };
    inline constexpr int kPutOutCount = 6;

    // Everything alive at once fits the bones it can be drawn on.
    static_assert(kSprayCloud.rate * kSprayCloud.life <= kBones);
    static_assert(kHoseFroosh.rate * kHoseFroosh.life <= kBones);

    inline float BucketMiddle(int a_age) { return (static_cast<float>(a_age) + 0.5f) / kAges; }
    inline int   AgeBucket(float a_t) { return std::clamp(static_cast<int>(a_t * kAges), 0, kAges - 1); }

    // An age bucket's sprite: half its size in Skyrim units, its colour and alpha.
    inline float SizeOf(const Prim& a_prim, int a_age) { return 0.5f * a_prim.size.At(BucketMiddle(a_age)) * kMeter; }
    inline std::uint8_t AlphaOf(const Prim& a_prim, int a_age) { return static_cast<std::uint8_t>(std::lround(std::clamp(a_prim.alpha.At(BucketMiddle(a_age)), 0.0f, 255.0f))); }
    inline std::uint8_t ShadeOf(const Prim& a_prim, int a_age, int a_channel)
    {
        const float t = BucketMiddle(a_age);
        return static_cast<std::uint8_t>(std::lround(a_prim.start[a_channel] + (a_prim.end[a_channel] - a_prim.start[a_channel]) * t));
    }

    // a_prim's kBones sprites, one quad each in its bone's XZ plane facing -Y: bone b is of age bucket
    // b / kPerAge, each age bucket a mesh of its own colour and alpha. a_sprite is the prim's texture.
    inline SkinModel::ModelIn Build(const Prim& a_prim, const SkinModel::TextureIn& a_sprite)
    {
        SkinModel::ModelIn model;
        model.name = a_prim.name;
        model.kind = Protocol::kSkinModelVehicle;  // a vehicle's wide bounds: the sprites spread round the nozzle
        model.textures.push_back(a_sprite);
        for (int age = 0; age < kAges; ++age) {
            SkinModel::MeshIn mesh;
            mesh.texture = 0;
            mesh.flags = Protocol::kSkinMeshBlend | (a_prim.additive ? Protocol::kSkinMeshAdditive : 0u);
            for (int c = 0; c < 3; ++c) {
                mesh.color[c] = ShadeOf(a_prim, age, c);
            }
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

    // A unit vector up to a_degrees off a_axis (a unit vector), from two numbers in 0..1.
    inline Vec3 Spread(const Vec3& a_axis, float a_degrees, float a_u, float a_v)
    {
        const Vec3  helper = std::fabs(a_axis.z) < 0.9f ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        const Vec3  side = Body::Normalize(Body::Cross(a_axis, helper), { 1.0f, 0.0f, 0.0f });
        const Vec3  up = Body::Cross(side, a_axis);
        const float tilt = a_u * a_degrees * 0.0174532925f;
        const float around = a_v * 6.2831853f;
        const Vec3  off = Body::Add(Body::Scale(side, std::cos(around)), Body::Scale(up, std::sin(around)));
        return Body::Normalize(Body::Add(Body::Scale(a_axis, std::cos(tilt)), Body::Scale(off, std::sin(tilt))), a_axis);
    }

    // One prim's sprites in the world: let out along a nozzle's line while it sprays, or in a burst, moving
    // on their own from there and drawn until they are gone.
    class Cloud
    {
    public:
        struct Sprite
        {
            Vec3          at;
            Vec3          speed;  // units a second
            float         age = 0.0f;
            float         roll = 0.0f;
            float         spin = 0.0f;
        };

        explicit Cloud(const Prim& a_prim = kHoseFroosh) : _prim(&a_prim) {}

        // a_dt seconds on. While a_spraying, sprites come out at a_nozzle along a_dir (a unit vector) at the
        // prim's rate.
        void Step(float a_dt, bool a_spraying, const Vec3& a_nozzle = {}, const Vec3& a_dir = { 0.0f, 1.0f, 0.0f })
        {
            const float dt = std::clamp(a_dt, 0.0f, 0.25f);
            for (auto& sprite : _sprites) {
                sprite.age += dt;
                sprite.speed.z -= _prim->force.At(sprite.age / _prim->life) * kMeter * dt;
                sprite.at = Body::Add(sprite.at, Body::Scale(sprite.speed, dt));
                sprite.roll += sprite.spin * dt;
            }
            std::erase_if(_sprites, [&](const Sprite& a_sprite) { return a_sprite.age >= _prim->life; });
            if (!a_spraying) {
                _since = 1.0f;  // the first comes at once when it starts again
                return;
            }
            const float every = 1.0f / _prim->rate;
            _since = (std::min)(_since, every) + dt;
            while (_since >= every) {
                _since -= every;
                Let(a_nozzle, a_dir, _prim->speed, _prim->angle, _since);
            }
        }

        // a_count sprites at once from a_at along a_dir, a_speed metres a second, up to a_degrees off it.
        void Burst(const Vec3& a_at, const Vec3& a_dir, int a_count, float a_speed, float a_degrees)
        {
            for (int i = 0; i < a_count; ++i) {
                Let(a_at, a_dir, a_speed, a_degrees, 0.0f);
            }
        }

        bool Empty() const { return _sprites.empty(); }
        const std::vector<Sprite>& Sprites() const { return _sprites; }

        // Every bone relative to a_origin: each sprite on a free bone of its age's size, facing a_camera; the
        // rest hidden. The newest of an age go first when there are more than its bones.
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
            for (auto it = _sprites.rbegin(); it != _sprites.rend(); ++it) {
                const int age = AgeBucket(it->age / _prim->life);
                if (used[age] >= kPerAge) {
                    continue;
                }
                auto&            bone = a_out.bones[age * kPerAge + used[age]++];
                const Body::Quat q = SmokeModel::Facing(it->at, a_camera, it->roll);
                bone.rotation[0] = q.x;
                bone.rotation[1] = q.y;
                bone.rotation[2] = q.z;
                bone.rotation[3] = q.w;
                bone.translation[0] = it->at.x - a_origin.x;
                bone.translation[1] = it->at.y - a_origin.y;
                bone.translation[2] = it->at.z - a_origin.z;
            }
        }

        void Clear()
        {
            _sprites.clear();
            _since = 1.0f;
        }

    private:
        void Let(const Vec3& a_at, const Vec3& a_dir, float a_speed, float a_degrees, float a_age)
        {
            if (_sprites.size() >= 2 * static_cast<std::size_t>(kBones)) {
                return;
            }
            const std::uint32_t n = _count++;
            const Vec3          dir = Spread(a_dir, a_degrees, SmokeModel::Hash(n * 4u), SmokeModel::Hash(n * 4u + 1u));
            Sprite              sprite;
            sprite.speed = Body::Scale(dir, a_speed * kMeter);
            sprite.at = Body::Add(a_at, Body::Scale(sprite.speed, a_age));
            sprite.age = a_age;
            sprite.roll = SmokeModel::Hash(n * 4u + 2u) * 6.2831853f;
            sprite.spin = kSpinMin + (kSpinMax - kSpinMin) * SmokeModel::Hash(n * 4u + 3u);
            _sprites.push_back(sprite);
        }

        const Prim*         _prim;
        std::vector<Sprite> _sprites;
        float               _since = 1.0f;  // seconds since the last sprite
        std::uint32_t       _count = 0;
    };
}
