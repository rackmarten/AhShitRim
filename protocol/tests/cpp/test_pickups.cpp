#include <cmath>
#include <ostream>
#include <set>

#include <doctest/doctest.h>

#include "../../cpp/pickups.h"

using namespace AhShitRim;
using namespace AhShitRim::Pickups;

namespace
{
    constexpr float M = Pose::kUnitsPerMeter;

    const Spot& Find(std::uint16_t a_id)
    {
        const auto* spot = SpotById(a_id);
        REQUIRE(spot != nullptr);
        return *spot;
    }
}

TEST_CASE("walking into one takes it, as San Andreas's CPickup::Update does")
{
    const Vec3 feet{ 1000.0f, 2000.0f, 300.0f };
    const Vec3 pickup{ 1000.0f, 2000.0f, 300.0f + kFloat };
    CHECK(Touches(feet, pickup));
    CHECK(Touches(feet, { pickup.x + 1.3f * M, pickup.y, pickup.z }));
    CHECK_FALSE(Touches(feet, { pickup.x + 1.4f * M, pickup.y, pickup.z }));
    CHECK_FALSE(Touches(feet, { pickup.x + 1.0f * M, pickup.y + 1.0f * M, pickup.z }));  // 1.41 m across
    // Up or down: within 2 m of his middle, a metre over his feet.
    CHECK(Touches(feet, { pickup.x, pickup.y, feet.z + 2.9f * M }));
    CHECK_FALSE(Touches(feet, { pickup.x, pickup.y, feet.z + 3.1f * M }));
    CHECK(Touches(feet, { pickup.x, pickup.y, feet.z - 0.9f * M }));
    CHECK_FALSE(Touches(feet, { pickup.x, pickup.y, feet.z - 1.1f * M }));
}

TEST_CASE("what CJ won't take")
{
    Cj cj;
    cj.health = 50.0f;
    cj.healthMax = 100.0f;
    cj.armour = 40.0f;
    CHECK(Wants(Kind::Health, cj));
    CHECK(Wants(Kind::Armour, cj));
    CHECK(Wants(Kind::Jetpack, cj));
    CHECK(Wants(Kind::Weapon, cj));
    CHECK(Wants(Kind::Parachute, cj));
    CHECK(Wants(Kind::Money, cj));
    CHECK_FALSE(Wants(Kind::None, cj));

    auto full = cj;
    full.health = 100.0f;
    full.armour = 100.0f;
    full.jetpack = true;
    CHECK_FALSE(Wants(Kind::Health, full));
    CHECK_FALSE(Wants(Kind::Armour, full));
    CHECK_FALSE(Wants(Kind::Jetpack, full));
    CHECK(Wants(Kind::Weapon, full));

    auto driving = cj;
    driving.inVehicle = true;
    CHECK_FALSE(Wants(Kind::Health, driving));
    CHECK_FALSE(Wants(Kind::Weapon, driving));
}

TEST_CASE("a weapon off the street comes with San Andreas's ammo for it")
{
    CHECK(StreetAmmo(Weapons::Pistol) == 30);
    CHECK(StreetAmmo(Weapons::Shotgun) == 15);
    CHECK(StreetAmmo(Weapons::Ak47) == 80);
    CHECK(StreetAmmo(Weapons::SniperRifle) == 10);
    CHECK(StreetAmmo(Weapons::RocketLauncher) == 4);
    CHECK(StreetAmmo(Weapons::Minigun) == 500);
    CHECK(StreetAmmo(Weapons::Molotov) == 8);
    CHECK(StreetAmmo(Weapons::Parachute) == 1);
    CHECK(StreetAmmo(Weapons::Katana) == 1);
    CHECK(StreetAmmo(200) == 1);
}

TEST_CASE("a street pickup is back 30 s after it was taken, once CJ is 10 m off")
{
    const auto& pistol = Find(60);
    REQUIRE(pistol.respawn == Respawn::Street);
    Board board;
    CHECK(board.Shows(pistol.id));
    board.Take(pistol, 100.0);
    CHECK_FALSE(board.Shows(pistol.id));
    CHECK_FALSE(board.Tick(pistol, 129.0, 50.0f * M));  // too soon
    CHECK_FALSE(board.Tick(pistol, 131.0, 5.0f * M));   // he is standing on it
    CHECK_FALSE(board.Shows(pistol.id));
    CHECK(board.Tick(pistol, 131.0, 11.0f * M));
    CHECK(board.Shows(pistol.id));
    CHECK(board.Tick(pistol, 132.0, 0.0f));  // one that is out stays out
}

TEST_CASE("armour waits 6 minutes, a one-off for good")
{
    const auto& armour = Find(20);
    REQUIRE(armour.respawn == Respawn::Slow);
    Board board;
    board.Take(armour, 0.0);
    CHECK_FALSE(board.Tick(armour, 359.0, 100.0f * M));
    CHECK(board.Tick(armour, 361.0, 100.0f * M));

    const auto& minigun = Find(81);
    REQUIRE(minigun.respawn == Respawn::Once);
    board.Take(minigun, 0.0);
    CHECK_FALSE(board.Tick(minigun, 1e9, 1e6f));
    CHECK_FALSE(board.Shows(minigun.id));
}

TEST_CASE("the co-save keeps the one-offs taken, and only those")
{
    Board board;
    board.Take(Find(80), 10.0);
    board.Take(Find(82), 20.0);
    board.Take(Find(1), 30.0);   // a parachute: it comes back by itself
    board.Take(Find(20), 40.0);  // armour too
    CHECK(board.Once() == std::vector<std::uint16_t>{ 80, 82 });

    Board loaded;
    loaded.Take(Find(60), 0.0);
    loaded.Restore({ 80, 82, 1, 9999 });  // a street spot and an id no spot has any more are left out
    CHECK_FALSE(loaded.Shows(80));
    CHECK_FALSE(loaded.Shows(82));
    CHECK(loaded.Shows(1));
    CHECK(loaded.Shows(60));  // what was taken before the load is back
    CHECK(loaded.Once() == std::vector<std::uint16_t>{ 80, 82 });
}

