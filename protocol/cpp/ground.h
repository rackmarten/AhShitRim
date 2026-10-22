// Skyrim's ground in San Andreas, the game-free half: the floating origin, reading a
// CollisionChunk payload into San Andreas's collision format, and which chunks San Andreas keeps.
// Header-only and free of game types, so the protocol tests cover it; the San Andreas ASI builds
// collision objects from it (sa/src/ground.cpp).
//
// Floating origin: San Andreas's world is about 6 km across, Skyrim's much larger. Skyrim position
// p (game units) sits in San Andreas at (p - base) * kMetersPerUnit + (0, 0, kHeight) metres. A
// handover puts base at the Skyrim player's feet, so CJ starts at (0, 0, kHeight), high above
// where San Andreas's (empty) map and its sea would be. Once CJ is far from there, base moves by
// whole metres and every San Andreas position moves back by the same shift.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "pose.h"

namespace AhShitRim::Ground
{
    using Pose::Vec3;

    // ---- Floating origin ----

    inline constexpr float kHeight = 500.0f;            // San Andreas height of base, metres
    inline constexpr float kRecenterDistance = 1000.0f;  // CJ this far from San Andreas's (0, 0) moves the origin
    inline constexpr float kRecenterHeight = 300.0f;     // ... or this far above or below kHeight

    struct Origin
    {
        double base[3]{};  // Skyrim world units at San Andreas (0, 0, kHeight)

        // A handover's anchor. One that isn't a number is refused and base stays as it was.
        bool Reset(const float (&a_anchor)[3])
        {
            if (!std::isfinite(a_anchor[0]) || !std::isfinite(a_anchor[1]) || !std::isfinite(a_anchor[2])) {
                return false;
            }
            for (int i = 0; i < 3; ++i) {
                base[i] = a_anchor[i];
            }
            return true;
        }

        // A Skyrim world position in San Andreas, metres.
        Vec3 ToSanAndreas(double a_x, double a_y, double a_z) const
        {
            return { static_cast<float>((a_x - base[0]) * Pose::kMetersPerUnit),
                static_cast<float>((a_y - base[1]) * Pose::kMetersPerUnit),
                static_cast<float>((a_z - base[2]) * Pose::kMetersPerUnit + kHeight) };
        }

        // A San Andreas position in Skyrim world units.
        std::array<double, 3> ToSkyrim(const Vec3& a_sa) const
        {
            return { base[0] + a_sa.x * static_cast<double>(Pose::kUnitsPerMeter),
                base[1] + a_sa.y * static_cast<double>(Pose::kUnitsPerMeter),
                base[2] + (a_sa.z - kHeight) * static_cast<double>(Pose::kUnitsPerMeter) };
        }

        // With CJ at a_cj (San Andreas metres) far from the middle, moves base under him and returns
        // true with the shift (whole metres) to subtract from every San Andreas position: CJ, the
        // collision chunks, the camera, remembered spots. Skyrim positions don't change. A position
        // that isn't a number moves nothing: shifted by NaN, base and everything else stayed NaN for good.
        bool Recenter(const Vec3& a_cj, Vec3& a_shift)
        {
            if (!std::isfinite(a_cj.x) || !std::isfinite(a_cj.y) || !std::isfinite(a_cj.z)) {
                return false;
            }
            if (std::hypot(a_cj.x, a_cj.y) < kRecenterDistance && std::abs(a_cj.z - kHeight) < kRecenterHeight) {
                return false;
            }
            a_shift = { std::round(a_cj.x), std::round(a_cj.y), std::round(a_cj.z - kHeight) };
            base[0] += a_shift.x * static_cast<double>(Pose::kUnitsPerMeter);
            base[1] += a_shift.y * static_cast<double>(Pose::kUnitsPerMeter);
            base[2] += a_shift.z * static_cast<double>(Pose::kUnitsPerMeter);
            return true;
        }
    };

    // ---- Chunk payloads ----

