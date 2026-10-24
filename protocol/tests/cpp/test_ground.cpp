#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "../../cpp/collision.h"
#include "../../cpp/ground.h"

using namespace AhShitRim;
using namespace AhShitRim::Ground;

namespace
{
    Collision::Heightfield Flat(std::int32_t a_x, std::int32_t a_y, std::uint32_t a_cells, float a_z)
    {
        Collision::Heightfield f;
        f.Reset(a_x, a_y, a_cells);
        for (std::uint32_t k = 0; k < f.Samples(); k++) {
            f.heights[k] = a_z;
            f.hit[k] = 1;
        }
        return f;
    }

    Origin At(float a_x, float a_y, float a_z)
    {
        Origin o;
        const float anchor[3] = { a_x, a_y, a_z };
        o.Reset(anchor);
        return o;
    }

    Vec3 Unpack(const Mesh& a_mesh, std::uint16_t a_vertex)
    {
        return { a_mesh.vertices[a_vertex * 3] / kVertexScale, a_mesh.vertices[a_vertex * 3 + 1] / kVertexScale, a_mesh.vertices[a_vertex * 3 + 2] / kVertexScale };
    }
}

// ---- Floating origin ----

TEST_CASE("the handover anchor sits at San Andreas's middle, kHeight up")
{
    const Origin o = At(100000.0f, -50000.0f, 2000.0f);
    const Vec3   at = o.ToSanAndreas(100000.0, -50000.0, 2000.0);
    CHECK(at.x == doctest::Approx(0.0f));
    CHECK(at.y == doctest::Approx(0.0f));
    CHECK(at.z == doctest::Approx(kHeight));

    // 70 units east and 140 up is a metre east and two up.
    const Vec3 near = o.ToSanAndreas(100070.0, -50000.0, 2140.0);
    CHECK(near.x == doctest::Approx(1.0f).epsilon(0.001));
    CHECK(near.z == doctest::Approx(kHeight + 2.0f).epsilon(0.001));
}

TEST_CASE("San Andreas positions go back to the same Skyrim positions")
{
    const Origin o = At(-123456.0f, 98765.0f, -3000.0f);
    const Vec3   sa = o.ToSanAndreas(-120000.0, 99000.0, -2500.0);
    const auto   back = o.ToSkyrim(sa);
    CHECK(back[0] == doctest::Approx(-120000.0).epsilon(1e-6));
    CHECK(back[1] == doctest::Approx(99000.0).epsilon(1e-6));
    CHECK(back[2] == doctest::Approx(-2500.0).epsilon(1e-5));
}

TEST_CASE("the origin stays put near the middle and follows CJ far from it")
{
    Origin o = At(0.0f, 0.0f, 0.0f);
    Vec3   shift;
    CHECK_FALSE(o.Recenter({ 900.0f, 0.0f, kHeight + 200.0f }, shift));
    CHECK_FALSE(o.Recenter({ 0.0f, -999.0f, kHeight - 299.0f }, shift));

    // CJ 1.2 km east and 50 m up: the origin moves under him by whole metres.
    const Vec3 cj{ 1200.4f, 10.6f, kHeight + 50.2f };
    const auto skyrimBefore = o.ToSkyrim(cj);
    REQUIRE(o.Recenter(cj, shift));
    CHECK(shift.x == 1200.0f);
    CHECK(shift.y == 11.0f);
    CHECK(shift.z == 50.0f);
    const Vec3 moved{ cj.x - shift.x, cj.y - shift.y, cj.z - shift.z };
    CHECK(std::hypot(moved.x, moved.y) < 1.0f);
    CHECK(moved.z == doctest::Approx(kHeight + 0.2f).epsilon(0.001));

    // The shifted position is still the same place in Skyrim.
    const auto skyrimAfter = o.ToSkyrim(moved);
    for (int i = 0; i < 3; ++i) {
        CHECK(skyrimAfter[i] == doctest::Approx(skyrimBefore[i]).epsilon(1e-6));
    }
    CHECK_FALSE(o.Recenter(moved, shift));
}

TEST_CASE("a position or an anchor that isn't a number moves nothing")
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Origin      o = At(100.0f, 200.0f, 300.0f);
    Vec3        shift;
    CHECK_FALSE(o.Recenter({ nan, nan, nan }, shift));
    CHECK_FALSE(o.Recenter({ 0.0f, 0.0f, nan }, shift));
    CHECK_FALSE(o.Recenter({ std::numeric_limits<float>::infinity(), 0.0f, kHeight }, shift));
    CHECK(o.base[0] == 100.0);
    CHECK(o.base[2] == 300.0);
    const float broken[3] = { nan, 0.0f, 0.0f };
    CHECK_FALSE(o.Reset(broken));
    CHECK(o.base[0] == 100.0);
    CHECK(o.base[1] == 200.0);
}

