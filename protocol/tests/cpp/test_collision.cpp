#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "../../cpp/collision.h"

using namespace AhShitRim;
using namespace AhShitRim::Collision;

// Ported from PerkeleRim's tests, without its road triangles.

namespace
{
    struct Parsed
    {
        Protocol::CollisionChunkHeader header{};
        std::vector<float>             vertices;
        std::vector<std::uint32_t>     indices;
    };

    Parsed Parse(const std::vector<std::uint8_t>& a_payload)
    {
        Parsed p;
        REQUIRE(a_payload.size() >= sizeof(p.header));
        std::memcpy(&p.header, a_payload.data(), sizeof(p.header));
        const std::size_t vertexBytes = p.header.vertexCount * 3 * sizeof(float);
        const std::size_t indexBytes = p.header.indexCount * sizeof(std::uint32_t);
        REQUIRE(a_payload.size() == sizeof(p.header) + vertexBytes + indexBytes);
        p.vertices.resize(p.header.vertexCount * 3);
        p.indices.resize(p.header.indexCount);
        if (vertexBytes) {
            std::memcpy(p.vertices.data(), a_payload.data() + sizeof(p.header), vertexBytes);
        }
        if (indexBytes) {
            std::memcpy(p.indices.data(), a_payload.data() + sizeof(p.header) + vertexBytes, indexBytes);
        }
        return p;
    }

    // Z of cross(b - a, c - a) for triangle t: positive = counterclockwise seen from above.
    float NormalZ(const Parsed& a_p, std::size_t a_t)
    {
        const float* a = &a_p.vertices[a_p.indices[a_t * 3] * 3];
        const float* b = &a_p.vertices[a_p.indices[a_t * 3 + 1] * 3];
        const float* c = &a_p.vertices[a_p.indices[a_t * 3 + 2] * 3];
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    }

    Heightfield Flat(std::int32_t a_x, std::int32_t a_y, std::uint32_t a_cells, float a_z)
    {
        Heightfield f;
        f.Reset(a_x, a_y, a_cells);
        for (std::uint32_t k = 0; k < f.Samples(); k++) {
            f.heights[k] = a_z;
            f.hit[k] = 1;
        }
        return f;
    }
}

TEST_CASE("chunk index floors toward negative infinity")
{
    CHECK(ChunkIndex(0.0f) == 0);
    CHECK(ChunkIndex(2047.0f) == 0);
    CHECK(ChunkIndex(2048.0f) == 1);
    CHECK(ChunkIndex(-1.0f) == -1);
    CHECK(ChunkIndex(-2048.0f) == -1);
    CHECK(ChunkIndex(-2049.0f) == -2);
}

TEST_CASE("heightfield samples cover the chunk edge to edge")
{
    Heightfield f;
    f.Reset(-2, 3, 32);
    CHECK(f.Samples() == 33 * 33);
    CHECK(f.X(0) == doctest::Approx(-4096.0f));
    CHECK(f.X(32) == doctest::Approx(-2048.0f));
    CHECK(f.Y(0) == doctest::Approx(6144.0f));
    CHECK(f.Step() == doctest::Approx(64.0f));
}

TEST_CASE("a fully hit heightfield becomes a closed grid wound counterclockwise")
{
    const auto f = Flat(1, -1, 4, 300.0f);
    const auto p = Parse(BuildChunkPayload(f, 0x3C, 7));
    CHECK(p.header.chunkX == 1);
    CHECK(p.header.chunkY == -1);
    CHECK(p.header.worldspace == 0x3C);
    CHECK(p.header.generation == 7);
    CHECK(p.header.origin[0] == doctest::Approx(2048.0f));
    CHECK(p.header.origin[1] == doctest::Approx(-2048.0f));
    CHECK(p.header.origin[2] == doctest::Approx(300.0f));
    CHECK(p.header.vertexCount == 25);
    CHECK(p.header.indexCount == 4 * 4 * 2 * 3);
    for (std::size_t t = 0; t < p.header.indexCount / 3; t++) {
        CHECK(NormalZ(p, t) > 0.0f);
    }
}