    struct Key
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        auto operator<=>(const Key&) const = default;
    };

    // Chunk key of a Skyrim world position.
    inline Key KeyOf(double a_x, double a_y)
    {
        const double size = Protocol::kCollisionChunkUnits;
        return { static_cast<std::int32_t>(std::floor(a_x / size)), static_cast<std::int32_t>(std::floor(a_y / size)) };
    }

    // Chebyshev distance in chunks.
    inline int Distance(const Key& a_a, const Key& a_b)
    {
        return (std::max)(std::abs(a_a.x - a_b.x), std::abs(a_a.y - a_b.y));
    }

    // A checked view into a CollisionChunk payload.
    struct Chunk
    {
        Protocol::CollisionChunkHeader header{};
        const float*                   vertices = nullptr;  // vertexCount * 3, Skyrim units relative to header.origin
        const std::uint32_t*           indices = nullptr;   // indexCount
    };

    // False when the payload is short, its counts disagree with its size, or an index points past
    // the vertices.
    inline bool ParseChunk(const std::uint8_t* a_data, std::uint32_t a_size, Chunk& a_out)
    {
        if (a_size < sizeof(Protocol::CollisionChunkHeader)) {
            return false;
        }
        std::memcpy(&a_out.header, a_data, sizeof(a_out.header));
        const auto& h = a_out.header;
        if (h.vertexCount > 65535 || h.indexCount % 3 != 0 || h.indexCount > 3u * 1000000u) {
            return false;
        }
        const std::uint64_t vertexBytes = std::uint64_t{ h.vertexCount } * 3 * sizeof(float);
        const std::uint64_t indexBytes = std::uint64_t{ h.indexCount } * sizeof(std::uint32_t);
        if (sizeof(h) + vertexBytes + indexBytes != a_size) {
            return false;
        }
        a_out.vertices = reinterpret_cast<const float*>(a_data + sizeof(h));
        a_out.indices = reinterpret_cast<const std::uint32_t*>(a_data + sizeof(h) + vertexBytes);
        for (std::uint32_t i = 0; i < h.indexCount; ++i) {
            if (a_out.indices[i] >= h.vertexCount) {
                return false;
            }
        }
        return true;
    }

    // San Andreas's collision vertices are int16 at 1/128 m (gta-reversed's CompressedVector), so a
    // chunk's vertices must lie within 256 m of where its collision object stands.
    inline constexpr float kVertexScale = 128.0f;
    inline constexpr float kVertexReach = 255.0f;

    // A chunk in San Andreas's collision format.
    struct Mesh
    {
        Vec3                       position;   // where the collision object stands, San Andreas metres
        std::vector<std::int16_t>  vertices;   // x, y, z per vertex, 1/128 m relative to position
        std::vector<std::uint16_t> triangles;  // a, b, c per triangle
        Vec3                       min, max;   // bounding box relative to position, metres
        std::uint32_t              dropped = 0;  // triangles too far out (past kVertexReach) or flat after compression
    };

    // San Andreas faces a triangle where cross(c - a, b - a) points (gta-reversed's
    // CColTrianglePlane::Set), Skyrim where cross(b - a, c - a) does, so b and c swap.
    inline Mesh BuildMesh(const Chunk& a_chunk, const Origin& a_origin)
    {
        const auto& h = a_chunk.header;
        Mesh        mesh;

        std::vector<Vec3>  sa(h.vertexCount);
        std::vector<float> heights(h.vertexCount);
        for (std::uint32_t v = 0; v < h.vertexCount; ++v) {
            const float* p = a_chunk.vertices + v * 3;
            sa[v] = a_origin.ToSanAndreas(double{ h.origin[0] } + p[0], double{ h.origin[1] } + p[1], double{ h.origin[2] } + p[2]);
            heights[v] = sa[v].z;
        }
        // The chunk square's middle, at its median height: one tall static far off doesn't pull
        // the ground out of reach.
        const Vec3 middle = a_origin.ToSanAndreas((h.chunkX + 0.5) * Protocol::kCollisionChunkUnits, (h.chunkY + 0.5) * Protocol::kCollisionChunkUnits, 0.0);
        float      height = middle.z;
        if (!heights.empty()) {
            std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
            height = heights[heights.size() / 2];
        }
        mesh.position = { middle.x, middle.y, height };

        // Vertices relative to the position, compressed; out of reach ones are marked.
        std::vector<std::array<std::int16_t, 3>> packed(h.vertexCount);
        std::vector<std::uint8_t>                inReach(h.vertexCount);
        for (std::uint32_t v = 0; v < h.vertexCount; ++v) {
            const float r[3] = { sa[v].x - mesh.position.x, sa[v].y - mesh.position.y, sa[v].z - mesh.position.z };
            inReach[v] = std::abs(r[0]) <= kVertexReach && std::abs(r[1]) <= kVertexReach && std::abs(r[2]) <= kVertexReach;
            for (int i = 0; i < 3; ++i) {
                packed[v][i] = static_cast<std::int16_t>(std::lround(std::clamp(r[i], -kVertexReach, kVertexReach) * kVertexScale));
            }
        }

        // Only the vertices kept triangles use go in, renumbered.
        std::vector<std::uint32_t> remap(h.vertexCount, UINT32_MAX);
        bool                       any = false;
        for (std::uint32_t t = 0; t + 3 <= h.indexCount; t += 3) {
            const std::uint32_t a = a_chunk.indices[t], b = a_chunk.indices[t + 2], c = a_chunk.indices[t + 1];  // b and c swapped
            if (!inReach[a] || !inReach[b] || !inReach[c]) {
                mesh.dropped++;
                continue;
            }
            // Flat after compression: San Andreas would normalise a zero normal.
            const std::int64_t e1[3] = { packed[b][0] - packed[a][0], packed[b][1] - packed[a][1], packed[b][2] - packed[a][2] };
            const std::int64_t e2[3] = { packed[c][0] - packed[a][0], packed[c][1] - packed[a][1], packed[c][2] - packed[a][2] };
            if (e1[1] * e2[2] - e1[2] * e2[1] == 0 && e1[2] * e2[0] - e1[0] * e2[2] == 0 && e1[0] * e2[1] - e1[1] * e2[0] == 0) {
                mesh.dropped++;
                continue;
            }
            for (const std::uint32_t v : { a, b, c }) {
                if (remap[v] == UINT32_MAX) {
                    remap[v] = static_cast<std::uint32_t>(mesh.vertices.size() / 3);
                    mesh.vertices.insert(mesh.vertices.end(), packed[v].begin(), packed[v].end());
                    const Vec3 p{ packed[v][0] / kVertexScale, packed[v][1] / kVertexScale, packed[v][2] / kVertexScale };
                    if (!any) {
                        mesh.min = mesh.max = p;
                        any = true;
                    }
                    mesh.min = { (std::min)(mesh.min.x, p.x), (std::min)(mesh.min.y, p.y), (std::min)(mesh.min.z, p.z) };
                    mesh.max = { (std::max)(mesh.max.x, p.x), (std::max)(mesh.max.y, p.y), (std::max)(mesh.max.z, p.z) };
                }
                mesh.triangles.push_back(static_cast<std::uint16_t>(remap[v]));
            }
        }
        return mesh;
    }

    // ---- Which chunks San Andreas keeps ----

    // Skyrim forgets chunks more than 3 from the player and from a point up to 3 chunks ahead of
    // it (a fast car's), and sends them again when they come back into range. San Andreas drops them
    // only past kKeepRadius from CJ, so a chunk it dropped is always one Skyrim forgot and will resend.
    inline constexpr int kKeepRadius = 7;

    // The chunks San Andreas has built for the current handover, each in one of a fixed number of
    // slots (San Andreas model ids, sa/src/ground.cpp).
    class ChunkBook
    {
    public:
        enum class Verdict
        {
            Stale,    // an older handover's: dropped
            Current,  // build it
            Future,   // a handover San Andreas hasn't read yet: keep it until it has
        };

        explicit ChunkBook(int a_slots) :
            _free(a_slots, 1)
        {}

        std::uint32_t Generation() const { return _generation; }

        // A new handover: every chunk goes. Returns their slots, to free.
        std::vector<int> Begin(std::uint32_t a_generation)
        {
            _generation = a_generation;
            std::vector<int> slots;
            for (const auto& [key, slot] : _chunks) {
                slots.push_back(slot);
                _free[slot] = 1;
            }
            _chunks.clear();
            return slots;
        }

        Verdict Classify(std::uint32_t a_generation) const
        {
            return a_generation == _generation ? Verdict::Current : (a_generation < _generation ? Verdict::Stale : Verdict::Future);
        }

        // The slot a chunk goes in: the one it had (a resend replaces it, a_replaced), or the lowest
        // free one; -1 when every slot is taken.
        int Place(const Key& a_key, bool& a_replaced)
        {
            if (const auto it = _chunks.find(a_key); it != _chunks.end()) {
                a_replaced = true;
                return it->second;
            }
            a_replaced = false;
            const auto it = std::find(_free.begin(), _free.end(), std::uint8_t{ 1 });
            if (it == _free.end()) {
                return -1;
            }
            const int slot = static_cast<int>(it - _free.begin());
            _free[slot] = 0;
            _chunks[a_key] = slot;
            return slot;
        }

        // Forgets the chunks more than a_radius from a_center. Returns their slots, to free.
        std::vector<int> DropFar(const Key& a_center, int a_radius = kKeepRadius)
        {
            std::vector<int> slots;
            for (auto it = _chunks.begin(); it != _chunks.end();) {
                if (Distance(it->first, a_center) > a_radius) {
                    slots.push_back(it->second);
                    _free[it->second] = 1;
                    it = _chunks.erase(it);
                } else {
                    ++it;
                }
            }
            return slots;
        }

        bool        Has(const Key& a_key) const { return _chunks.contains(a_key); }
        std::size_t Count() const { return _chunks.size(); }

    private:
        std::uint32_t         _generation = 0;
        std::map<Key, int>    _chunks;
        std::vector<std::uint8_t> _free;  // 1: slot free
    };
}