TEST_CASE("falling or climbing far moves the origin too")
{
    Origin o = At(0.0f, 0.0f, 0.0f);
    Vec3   shift;
    REQUIRE(o.Recenter({ 0.0f, 0.0f, kHeight - 350.0f }, shift));
    CHECK(shift.z == -350.0f);
    REQUIRE(o.Recenter({ 0.0f, 0.0f, kHeight + 400.0f }, shift));
    CHECK(shift.z == 400.0f);
}

// ---- Chunk payloads ----

TEST_CASE("chunk keys come from Skyrim positions, flooring")
{
    CHECK(KeyOf(0.0, 0.0) == Key{ 0, 0 });
    CHECK(KeyOf(2048.0, -1.0) == Key{ 1, -1 });
    CHECK(KeyOf(-4097.0, 4095.0) == Key{ -3, 1 });
    CHECK(Distance({ 0, 0 }, { 3, -5 }) == 5);
}

TEST_CASE("a payload from the Skyrim side parses back")
{
    const auto payload = Collision::BuildChunkPayload(Flat(2, 3, 4, 1000.0f), 0x3C, 5);
    Chunk      chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    CHECK(chunk.header.chunkX == 2);
    CHECK(chunk.header.chunkY == 3);
    CHECK(chunk.header.generation == 5);
    CHECK(chunk.header.vertexCount == 25);
    CHECK(chunk.header.indexCount == 96);
}

TEST_CASE("broken payloads are refused")
{
    auto  payload = Collision::BuildChunkPayload(Flat(0, 0, 2, 0.0f), 0, 1);
    Chunk chunk;
    CHECK_FALSE(ParseChunk(payload.data(), 10, chunk));
    CHECK_FALSE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size() - 4), chunk));
    // An index past the vertices.
    auto broken = payload;
    reinterpret_cast<std::uint32_t*>(broken.data() + broken.size())[-1] = 999;
    CHECK_FALSE(ParseChunk(broken.data(), static_cast<std::uint32_t>(broken.size()), chunk));
}

TEST_CASE("a flat chunk becomes San Andreas triangles facing up, around the chunk's middle")
{
    // Chunk (1, 0), flat at the anchor's height.
    const Origin o = At(2048.0f, 0.0f, 500.0f);
    const auto   payload = Collision::BuildChunkPayload(Flat(1, 0, 4, 500.0f), 0, 1);
    Chunk        chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    const Mesh mesh = BuildMesh(chunk, o);

    CHECK(mesh.dropped == 0);
    CHECK(mesh.vertices.size() == 25 * 3);
    CHECK(mesh.triangles.size() == 96);
    const float half = Protocol::kCollisionChunkUnits / 2.0f * Pose::kMetersPerUnit;
    CHECK(mesh.position.x == doctest::Approx(half));
    CHECK(mesh.position.y == doctest::Approx(half));
    CHECK(mesh.position.z == doctest::Approx(kHeight));
    CHECK(mesh.min.x == doctest::Approx(-half).epsilon(0.01));
    CHECK(mesh.max.y == doctest::Approx(half).epsilon(0.01));
    CHECK(mesh.min.z == doctest::Approx(0.0f));
    CHECK(mesh.max.z == doctest::Approx(0.0f));

    // San Andreas's normal is cross(c - a, b - a): it must point up.
    for (std::size_t t = 0; t < mesh.triangles.size(); t += 3) {
        const Vec3 a = Unpack(mesh, mesh.triangles[t]), b = Unpack(mesh, mesh.triangles[t + 1]), c = Unpack(mesh, mesh.triangles[t + 2]);
        const Vec3 ca{ c.x - a.x, c.y - a.y, c.z - a.z }, ba{ b.x - a.x, b.y - a.y, b.z - a.z };
        CHECK(ca.x * ba.y - ca.y * ba.x > 0.0f);
    }
}

