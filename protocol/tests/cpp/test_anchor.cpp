#include <doctest/doctest.h>

#include "../../cpp/anchor.h"
#include "../../cpp/collision.h"
#include "../../cpp/ground.h"
#include "../../cpp/pose.h"

#include <cstring>
#include <vector>

using namespace AhShitRim;

TEST_CASE("walking on keeps the anchor, doors and teleports move it, loading waits")
{
    using Anchor::Classify;
    using Anchor::Move;
    CHECK(Classify({ true, false, false, false, false, 20.0f }) == Move::None);
    CHECK(Classify({ true, true, false, false, false, 20.0f }) == Move::None);  // into the next exterior cell
    CHECK(Classify({ false, false, false, false, false, 0.0f }) == Move::Loading);
    CHECK(Classify({ false, true, true, false, false, 99999.0f }) == Move::Loading);  // no cell yet beats everything
    CHECK(Classify({ true, true, true, false, false, 10.0f }) == Move::LoadDoor);   // into Breezehome
    CHECK(Classify({ true, true, false, true, false, 10.0f }) == Move::LoadDoor);   // back out
    CHECK(Classify({ true, true, true, true, false, 10.0f }) == Move::LoadDoor);    // Bleak Falls Barrow, one interior to the next
    CHECK(Classify({ true, true, false, false, true, 600.0f }) == Move::LoadDoor);   // Whiterun's gate: Tamriel to the city, a few metres
    CHECK(Classify({ true, false, false, false, true, 0.0f }) == Move::None);        // no new cell, nothing to compare
    CHECK(Classify({ true, true, false, false, false, 60000.0f }) == Move::Teleport);  // fast travel, Whiterun to Riverwood
    CHECK(Classify({ true, false, true, true, false, 1500.0f }) == Move::Teleport);   // a load door within one dungeon cell
    CHECK(Classify({ true, false, false, false, false, Anchor::kTeleportUnits + 1.0f }) == Move::Teleport);  // a script, same cell
    CHECK(Classify({ true, false, false, false, false, Anchor::kTeleportUnits }) == Move::None);
    CHECK(Classify({ true, false, false, false, false, 40.0f }) == Move::None);  // Skyrim easing the player out of a wall
    // Skyrim's PositionPlayerEvent: Whiterun's gate out to Tamriel, 272 units and a worldspace the
    // cells didn't show apart in game, re-anchors on Skyrim's word alone.
    CHECK(Classify({ true, false, false, false, false, 272.0f, true }) == Move::Positioned);
    CHECK(Classify({ true, true, false, false, false, 0.0f, true }) == Move::Positioned);
    CHECK(Classify({ false, true, false, false, false, 0.0f, true }) == Move::Loading);  // still loading: wait for a cell
    CHECK(Anchor::Reanchors(Move::Positioned));
    CHECK(Anchor::Reanchors(Move::LoadDoor));
    CHECK(Anchor::Reanchors(Move::Teleport));
    CHECK_FALSE(Anchor::Reanchors(Move::None));
    CHECK_FALSE(Anchor::Reanchors(Move::Loading));
}

namespace
{
    // A flat 3 x 3 metre floor at Skyrim height a_z round (a_x, a_y), as one chunk payload.
    std::vector<std::uint8_t> Floor(double a_x, double a_y, float a_z, std::uint32_t a_generation)
    {
        Protocol::CollisionChunkHeader h{};
        h.chunkX = Collision::ChunkIndex(static_cast<float>(a_x));
        h.chunkY = Collision::ChunkIndex(static_cast<float>(a_y));
        h.generation = a_generation;
        h.origin[0] = static_cast<float>(a_x);
        h.origin[1] = static_cast<float>(a_y);
        h.origin[2] = a_z;
        const float         v[] = { -100, -100, 0, 100, -100, 0, 100, 100, 0, -100, 100, 0 };
        const std::uint32_t idx[] = { 0, 1, 2, 0, 2, 3 };
        h.vertexCount = 4;
        h.indexCount = 6;
        std::vector<std::uint8_t> out(sizeof(h) + sizeof(v) + sizeof(idx));
        std::memcpy(out.data(), &h, sizeof(h));
        std::memcpy(out.data() + sizeof(h), v, sizeof(v));
        std::memcpy(out.data() + sizeof(h) + sizeof(v), idx, sizeof(idx));
        return out;
    }
}

