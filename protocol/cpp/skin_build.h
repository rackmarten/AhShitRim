// Turns a RenderWare skinned geometry (as the ASI reads it from CJ's clump) into the meshes of a
// SkinModel: one mesh per material, Skyrim units, Skyrim's triangle winding, normalized weights.
// Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "body.h"
#include "skin_model.h"

namespace AhShitRim::SkinBuild
{
    struct Material
    {
        std::int32_t texture = -1;  // index into the model's textures
        std::uint32_t flags = 0;    // Protocol::kSkinMesh*
        std::uint8_t color[4] = { 255, 255, 255, 255 };
    };

    // Pointers into San Andreas's geometry; normals, uvs and the skin may be null.
    struct Geometry
    {
        std::uint32_t        vertexCount = 0;
        const float*         positions = nullptr;    // xyz per vertex, metres
        const float*         normals = nullptr;      // xyz per vertex
        const float*         uvs = nullptr;          // uv per vertex
        const std::uint32_t* boneIndices = nullptr;  // RpSkin: four bone indices packed per vertex, lowest byte first
        const float*         weights = nullptr;      // RpSkin: four weights per vertex
        std::uint32_t        boneCount = 256;        // weights on bones past this are dropped
        std::uint32_t        triangleCount = 0;
        const std::uint16_t* triangles = nullptr;  // RpTriangle: three vertex indices and the material index each
        bool                 keepWinding = false;  // v19: never turn a triangle round (a model of back-to-back faces sharing
                                                   // one normal, the parachute's canopy: turning one of each pair made both face
                                                   // the same way, and drawn one-sided both were culled from below)
    };

    struct Stats
    {
        std::uint32_t flipped = 0;   // triangles turned round to face out the way Skyrim expects
        std::uint32_t dropped = 0;   // triangles with a bad vertex or material index
        std::uint32_t unweighted = 0;  // vertices without any bone weight (bone 0 takes them)
        std::uint32_t foreignBones = 0;  // weights dropped for naming a bone past Geometry::boneCount
    };

    // Smooth normals from the triangles, for a geometry that has none.
    inline std::vector<Body::Vec3> FaceNormals(const Geometry& a_geometry)
    {
        std::vector<Body::Vec3> normals(a_geometry.vertexCount);
        for (std::uint32_t t = 0; t < a_geometry.triangleCount; t++) {
            const std::uint16_t* tri = a_geometry.triangles + t * 4;
            if (tri[0] >= a_geometry.vertexCount || tri[1] >= a_geometry.vertexCount || tri[2] >= a_geometry.vertexCount) {
                continue;
            }
            auto at = [&](std::uint16_t i) { return Body::Vec3{ a_geometry.positions[i * 3], a_geometry.positions[i * 3 + 1], a_geometry.positions[i * 3 + 2] }; };
            const auto n = Body::Cross(Body::Sub(at(tri[1]), at(tri[0])), Body::Sub(at(tri[2]), at(tri[0])));
            for (int k = 0; k < 3; k++) {
                normals[tri[k]] = Body::Add(normals[tri[k]], n);
            }
        }
        for (auto& n : normals) {
            n = Body::Normalize(n);
        }
        return normals;
    }

