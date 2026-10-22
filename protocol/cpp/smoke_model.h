// A damaged vehicle's black smoke (protocol v18), made here: Skyrim's own smoke can't be turned black, so
// the dark stage before the fire is drawn as soft black puffs, each three crossed quads (as the muzzle
// flash) on a bone of its own, rising from the engine and growing as they fade. Header-only and free of
// game types, so the protocol tests cover it; the SKSE plugin hands the blob to its ModelView and poses the
// puffs every frame.
//
// A bone can't scale, so the model holds kAges sizes of puff, kPerAge of each, every size in a mesh of its
// own with its own alpha: a puff is drawn on a bone of the size and fade its age has reached. Unused bones
// go far below the ground.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "pose.h"
#include "skin_model.h"

namespace AhShitRim::SmokeModel
{
    using Pose::Vec3;

    inline constexpr float         kMeter = Pose::kUnitsPerMeter;
    inline constexpr std::uint32_t kTextureSize = 64;
    inline constexpr int           kAges = 8;     // sizes a puff goes through
    inline constexpr int           kPerAge = 6;   // puffs of each size drawn at once
    inline constexpr int           kBones = kAges * kPerAge;
    inline constexpr float         kLife = 3.0f;  // seconds a puff lasts
    inline constexpr float         kEvery = 0.07f;               // seconds between puffs
    inline constexpr float         kRise = 1.6f * kMeter;         // units a second a puff rises
    inline constexpr float         kSpread = 0.35f * kMeter;      // and drifts sideways at most
    inline constexpr float         kSizeFrom = 0.35f * kMeter;    // half size of a new puff
    inline constexpr float         kSizeTo = 1.5f * kMeter;       // and of one about to go
    inline constexpr float         kHidden = -100000.0f;          // where an unused bone goes
    inline constexpr std::uint8_t  kShade = 18;                    // the smoke's colour, near black

    static_assert(kBones <= static_cast<int>(Protocol::kBodyBoneMax));
    static_assert(kLife / kEvery <= kBones);

    // A puff's half size and alpha (0..255) at age bucket a_age: growing, and fading in its last half.
    inline float SizeOf(int a_age) { return kSizeFrom + (kSizeTo - kSizeFrom) * static_cast<float>(a_age) / (kAges - 1); }

    inline std::uint8_t AlphaOf(int a_age)
    {
        const float t = static_cast<float>(a_age) / (kAges - 1);
        const float alpha = t < 0.4f ? 1.0f : 1.0f - (t - 0.4f) / 0.6f * 0.85f;
        return static_cast<std::uint8_t>(std::lround(235.0f * alpha));
    }

    inline int AgeBucket(float a_age) { return std::clamp(static_cast<int>(a_age / kLife * kAges), 0, kAges - 1); }

    // A small hash for the puffs' look and drift, the same every run.
    inline float Hash(std::uint32_t a_n)
    {
        a_n = (a_n ^ 61u) ^ (a_n >> 16);
        a_n *= 9u;
        a_n ^= a_n >> 4;
        a_n *= 0x27d4eb2du;
        a_n ^= a_n >> 15;
        return static_cast<float>(a_n & 0xFFFFu) / 65535.0f;
    }

