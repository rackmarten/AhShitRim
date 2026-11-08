#include <doctest/doctest.h>

#include "../../cpp/traps.h"

#include <cmath>

using namespace AhShitRim;
using namespace AhShitRim::Traps;

TEST_CASE("traps: each of CJ's hits comes from a source Skyrim's scripts know")
{
    CHECK(SourceOf(Hit::Bullet, Weapons::Pistol) == kHuntingBow);
    CHECK(SourceOf(Hit::Blow, Weapons::Unarmed) == kUnarmed);
    CHECK(SourceOf(Hit::Blow, Weapons::BrassKnuckle) == kUnarmed);
    CHECK(SourceOf(Hit::Blow, Weapons::Knife) == kIronSword);
    CHECK(SourceOf(Hit::Blow, Weapons::Chainsaw) == kIronSword);
    CHECK(SourceOf(Hit::Blow, Weapons::BaseballBat) == kIronMace);
    // TrapExplosiveGas (the oil on the ground) ignites on a hit from Torch01.
    CHECK(SourceOf(Hit::Fire, Weapons::Flamethrower) == kTorch);
    CHECK(SourceOf(Hit::Blast, Weapons::RocketLauncher) == kTorch);
}

TEST_CASE("traps: bullets and blows hurt objects as they would an actor, fire and blasts tear webs")
{
    const float pistol = SkyrimDamage(25.0f, Weapons::Pistol);
    CHECK(pistol == doctest::Approx(25.0f * Damage::kScale));
    CHECK(ObjectDamage(Hit::Bullet, pistol) == doctest::Approx(pistol));
    CHECK(ObjectDamage(Hit::Blow, 5.0f) == doctest::Approx(5.0f));
    CHECK(ObjectDamage(Hit::Fire, 2.0f) == kBurns);
    CHECK(ObjectDamage(Hit::Blast, 400.0f) == doctest::Approx(400.0f));
    CHECK(ObjectDamage(Hit::Bullet, NAN) == 0.0f);
    CHECK(ObjectDamage(Hit::Blow, -3.0f) == 0.0f);
}

TEST_CASE("traps: what is told of a hit, and whose collision stays round CJ")
{
    CHECK(Told(Base::Activator, false));  // a lamp, the oil, a lever
    CHECK(Told(Base::Static, true));      // a destructible static
    CHECK_FALSE(Told(Base::Static, false));
    CHECK_FALSE(Told(Base::Other, false));

    CHECK(KeepsCollision(kLayerProps, true));  // a web
    CHECK(KeepsCollision(kLayerTrap, true));
    CHECK_FALSE(KeepsCollision(kLayerProps, false));
    CHECK_FALSE(KeepsCollision(15, true));  // non-collidable stays out
}

TEST_CASE("traps: an object takes CJ's fire once a second at most")
{
    Scorched scorched;
    CHECK(scorched.Takes(0x1234, 10.0));
    CHECK_FALSE(scorched.Takes(0x1234, 10.5));
    CHECK(scorched.Takes(0x5678, 10.5));
    CHECK(scorched.Takes(0x1234, 11.0));
    for (std::uint32_t i = 0; i < Scorched::kRemembered; ++i) {
        scorched.Takes(0x9000 + i, 11.0);
    }
    CHECK(scorched.Takes(0x1234, 11.1));  // forgotten among more than it remembers
    scorched.Clear();
    CHECK(scorched.Takes(0x5678, 11.1));
}

TEST_CASE("traps: oil burns GTA style, from bullets as well as fire and blasts")
{
    CHECK(IgnitesOil(Hit::Bullet));
    CHECK(IgnitesOil(Hit::Fire));
    CHECK(IgnitesOil(Hit::Blast));
    CHECK_FALSE(IgnitesOil(Hit::Blow));

    // A pool 2 m across (bound radius 70) on the floor at z 100.
    const Damage::Vec3 pool{ 1000.0f, 2000.0f, 100.0f };
    CHECK(InOil({ 1000.0f, 2000.0f, 100.0f }, pool, 70.0f));     // the middle
    CHECK(InOil({ 1075.0f, 2000.0f, 98.0f }, pool, 70.0f));      // its edge, the margin past it
    CHECK_FALSE(InOil({ 1090.0f, 2000.0f, 100.0f }, pool, 70.0f));
    CHECK_FALSE(InOil({ 1060.0f, 2060.0f, 100.0f }, pool, 70.0f));  // a corner past the round pool
    CHECK_FALSE(InOil({ 1000.0f, 2050.0f, 160.0f }, pool, 70.0f));  // the wall above it
    CHECK_FALSE(InOil({ NAN, 2000.0f, 100.0f }, pool, 70.0f));
    CHECK_FALSE(InOil({ 1000.0f, 2000.0f, 100.0f }, pool, NAN));
}