    // One mesh per material that has triangles, in material order.
    inline std::vector<SkinModel::MeshIn> Meshes(const Geometry& a_geometry, const std::vector<Material>& a_materials, Stats* a_stats = nullptr)
    {
        Stats stats;
        const bool              computed = a_geometry.normals == nullptr;
        std::vector<Body::Vec3> faceNormals = computed ? FaceNormals(a_geometry) : std::vector<Body::Vec3>{};
        auto                    normal = [&](std::uint32_t i) {
            return computed ? faceNormals[i] : Body::Vec3{ a_geometry.normals[i * 3], a_geometry.normals[i * 3 + 1], a_geometry.normals[i * 3 + 2] };
        };
        auto position = [&](std::uint32_t i) { return Body::Vec3{ a_geometry.positions[i * 3], a_geometry.positions[i * 3 + 1], a_geometry.positions[i * 3 + 2] }; };

        std::vector<SkinModel::MeshIn> meshes;
        for (std::size_t m = 0; m < a_materials.size(); m++) {
            SkinModel::MeshIn mesh;
            mesh.texture = a_materials[m].texture;
            mesh.flags = a_materials[m].flags;
            std::memcpy(mesh.color, a_materials[m].color, sizeof(mesh.color));
            std::vector<std::int32_t> remap(a_geometry.vertexCount, -1);
            auto                      vertex = [&](std::uint32_t i) {
                if (remap[i] >= 0) {
                    return static_cast<std::uint16_t>(remap[i]);
                }
                Protocol::SkinVertex v{};
                const auto           p = Body::Scale(position(i), Pose::kUnitsPerMeter);
                const auto           n = Body::Normalize(normal(i));
                v.position[0] = p.x;
                v.position[1] = p.y;
                v.position[2] = p.z;
                v.normal[0] = n.x;
                v.normal[1] = n.y;
                v.normal[2] = n.z;
                if (a_geometry.uvs) {
                    v.uv[0] = a_geometry.uvs[i * 2];
                    v.uv[1] = a_geometry.uvs[i * 2 + 1];
                }
                float total = 0.0f;
                if (a_geometry.boneIndices && a_geometry.weights) {
                    for (int k = 0; k < 4; k++) {
                        const float w = a_geometry.weights[i * 4 + k];
                        v.bones[k] = static_cast<std::uint8_t>((a_geometry.boneIndices[i] >> (8 * k)) & 0xFF);
                        v.weights[k] = w > 0.0f && w < 1e6f ? w : 0.0f;  // also drops a NaN
                        if (v.weights[k] > 0.0f && v.bones[k] >= a_geometry.boneCount) {
                            v.weights[k] = 0.0f;
                            v.bones[k] = 0;
                            stats.foreignBones++;
                        }
                        total += v.weights[k];
                    }
                }
                if (total > 0.0f) {
                    for (float& w : v.weights) {
                        w /= total;
                    }
                } else {
                    v.bones[0] = 0;
                    v.weights[0] = 1.0f;
                    stats.unweighted++;
                }
                remap[i] = static_cast<std::int32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                return static_cast<std::uint16_t>(remap[i]);
            };

            for (std::uint32_t t = 0; t < a_geometry.triangleCount; t++) {
                const std::uint16_t* tri = a_geometry.triangles + t * 4;
                if (tri[3] != m) {
                    continue;
                }
                if (tri[0] >= a_geometry.vertexCount || tri[1] >= a_geometry.vertexCount || tri[2] >= a_geometry.vertexCount) {
                    stats.dropped++;
                    continue;
                }
                if (mesh.vertices.size() + 3 > Protocol::kModelMeshVertexMax) {
                    stats.dropped++;
                    continue;
                }
                std::uint32_t corner[3] = { tri[0], tri[1], tri[2] };
                // Skyrim faces a triangle the way cross(b - a, c - a) points; turn it round when that's
                // against its vertices' normals.
                const auto face = Body::Cross(Body::Sub(position(corner[1]), position(corner[0])), Body::Sub(position(corner[2]), position(corner[0])));
                const auto around = Body::Add(Body::Add(normal(corner[0]), normal(corner[1])), normal(corner[2]));
                if (!a_geometry.keepWinding && Body::Dot(face, around) < 0.0f) {
                    std::swap(corner[1], corner[2]);
                    stats.flipped++;
                }
                for (auto c : corner) {
                    mesh.indices.push_back(vertex(c));
                }
            }
            if (!mesh.indices.empty()) {
                meshes.push_back(std::move(mesh));
            }
        }
        for (std::uint32_t t = 0; t < a_geometry.triangleCount; t++) {
            if (a_geometry.triangles[t * 4 + 3] >= a_materials.size()) {
                stats.dropped++;
            }
        }
        if (a_stats) {
            *a_stats = stats;
        }
        return meshes;
    }
}
