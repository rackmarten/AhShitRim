#include <cmath>
#include <ostream>
#include <set>

#include <doctest/doctest.h>

#include "../../cpp/parked.h"
#include "../../cpp/vehicle_kinds.h"

using namespace AhShitRim;
using namespace AhShitRim::Parked;

namespace
{
    const Spot& Find(std::uint16_t a_id)
    {
        const auto* spot = SpotById(a_id);
        REQUIRE(spot != nullptr);
        return *spot;
    }

    int Count(std::uint16_t a_model)
    {
        int n = 0;
        for (const auto& spot : kSpots) {
            n += spot.model == a_model;
        }
        return n;
    }
}

TEST_CASE("the parked vehicles' spot table")
{
    std::set<std::uint16_t> ids;
    std::set<Hold>          holds, stables;
    for (const auto& spot : kSpots) {
        CHECK(ids.insert(spot.id).second);  // ids are unique
        CHECK(std::isfinite(spot.at.x + spot.at.y + spot.at.z + spot.heading));
        CHECK(!spot.name.empty());
        CHECK(spot.world == World::Tamriel);  // the cities' own worldspaces have no room for one
        // A model San Andreas can make here: no boat, train, trailer or remote, no jetpack.
        const auto kind = VehicleKinds::KindOf(spot.model);
        CHECK(VehicleKinds::TraitsOf(kind).spawnable);
        CHECK_FALSE(VehicleKinds::IsGear(spot.model));
        holds.insert(spot.hold);
        if (spot.model == kSanchez) {
            stables.insert(spot.hold);
        }
        // No two spots on top of each other.
        for (const auto& other : kSpots) {
            if (&other != &spot) {
                CHECK(std::hypot(other.at.x - spot.at.x, other.at.y - spot.at.y) > 5.0f * kMeter);
            }
        }
    }
    CHECK(holds.size() == static_cast<std::size_t>(Pickups::kHolds));  // every hold has one
    // A Sanchez at the five hold capitals' stables.
    CHECK(Count(kSanchez) == 5);
    CHECK(stables == std::set<Hold>{ Hold::Whiterun, Hold::Haafingar, Hold::Eastmarch, Hold::Rift, Hold::Reach });
    CHECK(Find(1).name == "the Whiterun stables");
    // A BMX in every village.
    CHECK(Count(kBmx) >= 12);
    bool riverwood = false;
    for (const auto& spot : kSpots) {
        riverwood |= spot.model == kBmx && spot.name == "Riverwood";
    }
    CHECK(riverwood);
    // About ten themed ones, the rest.
    const int themed = static_cast<int>(std::size(kSpots)) - Count(kSanchez) - Count(kBmx);
    CHECK(themed >= 10);
    CHECK(Count(kRhino) == 1);
    CHECK(SpotById(9999) == nullptr);
}

TEST_CASE("a spot is only near CJ in its own world")
{
    const auto& stables = Find(1);
    const Vec3  near{ stables.at.x + 30.0f * kMeter, stables.at.y + 40.0f * kMeter, stables.at.z + 500.0f };
    CHECK(DistanceTo(stables, Protocol::kWorldTamriel, near) == doctest::Approx(50.0f).epsilon(0.001));  // across: the height doesn't count
    CHECK(std::isinf(DistanceTo(stables, Protocol::kWorldNone, near)));                                 // an interior at the same coordinates
    CHECK(std::isinf(DistanceTo(stables, Protocol::kWorldWhiterun, near)));
    CHECK(Pickups::WorldByteOf(0x3C, false) == Protocol::kWorldTamriel);
    CHECK(Pickups::WorldByteOf(0x1A26F, false) == Protocol::kWorldWhiterun);
    CHECK(Pickups::WorldByteOf(0x3C, true) == Protocol::kWorldNone);
    CHECK(Pickups::WorldByteOf(0x16BB4, false) == Protocol::kWorldNone);  // another city's worldspace
}