TEST_CASE("vertices keep about a centimetre of precision on a slope")
{
    auto f = Flat(0, 0, 32, 0.0f);
    for (std::uint32_t k = 0; k < f.Samples(); k++) {
        f.heights[k] = static_cast<float>(k % f.Side()) * 37.3f;  // 30 m over the chunk
    }
    const Origin o = At(0.0f, 0.0f, 0.0f);
    const auto   payload = Collision::BuildChunkPayload(f, 0, 1);
    Chunk        chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    const Mesh mesh = BuildMesh(chunk, o);
    CHECK(mesh.dropped == 0);
    CHECK(mesh.vertices.size() == 33 * 33 * 3);
    // Each column is one 37.3-unit step higher than the last.
    const float step = 37.3f * Pose::kMetersPerUnit;
    CHECK(mesh.max.z - mesh.min.z == doctest::Approx(32 * step).epsilon(0.001));
    for (std::uint16_t v = 0; v < mesh.vertices.size() / 3; ++v) {
        const float steps = (Unpack(mesh, v).z - mesh.min.z) / step;
        CHECK(std::abs(steps - std::round(steps)) * step < 0.01f);
    }
}

TEST_CASE("a triangle reaching past 256 m is cut and the parts in reach kept; flat ones are dropped and counted")
{
    Collision::Heightfield f;
    f.Reset(0, 0, 1);
    const std::vector<float> statics = {
        0, 0, 0, 100, 0, 0, 0, 100, 0,                // kept
        0, 0, 0, 100, 0, 30000, 0, 100, 0,           // a corner 430 m up the median: its lower part kept
        0, 0, 0, 0.1f, 0, 0, 0, 0.1f, 0,             // under 1/128 m across: flat once compressed
    };
    const auto payload = Collision::BuildChunkPayload(f, 0, 1, statics);
    Chunk      chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    const Mesh mesh = BuildMesh(chunk, At(0.0f, 0.0f, 0.0f));
    CHECK(mesh.triangles.size() > 6);  // the first, and parts of the second
    CHECK(mesh.dropped >= 1);         // the flat one (and slivers of the cut one's edge)
    CHECK(mesh.max.z > 200.0f);       // the second goes up to the reach
    CHECK(mesh.max.z <= kVertexReach + 0.01f);
}