    // A soft, lumpy round puff, black, its alpha falling off to nothing at the edge.
    inline std::vector<std::uint8_t> Texture()
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kTextureSize) * kTextureSize * 4);
        for (std::uint32_t y = 0; y < kTextureSize; ++y) {
            for (std::uint32_t x = 0; x < kTextureSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float v = (static_cast<float>(y) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float r = std::sqrt(u * u + v * v);
                // Lumps: the edge pushed in and out with the angle.
                const float angle = std::atan2(v, u);
                const float edge = 0.82f + 0.08f * std::sin(angle * 5.0f) + 0.05f * std::sin(angle * 9.0f + 1.3f);
                const float soft = std::clamp(1.0f - r / edge, 0.0f, 1.0f);
                const float grain = 0.85f + 0.15f * Hash(y * kTextureSize + x);
                auto*       p = &rgba[(static_cast<std::size_t>(y) * kTextureSize + x) * 4];
                p[0] = p[1] = p[2] = kShade;
                p[3] = static_cast<std::uint8_t>(std::lround(std::pow(soft, 0.8f) * grain * 255.0f));
            }
        }
        return rgba;
    }

    // kBones puffs: bone b is of age bucket b / kPerAge; each age bucket is a blended mesh of its own alpha.
    inline SkinModel::ModelIn Build()
    {
        SkinModel::ModelIn model;
        model.name = "BlackSmoke";
        model.kind = Protocol::kSkinModelVehicle;  // a vehicle's bounds: the trail rises well above its origin
        model.textures.push_back({ kTextureSize, kTextureSize, Texture() });
        struct Plane
        {
            float a[3], b[3], n[3];
        };
        const Plane planes[3] = {
            { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
            { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
            { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
        };
        for (int age = 0; age < kAges; ++age) {
            SkinModel::MeshIn mesh;
            mesh.texture = 0;
            mesh.flags = Protocol::kSkinMeshBlend;
            mesh.color[3] = AlphaOf(age);
            const float h = SizeOf(age);
            for (int k = 0; k < kPerAge; ++k) {
                const auto bone = static_cast<std::uint32_t>(age * kPerAge + k);
                model.boneIds.push_back(bone);
                for (const auto& plane : planes) {
                    const auto  base = static_cast<std::uint16_t>(mesh.vertices.size());
                    const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                    for (const auto& corner : corners) {
                        Protocol::SkinVertex v{};
                        for (int c = 0; c < 3; ++c) {
                            v.position[c] = (plane.a[c] * corner[0] + plane.b[c] * corner[1]) * h;
                            v.normal[c] = plane.n[c];
                        }
                        v.uv[0] = corner[0] * 0.5f + 0.5f;
                        v.uv[1] = corner[1] * 0.5f + 0.5f;
                        v.bones[0] = static_cast<std::uint8_t>(bone);
                        v.weights[0] = 1.0f;
                        mesh.vertices.push_back(v);
                    }
                    for (const std::uint16_t i : { 0, 1, 2, 0, 2, 3 }) {
                        mesh.indices.push_back(static_cast<std::uint16_t>(base + i));
                    }
                }
            }
            model.meshes.push_back(std::move(mesh));
        }
        return model;
    }

    // One vehicle's trail of black smoke: puffs let out at the engine while it smokes black, each rising and
    // drifting where it was let out (so a moving vehicle leaves a trail), and drawn until it is gone.
    class Trail
    {
    public:
        struct Puff
        {
            Vec3          at;
            Vec3          drift;  // units a second
            float         age = 0.0f;
            std::uint32_t n = 0;
        };

        // a_dt seconds on; a_emitting: puffs come out at a_engine (Skyrim world units).
        void Step(float a_dt, bool a_emitting, const Vec3& a_engine)
        {
            const float dt = std::clamp(a_dt, 0.0f, 0.25f);
            for (auto& puff : _puffs) {
                puff.age += dt;
                puff.at = { puff.at.x + puff.drift.x * dt, puff.at.y + puff.drift.y * dt, puff.at.z + puff.drift.z * dt };
            }
            std::erase_if(_puffs, [](const Puff& a_puff) { return a_puff.age >= kLife; });
            if (!a_emitting) {
                _since = kEvery;  // the first puff comes at once when it starts again
                return;
            }
            _since += dt;
            while (_since >= kEvery && _puffs.size() < static_cast<std::size_t>(kBones)) {
                _since -= kEvery;
                const std::uint32_t n = _count++;
                const float         angle = Hash(n * 2u) * 6.2831853f;
                const float         side = kSpread * Hash(n * 2u + 1u);
                _puffs.push_back({ a_engine, { std::cos(angle) * side, std::sin(angle) * side, kRise }, _since, n });
            }
            _since = (std::min)(_since, kEvery);
        }

        bool Empty() const { return _puffs.empty(); }
        const std::vector<Puff>& Puffs() const { return _puffs; }

        // Every bone's place relative to a_origin: each puff on a free bone of its age's size, the rest hidden.
        void Pose(const Vec3& a_origin, Protocol::BodyState& a_out) const
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
            for (const auto& puff : _puffs) {
                const int age = AgeBucket(puff.age);
                if (used[age] >= kPerAge) {
                    continue;
                }
                auto& bone = a_out.bones[age * kPerAge + used[age]++];
                // Each puff turned its own way about the vertical, so the quads don't line up.
                const float half = Hash(puff.n * 7u + 3u) * 3.1415927f * 0.5f;
                bone.rotation[2] = std::sin(half);
                bone.rotation[3] = std::cos(half);
                bone.translation[0] = puff.at.x - a_origin.x;
                bone.translation[1] = puff.at.y - a_origin.y;
                bone.translation[2] = puff.at.z - a_origin.z;
            }
        }

        void Clear()
        {
            _puffs.clear();
            _since = kEvery;
        }

    private:
        std::vector<Puff> _puffs;
        float             _since = kEvery;
        std::uint32_t     _count = 0;
    };
}