TEST_CASE("a heart turns up under a quarter of CJ's health, at most once every 3 minutes")
{
    HeartRule rule;
    CHECK_FALSE(rule.Due(30.0f, 100.0f, 0.0, false));
    CHECK(rule.Due(24.0f, 100.0f, 0.0, false));
    CHECK_FALSE(rule.Due(24.0f, 100.0f, 0.0, true));  // one is out already
    CHECK_FALSE(rule.Due(0.0f, 100.0f, 0.0, false));  // dying: Wasted, not a heart
    CHECK_FALSE(rule.Due(10.0f, 0.0f, 0.0, false));
    rule.Spawned(10.0);
    CHECK_FALSE(rule.Due(10.0f, 100.0f, 100.0, false));
    CHECK_FALSE(rule.Due(10.0f, 100.0f, 189.0, false));
    CHECK(rule.Due(10.0f, 100.0f, 190.0, false));
    rule.Reset();
    CHECK(rule.Due(10.0f, 100.0f, 11.0, false));
}

TEST_CASE("the heart is tried in front of CJ first")
{
    const Vec3 feet{ 0.0f, 0.0f, 50.0f };
    const auto north = HeartPlaces(feet, 0.0f);
    REQUIRE(north.size() >= 5);
    CHECK(north[0].x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(north[0].y == doctest::Approx(6.0f * M));
    CHECK(north[0].z == 50.0f);
    CHECK(north[1].x > 0.0f);  // a little to his right
    CHECK(north[2].x < 0.0f);
    const auto east = HeartPlaces(feet, 1.5707964f);
    CHECK(east[0].x == doctest::Approx(6.0f * M));
    CHECK(std::abs(east[0].y) < 1.0f);
    for (const auto& p : north) {
        const float d = std::hypot(p.x, p.y);
        CHECK(d >= 3.0f * M - 1.0f);
        CHECK(d <= 8.0f * M + 1.0f);
    }
}

TEST_CASE("pickups spin a turn every 2 s, all together")
{
    CHECK(SpinAt(0.0) == doctest::Approx(0.0f));
    CHECK(SpinAt(0.5) == doctest::Approx(1.5707964f));
    CHECK(SpinAt(2.5) == doctest::Approx(1.5707964f));
    CHECK(SpinAt(1e6 + 1.0) == doctest::Approx(3.1415927f).epsilon(1e-3));
}

TEST_CASE("the spot table")
{
    std::set<std::uint16_t> ids;
    int                     parachutes = 0, armour = 0;
    for (const auto& spot : kSpots) {
        CHECK(ids.insert(spot.id).second);  // ids are unique
        CHECK(std::isfinite(spot.at.x + spot.at.y + spot.at.z));
        CHECK(spot.kind != Kind::None);
        CHECK(spot.kind != Kind::Jetpack);  // only San Andreas drops one
        CHECK(!spot.name.empty());
        CHECK(RayFrom(spot) > spot.at.z);
        CHECK(RayTo(spot) < spot.at.z);
        if (spot.kind == Kind::Weapon) {
            CHECK(Weapons::Carried(spot.weapon));
            CHECK(!Weapons::ModelName(spot.weapon).empty());
        }
        if (spot.kind == Kind::Money) {
            CHECK(spot.amount > 0);
            CHECK(spot.respawn == Respawn::Once);
        }
        parachutes += spot.kind == Kind::Parachute;
        armour += spot.kind == Kind::Armour;
    }
    CHECK(parachutes >= 5);
    CHECK(armour >= 18);  // two or more per hold
    // The ones the 0.1 list asked for by name.
    CHECK(Find(1).name == "the Throat of the World");
    CHECK(Find(1).kind == Kind::Parachute);
    CHECK(Find(1).at.z > 36000.0f);  // the summit, above High Hrothgar
    CHECK(Find(2).kind == Kind::Parachute);
    CHECK(Find(2).at.z < Find(1).at.z);
    CHECK(Find(3).kind == Kind::Parachute);
    CHECK(Find(4).world == World::Whiterun);
    CHECK(Find(4).top);
    CHECK(WorldForm(World::Tamriel) == 0x3C);
    CHECK(WorldForm(World::Whiterun) == 0x1A26F);
    CHECK(SpotById(9999) == nullptr);
}

TEST_CASE("San Andreas's models for each kind")
{
    CHECK(ModelName(Kind::Health) == "health");
    CHECK(ModelName(Kind::Armour) == "bodyarmour");
    CHECK(ModelName(Kind::Parachute) == Weapons::ModelName(Weapons::Parachute));
    CHECK(ModelName(Kind::Jetpack) == "jetpack");
    CHECK(ModelName(Kind::Weapon).empty());
    CHECK(KindOfModel("jetpack") == Kind::Jetpack);
    CHECK(KindOfModel("bodyarmour") == Kind::Armour);
    CHECK(KindOfModel("cellphone") == Kind::None);
    CHECK(ModelKey(Kind::Weapon, Weapons::Ak47) != ModelKey(Kind::Weapon, Weapons::M4));
    CHECK(ModelKey(Kind::Health, 7) == ModelKey(Kind::Health, 0));
    CHECK(ModelKey(Kind::Health, 0) != ModelKey(Kind::Armour, 0));
}
