// Collision chunk geometry, Skyrim side: the chunk grid and turning a sampled terrain heightfield
// plus the statics' own collision triangles into a CollisionChunk payload. Used by the SKSE
// sampler, tested standalone. Ported from PerkeleRim (protocol/cpp/collision.h), without its road
// triangles. The San Andreas side reads the payload with Ground::ParseChunk (ground.h).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Collision
{
    // Chunk index of a Skyrim world coordinate (X or Y).
    inline std::int32_t ChunkIndex(float a_coord)
    {
        return static_cast<std::int32_t>(std::floor(a_coord / static_cast<float>(Protocol::kCollisionChunkUnits)));
    }

    // Heights sampled on a (cells + 1) x (cells + 1) grid covering one chunk, edges included, so
    // neighbouring chunks share their border samples. Sample (i, j) sits at
    // x = chunkX * size + i * step, y = chunkY * size + j * step and is stored at j * (cells + 1) + i.
    struct Heightfield
    {
        std::int32_t              chunkX = 0;
        std::int32_t              chunkY = 0;
        std::uint32_t             cells = 0;
        std::vector<float>        heights;  // Skyrim Z of the surface
        std::vector<std::uint8_t> hit;      // 0: nothing found there, the height is meaningless

        void Reset(std::int32_t a_chunkX, std::int32_t a_chunkY, std::uint32_t a_cells)
        {
            chunkX = a_chunkX;
            chunkY = a_chunkY;
            cells = a_cells;
            heights.assign(Samples(), 0.0f);
            hit.assign(Samples(), 0);
        }

        std::uint32_t Side() const { return cells + 1; }
        std::uint32_t Samples() const { return Side() * Side(); }
        float         Step() const { return static_cast<float>(Protocol::kCollisionChunkUnits) / static_cast<float>(cells); }
        float         X(std::uint32_t a_i) const { return static_cast<float>(chunkX) * Protocol::kCollisionChunkUnits + a_i * Step(); }
        float         Y(std::uint32_t a_j) const { return static_cast<float>(chunkY) * Protocol::kCollisionChunkUnits + a_j * Step(); }

        std::uint32_t Hits() const
        {
            return static_cast<std::uint32_t>(std::count(hit.begin(), hit.end(), std::uint8_t{ 1 }));
        }
    };

    // Closes small holes in a heightfield: a missed sample with at least three hit neighbours (of
    // eight) takes their mean height, a_passes times over, so holes a few samples across close
    // from their edges while wide misses (no land there at all) stay open. Returns how many
    // samples it filled.
    inline std::uint32_t FillMisses(Heightfield& a_field, int a_passes = 3)
    {
        const int     side = static_cast<int>(a_field.Side());
        std::uint32_t filled = 0;
        for (int pass = 0; pass < a_passes; pass++) {
            std::vector<std::pair<std::uint32_t, float>> fills;
            for (int j = 0; j < side; j++) {
                for (int i = 0; i < side; i++) {
                    const auto k = static_cast<std::uint32_t>(j * side + i);
                    if (a_field.hit[k]) {
                        continue;
                    }
                    float sum = 0.0f;
                    int   count = 0;
                    for (int dj = -1; dj <= 1; dj++) {
                        for (int di = -1; di <= 1; di++) {
                            const int ni = i + di, nj = j + dj;
                            if ((di || dj) && ni >= 0 && nj >= 0 && ni < side && nj < side && a_field.hit[nj * side + ni]) {
                                sum += a_field.heights[nj * side + ni];
                                count++;
                            }
                        }
                    }
                    if (count >= 3) {
                        fills.emplace_back(k, sum / static_cast<float>(count));
                    }
                }
            }
            if (fills.empty()) {
                break;
            }
            for (const auto& [k, z] : fills) {
                a_field.heights[k] = z;
                a_field.hit[k] = 1;
            }
            filled += static_cast<std::uint32_t>(fills.size());
        }
        return filled;
    }

    // San Andreas's collision triangles index vertices with 16 bits.
    constexpr std::uint32_t kMaxChunkVertices = 65000;

    // CollisionChunk payload for a heightfield and a triangle soup. The heightfield gives a vertex
    // per hit sample, two triangles per grid cell, and a triangle only where all three corners
    // were hit, so misses leave holes instead of spikes. The statics are 9 floats per triangle
    // (three Skyrim world positions, counterclockwise seen from outside), appended with
    // identical vertices shared; triangles that would go past kMaxChunkVertices are left out and
    // counted in a_dropped. The origin is the chunk's corner at the lowest height, which keeps the
    // relative vertices small.
    inline std::vector<std::uint8_t> BuildChunkPayload(const Heightfield& a_field, std::uint32_t a_worldspace, std::uint32_t a_generation,
        const std::vector<float>& a_statics = {}, std::uint32_t* a_dropped = nullptr)
    {
        const std::uint32_t side = a_field.Side();
        float               minZ = 0.0f;
        bool                any = false;
        for (std::uint32_t k = 0; k < a_field.Samples(); k++) {
            if (a_field.hit[k] && (!any || a_field.heights[k] < minZ)) {
                minZ = a_field.heights[k];
                any = true;
            }
        }
        for (std::size_t v = 2; v < a_statics.size(); v += 3) {
            if (!any || a_statics[v] < minZ) {
                minZ = a_statics[v];
                any = true;
            }
        }

        Protocol::CollisionChunkHeader header{};
        header.chunkX = a_field.chunkX;
        header.chunkY = a_field.chunkY;
        header.worldspace = a_worldspace;
        header.generation = a_generation;
        header.origin[0] = a_field.X(0);
        header.origin[1] = a_field.Y(0);
        header.origin[2] = minZ;

        std::vector<float>         vertices;
        std::vector<std::uint32_t> remap(a_field.Samples(), UINT32_MAX);
        for (std::uint32_t j = 0; j < side; j++) {
            for (std::uint32_t i = 0; i < side; i++) {
                const std::uint32_t k = j * side + i;
                if (!a_field.hit[k]) {
                    continue;
                }
                remap[k] = static_cast<std::uint32_t>(vertices.size() / 3);
                vertices.push_back(i * a_field.Step());
                vertices.push_back(j * a_field.Step());
                vertices.push_back(a_field.heights[k] - minZ);
            }
        }

        // a = (i, j), b = (i + 1, j) east, c = (i, j + 1) north, d = (i + 1, j + 1).
        // a-b-c and b-d-c both wind counterclockwise seen from above.
        std::vector<std::uint32_t> indices;
        const auto                 triangle = [&](std::uint32_t a_a, std::uint32_t a_b, std::uint32_t a_c) {
            if (remap[a_a] != UINT32_MAX && remap[a_b] != UINT32_MAX && remap[a_c] != UINT32_MAX) {
                indices.push_back(remap[a_a]);
                indices.push_back(remap[a_b]);
                indices.push_back(remap[a_c]);
            }
        };
        for (std::uint32_t j = 0; j < a_field.cells; j++) {
            for (std::uint32_t i = 0; i < a_field.cells; i++) {
                const std::uint32_t a = j * side + i, b = a + 1, c = a + side, d = c + 1;
                triangle(a, b, c);
                triangle(b, d, c);
            }
        }

        std::map<std::array<float, 3>, std::uint32_t> shared;
        std::uint32_t                                 dropped = 0;
        for (std::size_t t = 0; t + 9 <= a_statics.size(); t += 9) {
            if (vertices.size() / 3 + 3 > kMaxChunkVertices) {
                dropped++;
                continue;
            }
            for (int c = 0; c < 3; c++) {
                const std::array<float, 3> p{ a_statics[t + c * 3] - header.origin[0], a_statics[t + c * 3 + 1] - header.origin[1], a_statics[t + c * 3 + 2] - minZ };
                const auto [it, fresh] = shared.try_emplace(p, static_cast<std::uint32_t>(vertices.size() / 3));
                if (fresh) {
                    vertices.insert(vertices.end(), p.begin(), p.end());
                }
                indices.push_back(it->second);
            }
        }
        if (a_dropped) {
            *a_dropped = dropped;
        }

        header.vertexCount = static_cast<std::uint32_t>(vertices.size() / 3);
        header.indexCount = static_cast<std::uint32_t>(indices.size());

        std::vector<std::uint8_t> payload(sizeof(header) + vertices.size() * sizeof(float) + indices.size() * sizeof(std::uint32_t));
        std::uint8_t*             out = payload.data();
        std::memcpy(out, &header, sizeof(header));
        out += sizeof(header);
        if (!vertices.empty()) {
            std::memcpy(out, vertices.data(), vertices.size() * sizeof(float));
            out += vertices.size() * sizeof(float);
        }
        if (!indices.empty()) {
            std::memcpy(out, indices.data(), indices.size() * sizeof(std::uint32_t));
        }
        return payload;
    }
}