TEST_CASE("a re-anchor puts CJ at the destination under a fresh origin and drops the old ground")
{
    Ground::Origin    origin;
    Ground::ChunkBook book(9);

    // Handover 1 outside Whiterun's gate.
    const float gate[3] = { 19000.0f, -9000.0f, -2400.0f };
    origin.Reset(gate);
    CHECK(book.Begin(1).empty());
    bool replaced = false;
    CHECK(book.Place(Ground::KeyOf(gate[0], gate[1]), replaced) == 0);
    CHECK(book.Place(Ground::KeyOf(gate[0] + 2048, gate[1]), replaced) == 1);

    // CJ walked 1.2 km and the origin moved under him once.
    Ground::Vec3 shift;
    CHECK(origin.Recenter({ 1200.0f, 0.0f, 500.0f }, shift));

    // Into Breezehome: an interior, its own coordinates, far below the gate.
    const float home[3] = { -64.0f, 300.0f, -12800.0f };
    origin.Reset(home);
    const auto freed = book.Begin(2);
    CHECK(freed.size() == 2);        // every chunk of the gate goes, whatever origin moves came before
    CHECK(book.Count() == 0);
    CHECK(origin.base[0] == home[0]);  // the origin starts over exactly at the anchor
    CHECK(origin.base[2] == home[2]);

    // CJ stands at San Andreas (0, 0, kHeight) there, and the floor under him builds right under it.
    const auto feet = origin.ToSanAndreas(home[0], home[1], home[2]);
    CHECK(feet.x == doctest::Approx(0.0f));
    CHECK(feet.y == doctest::Approx(0.0f));
    CHECK(feet.z == doctest::Approx(Ground::kHeight));
    const auto     payload = Floor(home[0], home[1], home[2], 2);
    Ground::Chunk  chunk;
    REQUIRE(Ground::ParseChunk(payload.data(), static_cast<std::uint32_t>(payload.size()), chunk));
    CHECK(book.Classify(chunk.header.generation) == Ground::ChunkBook::Verdict::Current);
    const auto mesh = Ground::BuildMesh(chunk, origin);
    CHECK(mesh.dropped == 0);
    CHECK(mesh.position.z + mesh.min.z == doctest::Approx(Ground::kHeight).epsilon(1e-3));
    CHECK(mesh.triangles.size() == 6);

    // The gate's chunks still in the ring arrive after: stale. The next re-anchor's wait.
    CHECK(book.Classify(1) == Ground::ChunkBook::Verdict::Stale);
    CHECK(book.Classify(3) == Ground::ChunkBook::Verdict::Future);
}

TEST_CASE("after a re-anchor CJ's pose is an offset from the new anchor")
{
    // San Andreas put CJ's feet at the re-anchor's spawn and he took two steps north.
    Pose::SanAndreasPose sa;
    sa.spawn = { 0.0f, 0.0f, Ground::kHeight + 0.05f };
    sa.position = { 0.0f, 1.4f, Ground::kHeight + 0.05f };
    sa.forward = { 0.0f, 1.0f, 0.0f };
    sa.view = { 0.0f, 1.0f, 0.0f };
    const auto pose = Pose::ToPlayerState(sa, 10, 2);
    CHECK(pose.handover == 2);
    CHECK(pose.offset[0] == doctest::Approx(0.0f));
    CHECK(pose.offset[1] == doctest::Approx(1.4f * Pose::kUnitsPerMeter));
    CHECK(pose.offset[2] == doctest::Approx(0.0f));

    // Skyrim puts the player at the new anchor plus that offset: where the origin says CJ is.
    const float    anchor[3] = { -64.0f, 300.0f, -12800.0f };
    Ground::Origin origin;
    origin.Reset(anchor);
    const auto at = origin.ToSkyrim({ sa.position.x, sa.position.y, sa.position.z - 0.05f });
    CHECK(at[0] == doctest::Approx(anchor[0] + pose.offset[0]));
    CHECK(at[1] == doctest::Approx(anchor[1] + pose.offset[1]));
    CHECK(at[2] == doctest::Approx(anchor[2] + pose.offset[2]));
}
