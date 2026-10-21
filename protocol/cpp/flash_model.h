// The muzzle flash Skyrim draws when CJ fires (protocol v12), made here rather than read from San
// Andreas (whose flash is a textured sprite of its own gun model): three crossed quads with a
// star-shaped glow, alpha blended, one bone that sits at the muzzle. Header-only and free of game
// types, so the protocol tests cover it; the SKSE plugin hands the blob to its ModelView.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "skin_model.h"

namespace AhShitRim::FlashModel
{
    inline constexpr std::uint32_t kTextureSize = 64;
    inline constexpr float         kHalfSize = 16.0f;  // Skyrim units, about 23 cm: the flash is 46 cm across

    // The glow: bright white-yellow in the middle fading to orange and clear at the edge, with a cross of spikes.
    inline std::vector<std::uint8_t> Texture()
    {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kTextureSize) * kTextureSize * 4);
        for (std::uint32_t y = 0; y < kTextureSize; ++y) {
            for (std::uint32_t x = 0; x < kTextureSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float v = (static_cast<float>(y) + 0.5f) / kTextureSize * 2.0f - 1.0f;
                const float r = std::sqrt(u * u + v * v);
                const float core = std::pow(std::clamp(1.0f - r * 1.7f, 0.0f, 1.0f), 1.4f);
                const float spikes = std::clamp(1.0f - std::abs(u) * std::abs(v) * 18.0f, 0.0f, 1.0f) * std::clamp(1.0f - r, 0.0f, 1.0f) * 0.9f;
                const float alpha = std::clamp(core + spikes, 0.0f, 1.0f);
                const float heat = std::clamp(1.0f - r, 0.0f, 1.0f);  // white in the middle, orange out
                auto*       p = &rgba[(static_cast<std::size_t>(y) * kTextureSize + x) * 4];
                p[0] = 255;
                p[1] = static_cast<std::uint8_t>(150.0f + 105.0f * heat);
                p[2] = static_cast<std::uint8_t>(40.0f + 190.0f * heat * heat);
                p[3] = static_cast<std::uint8_t>(alpha * 255.0f + 0.5f);
            }
        }
        return rgba;
    }

    // Three quads through the bone, in the planes XY, XZ and YZ, so the flash shows from every side.
    inline SkinModel::ModelIn Build()
    {
        SkinModel::ModelIn model;
        model.name = "MuzzleFlash";
        model.kind = Protocol::kSkinModelWeapon;
        model.boneIds = { 0 };
        model.textures.push_back({ kTextureSize, kTextureSize, Texture() });

        SkinModel::MeshIn mesh;
        mesh.texture = 0;
        mesh.flags = Protocol::kSkinMeshBlend;
        const float h = kHalfSize;
        // Each plane: the two axes it spans and the normal.
        struct Plane
        {
            float a[3], b[3], n[3];
        };
        const Plane planes[3] = {
            { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
            { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
            { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
        };
        for (const auto& plane : planes) {
            const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
            const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            for (const auto& corner : corners) {
                Protocol::SkinVertex v{};
                for (int k = 0; k < 3; ++k) {
                    v.position[k] = (plane.a[k] * corner[0] + plane.b[k] * corner[1]) * h;
                    v.normal[k] = plane.n[k];
                }
                v.uv[0] = corner[0] * 0.5f + 0.5f;
                v.uv[1] = corner[1] * 0.5f + 0.5f;
                v.weights[0] = 1.0f;
                mesh.vertices.push_back(v);
            }
            for (const std::uint16_t i : { 0, 1, 2, 0, 2, 3 }) {
                mesh.indices.push_back(static_cast<std::uint16_t>(base + i));
            }
        }
        model.meshes.push_back(std::move(mesh));
        return model;
    }
}