TEST_CASE("a mountain's huge face over the chunk is kept where it is near it")
{
    // One triangle 1000 m across (Skyrim units 70000), its middle over chunk (0, 0): every corner out of reach.
    Collision::Heightfield f;
    f.Reset(0, 0, 1);
    const std::vector<float> statics = { -35000, -35000, 500, 35000, -35000, 500, 0, 35000, 500 };
    const auto payload = Collision::BuildChunkPayload(f, 0, 1, statics);
    Chunk      chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    const Mesh mesh = BuildMesh(chunk, At(0.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(mesh.triangles.empty());
    // The kept parts cover the chunk's square round its middle, all inside the reach.
    CHECK(mesh.min.x < -14.0f);
    CHECK(mesh.max.x > 14.0f);
    CHECK(mesh.min.y < -14.0f);
    CHECK(mesh.max.y > 14.0f);
    CHECK(mesh.max.x <= kVertexReach + 0.01f);
    CHECK(mesh.vertices.size() / 3 < kMaxMeshVertices);
}

// ---- Which chunks San Andreas keeps ----

TEST_CASE("chunks of the current handover are built, older ones dropped, newer ones kept for later")
{
    ChunkBook book(8);
    book.Begin(3);
    CHECK(book.Classify(3) == ChunkBook::Verdict::Current);
    CHECK(book.Classify(2) == ChunkBook::Verdict::Stale);
    CHECK(book.Classify(4) == ChunkBook::Verdict::Future);
}

TEST_CASE("a resend replaces a chunk in its own slot, a new chunk takes the lowest free one")
{
    ChunkBook book(3);
    book.Begin(1);
    bool replaced = true;
    CHECK(book.Place({ 0, 0 }, replaced) == 0);
    CHECK_FALSE(replaced);
    CHECK(book.Place({ 1, 0 }, replaced) == 1);
    CHECK(book.Place({ 0, 0 }, replaced) == 0);
    CHECK(replaced);
    CHECK(book.Place({ 2, 0 }, replaced) == 2);
    CHECK(book.Place({ 3, 0 }, replaced) == -1);  // full
    CHECK(book.Count() == 3);
}

TEST_CASE("far chunks are dropped and their slots reused")
{
    ChunkBook book(4);
    book.Begin(1);
    bool replaced = false;
    book.Place({ 0, 0 }, replaced);
    book.Place({ kKeepRadius, 0 }, replaced);
    book.Place({ kKeepRadius + 1, 0 }, replaced);
    book.Place({ -3, -kKeepRadius - 1 }, replaced);

    auto dropped = book.DropFar({ 0, 0 });
    std::ranges::sort(dropped);
    CHECK(dropped == std::vector<int>{ 2, 3 });
    CHECK(book.Has({ 0, 0 }));
    CHECK(book.Has({ kKeepRadius, 0 }));
    CHECK_FALSE(book.Has({ kKeepRadius + 1, 0 }));
    CHECK(book.Place({ 1, 1 }, replaced) == 2);
}

TEST_CASE("a new handover frees every slot")
{
    ChunkBook book(4);
    book.Begin(1);
    bool replaced = false;
    book.Place({ 0, 0 }, replaced);
    book.Place({ 1, 0 }, replaced);
    auto freed = book.Begin(2);
    std::ranges::sort(freed);
    CHECK(freed == std::vector<int>{ 0, 1 });
    CHECK(book.Count() == 0);
    CHECK(book.Generation() == 2);
    CHECK(book.Place({ 5, 5 }, replaced) == 0);
}

TEST_CASE("San Andreas keeps every chunk Skyrim still remembers")
{
    // Skyrim forgets chunks more than kForgetRadius from the player and from a point at most kMaxAheadChunks ahead.
    // Anything San Andreas drops (past kKeepRadius from CJ, who is where the player is) must be
    // forgotten by Skyrim, or it would never be sent again.
    CHECK(kKeepRadius > kForgetRadius + kMaxAheadChunks);
}

TEST_CASE("the point ahead goes 2 s out, at most as far as San Andreas keeps what Skyrim sampled there")
{
    using namespace AhShitRim::Ground;
    CHECK(kMaxAheadChunks >= 6);  // a Hydra's 87 m/s

    auto a = Ahead(700.0f, 0.0f);  // 10 m/s
    CHECK(a[0] == doctest::Approx(1400.0f));
    CHECK(a[1] == doctest::Approx(0.0f));
    a = Ahead(0.0f, -100000.0f);  // far too fast: capped
    CHECK(a[0] == doctest::Approx(0.0f));
    CHECK(a[1] == doctest::Approx(-6.0f * AhShitRim::Protocol::kCollisionChunkUnits));
    a = Ahead(0.0f, 0.0f);
    CHECK(a[0] == 0.0f);
}

// ---- The land under CJ ----

TEST_CASE("the land's height comes from the land samples alone, through the origin")
{
    // A slope rising 1 unit per unit east over chunk (0, 0), and a static slab 300 units over it.
    Collision::Heightfield field;
    field.Reset(0, 0, Protocol::kCollisionChunkCells);
    for (std::uint32_t k = 0; k < field.Samples(); k++) {
        field.heights[k] = 1000.0f + field.X(k % field.Side());
        field.hit[k] = 1;
    }
    const std::vector<float> slab = { 0, 0, 5000, 2048, 0, 5000, 0, 2048, 5000 };
    const auto               payload = Collision::BuildChunkPayload(field, 0x3C, 1, slab);
    Chunk                    chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    CHECK(chunk.header.landVertexCount == field.Samples());
    LandMap map;
    map.Set({ 0, 0 }, LandOf(chunk));
    const Origin origin = At(0.0f, 0.0f, 1000.0f);
    for (const float x : { 100.0f, 1000.0f, 1500.0f }) {
        const auto z = map.HeightAt(origin, origin.ToSanAndreas(x, 700.0, 0.0));
        REQUIRE(z);
        CHECK(*z == doctest::Approx(kHeight + x * Pose::kMetersPerUnit).epsilon(0.001));
    }
    CHECK_FALSE(map.HeightAt(origin, origin.ToSanAndreas(-100.0, 700.0, 0.0)));  // chunk (-1, 0): none
}

TEST_CASE("a missed land sample leaves no height on the triangles touching it")
{
    auto field = Flat(0, 0, Protocol::kCollisionChunkCells, 0.0f);
    field.hit[0] = 0;
    const auto payload = Collision::BuildChunkPayload(field, 0x3C, 1);
    Chunk      chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    LandMap map;
    map.Set({ 0, 0 }, LandOf(chunk));
    const Origin origin = At(0.0f, 0.0f, 0.0f);
    CHECK_FALSE(map.HeightAt(origin, origin.ToSanAndreas(10.0, 10.0, 0.0)));
    CHECK(map.HeightAt(origin, origin.ToSanAndreas(300.0, 300.0, 0.0)));
}

TEST_CASE("the land goes with the chunks: far ones and a new handover's")
{
    const auto payload = Collision::BuildChunkPayload(Flat(0, 0, Protocol::kCollisionChunkCells, 0.0f), 0x3C, 1);
    Chunk      chunk;
    REQUIRE(ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    LandMap map;
    map.Set({ 0, 0 }, LandOf(chunk));
    map.Set({ 20, 0 }, LandOf(chunk));
    map.DropFar({ 1, 0 });
    CHECK(map.Count() == 1);
    map.Clear();
    CHECK(map.Count() == 0);
}