TEST_CASE("Skyrim's world in San Andreas, through the handover")
{
    const Vec3 anchor{ 17000.0f, -12000.0f, -4700.0f };
    const Vec3 spawnFeet{ 0.0f, 0.0f, 498.0f };
    const Vec3 spot{ 17000.0f + 700.0f, -12000.0f - 1400.0f, -4700.0f + 70.0f };
    const auto sa = ToSanAndreas(spot, anchor, spawnFeet);
    CHECK(sa.x == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(sa.y == doctest::Approx(-20.0f).epsilon(0.001));
    CHECK(sa.z == doctest::Approx(499.0f).epsilon(0.001));
    const auto back = ToSkyrim(sa, anchor, spawnFeet);
    CHECK(back.x == doctest::Approx(spot.x).epsilon(0.0001));
    CHECK(back.y == doctest::Approx(spot.y).epsilon(0.0001));
    CHECK(back.z == doctest::Approx(spot.z).epsilon(0.0001));
}

TEST_CASE("its spots: the spot itself first, then rings round it, all facing its way")
{
    const Vec3 at{ 10.0f, 20.0f, 500.0f };
    const auto spots = Candidates(at, 1.0f);
    REQUIRE(spots.size() == static_cast<std::size_t>(1 + kAroundRings * kAroundSteps));
    CHECK(spots[0].center.x == at.x);
    CHECK(spots[0].center.y == at.y);
    for (const auto& spot : spots) {
        CHECK(spot.heading == 1.0f);
        CHECK(Vehicle::HeadingOf(spot.forward) == doctest::Approx(1.0f).epsilon(0.0001));
        CHECK(spot.center.z == at.z);
    }
    // Nearer rings first, at most kAroundRings * kAroundStep out.
    float last = 0.0f;
    for (const auto& spot : spots) {
        const float r = std::hypot(spot.center.x - at.x, spot.center.y - at.y);
        CHECK(r >= last - 0.001f);
        CHECK(r <= kAroundRings * kAroundStep + 0.001f);
        last = r;
    }
}

TEST_CASE("put out within 100 m, dropped past 160 m when nobody uses it")
{
    const auto& spot = Find(20);
    Board       board;
    CHECK(board.StatusOf(spot.id) == Status::Waiting);
    CHECK_FALSE(board.Tick(spot, 0.0, 101.0f));
    CHECK_FALSE(board.Tick(spot, 0.0, INFINITY));  // another world
    CHECK(board.Tick(spot, 0.0, 99.0f));
    // San Andreas couldn't (the ground isn't in): tried again a second later.
    board.Tried(spot.id, 0.0);
    CHECK_FALSE(board.Tick(spot, 0.5, 80.0f));
    CHECK(board.Tick(spot, 1.0, 70.0f));
    board.Out(spot.id);
    CHECK(board.StatusOf(spot.id) == Status::Out);
    CHECK_FALSE(board.Tick(spot, 2.0, 10.0f));  // it's out: nothing more to do
    // Dropping is by the vehicle's own distance from CJ.
    CHECK_FALSE(Drops(150.0f, false));
    CHECK(Drops(161.0f, false));
    CHECK(Drops(INFINITY, false));
    CHECK_FALSE(Drops(500.0f, true));  // someone uses it: CJ getting in, or a wreck he's at
    board.Dropped(spot.id);
    CHECK(board.StatusOf(spot.id) == Status::Waiting);
    CHECK(board.Tick(spot, 3.0, 90.0f));  // untouched, so it's there again as soon as he's back
}

TEST_CASE("a taken or wrecked one comes back 60 s on, once CJ has been past 160 m")
{
    const auto& spot = Find(1);
    Board       board;
    board.Out(spot.id);
    board.Taken(spot.id, 100.0);
    CHECK(board.StatusOf(spot.id) == Status::Taken);
    // Never while he stays about, however long.
    CHECK_FALSE(board.Tick(spot, 120.0, 20.0f));
    CHECK_FALSE(board.Tick(spot, 1000.0, 150.0f));
    CHECK(board.StatusOf(spot.id) == Status::Taken);
    // Far, but too soon: it waits for the 60 s, and he still has to come back within 100 m.
    Board soon;
    soon.Taken(spot.id, 100.0);
    CHECK_FALSE(soon.Tick(spot, 130.0, 200.0f));
    CHECK_FALSE(soon.Tick(spot, 150.0, 90.0f));
    CHECK(soon.Tick(spot, 160.0, 90.0f));
    CHECK(soon.StatusOf(spot.id) == Status::Waiting);
    // In the first board he goes far now: back once he's within 100 m again.
    CHECK_FALSE(board.Tick(spot, 1001.0, 300.0f));
    CHECK(board.StatusOf(spot.id) == Status::Waiting);
    CHECK(board.Tick(spot, 1002.0, 95.0f));
    // Another world counts as far.
    Board away;
    away.Taken(spot.id, 0.0);
    CHECK_FALSE(away.Tick(spot, 10.0, INFINITY));
    CHECK(away.Tick(spot, 61.0, 50.0f));
}

TEST_CASE("a handover puts the untouched ones back to waiting, the taken stay taken")
{
    Board board;
    board.Out(1);
    board.Out(20);
    board.Taken(20, 5.0);
    board.Handover();
    CHECK(board.StatusOf(1) == Status::Waiting);
    CHECK(board.StatusOf(20) == Status::Taken);
    CHECK(board.StatusOf(50) == Status::Waiting);  // never seen
}

TEST_CASE("never put out on top of CJ")
{
    const Vehicle::Footprint size{ 1.0f, 2.5f };
    const Vec3               feet{ 0.0f, 0.0f, 500.0f };
    const auto               spots = Candidates(feet, 0.0f);  // CJ stands on the spot itself
    CHECK_FALSE(ClearOfCj(spots[0], size, feet));
    CHECK_FALSE(ClearOfCj(spots[1], size, feet));  // 3 m off: within its half length and 2 m
    CHECK(ClearOfCj(spots[1 + kAroundSteps], size, feet));  // 6 m off
    CHECK(ClearOfCj({ { 4.6f, 0.0f, 500.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f }, size, feet));
    CHECK_FALSE(ClearOfCj({ { 4.4f, 0.0f, 500.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f }, size, feet));
}