TEST_CASE("a missed sample drops only the triangles touching it")
{
    auto f = Flat(0, 0, 4, 0.0f);
    f.hit[2 * 5 + 2] = 0;  // the middle sample
    const auto p = Parse(BuildChunkPayload(f, 0, 1));
    CHECK(p.header.vertexCount == 24);
    CHECK(p.header.indexCount == (32 - 6) * 3);  // six triangles share the middle sample
}

TEST_CASE("an empty heightfield still makes a valid payload")
{
    Heightfield f;
    f.Reset(0, 0, 4);
    const auto p = Parse(BuildChunkPayload(f, 0, 1));
    CHECK(p.header.vertexCount == 0);
    CHECK(p.header.indexCount == 0);
}

TEST_CASE("a full-size chunk fits the ring many times over")
{
    const auto payload = BuildChunkPayload(Flat(0, 0, 32, 0.0f), 0, 1);
    CHECK(payload.size() * 20 < Protocol::kRingToSanAndreasCapacity);
}

TEST_CASE("static triangles follow the heightfield, sharing identical corners")
{
    const auto         f = Flat(0, 0, 1, 100.0f);
    std::vector<float> statics = {
        10, 10, 50, 20, 10, 50, 10, 20, 50,  // lower than the land: the origin follows it
        20, 10, 50, 20, 20, 50, 10, 20, 50,
    };
    const auto p = Parse(BuildChunkPayload(f, 0, 1, statics));
    CHECK(p.header.origin[2] == doctest::Approx(50.0f));
    CHECK(p.header.vertexCount == 4 + 4);
    CHECK(p.header.indexCount == 2 * 3 + 2 * 3);
    const float* first = &p.vertices[p.indices[6] * 3];
    CHECK(first[0] == doctest::Approx(10.0f));
    CHECK(first[2] == doctest::Approx(0.0f));
    CHECK(p.vertices[2] == doctest::Approx(50.0f));  // the land, relative to the new origin
}

TEST_CASE("a chunk with no terrain (an interior) is just its statics")
{
    Heightfield f;
    f.Reset(0, 0, 32);
    const std::vector<float> statics = { 0, 0, 0, 100, 0, 0, 0, 100, 0 };
    const auto               p = Parse(BuildChunkPayload(f, 0, 1, statics));
    CHECK(p.header.vertexCount == 3);
    CHECK(p.header.indexCount == 3);
}

TEST_CASE("statics past the vertex limit are dropped and counted")
{
    Heightfield f;
    f.Reset(0, 0, 1);
    std::vector<float> statics;
    for (std::uint32_t t = 0; t < kMaxChunkVertices / 3 + 10; t++) {
        const float x = static_cast<float>(t);
        statics.insert(statics.end(), { x, 0, 0, x, 1, 0, x, 0, 1 });
    }
    std::uint32_t dropped = 0;
    const auto    p = Parse(BuildChunkPayload(f, 0, 1, statics, &dropped));
    CHECK(dropped > 0);
    CHECK(p.header.vertexCount <= kMaxChunkVertices);
    CHECK(p.header.indexCount / 3 + dropped == kMaxChunkVertices / 3 + 10);
}

TEST_CASE("small heightfield holes are filled from their neighbours, wide misses stay open")
{
    auto                f = Flat(0, 0, 32, 100.0f);
    const std::uint32_t side = f.Side();
    for (std::uint32_t j = 10; j < 12; j++) {
        for (std::uint32_t i = 10; i < 12; i++) {
            f.hit[j * side + i] = 0;
            f.heights[j * side + i] = 0.0f;
        }
    }
    for (std::uint32_t k = 21 * side; k < f.Samples(); k++) {
        f.hit[k] = 0;
    }
    const auto filled = FillMisses(f);
    CHECK(filled >= 4);
    for (std::uint32_t j = 10; j < 12; j++) {
        for (std::uint32_t i = 10; i < 12; i++) {
            CHECK(f.hit[j * side + i] == 1);
            CHECK(f.heights[j * side + i] == doctest::Approx(100.0f));
        }
    }
    CHECK(f.hit[32 * side + 16] == 0);
    CHECK(f.hit[21 * side + 16] == 1);
}
