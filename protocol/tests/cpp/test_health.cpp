#include <doctest/doctest.h>

#include "../../cpp/health.h"

using namespace AhShitRim::Health;

TEST_CASE("armour absorbs damage before health")
{
    const auto split = Absorb(50.0f, 30.0f, 200.0f);
    CHECK(split.armor == doctest::Approx(20.0f));
    CHECK(split.health == doctest::Approx(0.0f));
}

TEST_CASE("damage past the armour is a share of Skyrim's maximum health")
{
    // 80 of 100 San Andreas points, 30 absorbed: 50 points left over, half a bar of a 300 health player.
    const auto split = Absorb(30.0f, 80.0f, 300.0f);
    CHECK(split.armor == doctest::Approx(0.0f));
    CHECK(split.health == doctest::Approx(150.0f));
}

TEST_CASE("without armour a whole bar of damage is all of Skyrim's health")
{
    CHECK(Absorb(0.0f, 100.0f, 250.0f).health == doctest::Approx(250.0f));
    CHECK(Absorb(0.0f, 0.0f, 250.0f).health == doctest::Approx(0.0f));
}

TEST_CASE("armour and damage are kept sane")
{
    CHECK(Absorb(500.0f, 0.0f, 100.0f).armor == doctest::Approx(100.0f));
    CHECK(Absorb(-5.0f, 10.0f, 100.0f).health == doctest::Approx(10.0f));
    CHECK(Absorb(0.0f, -10.0f, 100.0f).health == doctest::Approx(0.0f));
    CHECK(Absorb(0.0f, std::nanf(""), 100.0f).health == doctest::Approx(0.0f));
    CHECK(Absorb(std::nanf(""), 10.0f, 100.0f).health == doctest::Approx(10.0f));
}

TEST_CASE("what San Andreas took off CJ in a frame")
{
    CHECK(Lost(kSaHealth, kSaHealth - 12.0f) == doctest::Approx(12.0f));
    CHECK(Lost(kSaHealth, kSaHealth) == 0.0f);
    CHECK(Lost(kSaHealth, kSaHealth + 5.0f) == 0.0f);  // healing is not damage
    CHECK(Lost(kSaHealth, std::nanf("")) == 0.0f);
}

TEST_CASE("the damage meter turns a running total into new damage")
{
    Meter meter;
    CHECK(meter.Take(40.0f) == 0.0f);  // the first value only seeds it
    CHECK(meter.Take(40.0f) == 0.0f);
    CHECK(meter.Take(55.0f) == doctest::Approx(15.0f));
    CHECK(meter.Take(90.0f) == doctest::Approx(35.0f));  // a skipped frame loses nothing
    CHECK(meter.Take(3.0f) == 0.0f);                     // San Andreas started over
    CHECK(meter.Take(8.0f) == doctest::Approx(5.0f));
    CHECK(meter.Take(std::nanf("")) == 0.0f);
    meter.Reset();
    CHECK(meter.Take(100.0f) == 0.0f);
}

TEST_CASE("Skyrim's hits go through CJ's armour first")
{
    // A 30 hit on a 200 health player is 15 San Andreas points: all armour while it lasts.
    auto split = AbsorbSkyrim(100.0f, 30.0f, 200.0f);
    CHECK(split.armor == doctest::Approx(85.0f));
    CHECK(split.health == doctest::Approx(0.0f));

    // 10 points of armour left: they go, and the other 5 points are 10 of Skyrim's health.
    split = AbsorbSkyrim(10.0f, 30.0f, 200.0f);
    CHECK(split.armor == doctest::Approx(0.0f));
    CHECK(split.health == doctest::Approx(10.0f));

    // No armour: the hit as it was.
    split = AbsorbSkyrim(0.0f, 30.0f, 200.0f);
    CHECK(split.armor == doctest::Approx(0.0f));
    CHECK(split.health == doctest::Approx(30.0f));

    // Full armour takes a whole bar of health's worth.
    split = AbsorbSkyrim(100.0f, 250.0f, 200.0f);
    CHECK(split.armor == doctest::Approx(0.0f));
    CHECK(split.health == doctest::Approx(50.0f));
}

TEST_CASE("Skyrim's hits through the armour are kept sane")
{
    CHECK(AbsorbSkyrim(50.0f, -5.0f, 200.0f).health == doctest::Approx(0.0f));
    CHECK(AbsorbSkyrim(50.0f, -5.0f, 200.0f).armor == doctest::Approx(50.0f));
    CHECK(AbsorbSkyrim(50.0f, NAN, 200.0f).health == doctest::Approx(0.0f));
    CHECK(AbsorbSkyrim(50.0f, 30.0f, 0.0f).health == doctest::Approx(30.0f));  // no maximum to share: health takes it
    CHECK(AbsorbSkyrim(50.0f, 30.0f, 0.0f).armor == doctest::Approx(50.0f));
}

TEST_CASE("what CJ's armour takes, as San Andreas's does")
{
    CHECK(ArmourTakes(Kind::Blow));
    CHECK(ArmourTakes(Kind::Fire));
    CHECK(ArmourTakes(Kind::Frost));
    CHECK(ArmourTakes(Kind::Shock));
    CHECK(ArmourTakes(Kind::Magic));
    CHECK_FALSE(ArmourTakes(Kind::Poison));
    CHECK_FALSE(ArmourTakes(Kind::Disease));
    CHECK_FALSE(ArmourTakes(Kind::Fall));
    CHECK_FALSE(ArmourTakes(Kind::Drowning));
}

TEST_CASE("only fire sets CJ alight")
{
    CHECK(SetsAlight(Kind::Fire));
    CHECK_FALSE(SetsAlight(Kind::Frost));
    CHECK_FALSE(SetsAlight(Kind::Shock));
    CHECK_FALSE(SetsAlight(Kind::Blow));
    CHECK_FALSE(SetsAlight(Kind::Poison));
}

TEST_CASE("a magic effect's kind is what resists it")
{
    CHECK(OfEffect(Resist::Fire) == Kind::Fire);
    CHECK(OfEffect(Resist::Frost) == Kind::Frost);
    CHECK(OfEffect(Resist::Shock) == Kind::Shock);
    CHECK(OfEffect(Resist::Poison) == Kind::Poison);
    CHECK(OfEffect(Resist::Disease) == Kind::Disease);
    CHECK(OfEffect(Resist::Magic) == Kind::Magic);
    CHECK(OfEffect(Resist::None) == Kind::Magic);
}

TEST_CASE("a hit with no magic behind it is a blow, trap or not, unless it's drowning")
{
    CHECK(OfHit(true, false) == Kind::Blow);   // a bandit's sword, a spider's bite
    CHECK(OfHit(false, false) == Kind::Blow);  // a blade trap, falling rocks
    CHECK(OfHit(true, true) == Kind::Blow);    // a slaughterfish under water
    CHECK(OfHit(false, true) == Kind::Drowning);
}

TEST_CASE("damage comes in either sign: HandleHealthDamage's is negative")
{
    CHECK(DamageOf(-8.5f) == doctest::Approx(8.5f));
    CHECK(DamageOf(8.5f) == doctest::Approx(8.5f));
    CHECK(DamageOf(0.0f) == doctest::Approx(0.0f));
    CHECK(DamageOf(NAN) == doctest::Approx(0.0f));
    CHECK(DamageOf(-INFINITY) == doctest::Approx(0.0f));
}

TEST_CASE("a bandit's hit, negative as the log had it, drains the armour and gives the health back")
{
    // The log's "hit CJ for -8.5 health", full armour, a 200 health player: 4.25 armour points, all the health back.
    for (const float reported : { -8.5f, 8.5f }) {
        const auto s = Settle(100.0f, reported, 1.0f, 200.0f, Kind::Blow);
        CHECK(s.armor == doctest::Approx(95.75f));
        CHECK(s.health == doctest::Approx(0.0f));
        CHECK(s.back == doctest::Approx(8.5f));
    }
}

TEST_CASE("past the armour the rest comes off health, either sign")
{
    for (const float reported : { -30.0f, 30.0f }) {
        const auto s = Settle(10.0f, reported, 1.0f, 200.0f, Kind::Blow);
        CHECK(s.armor == doctest::Approx(0.0f));
        CHECK(s.health == doctest::Approx(10.0f));
        CHECK(s.back == doctest::Approx(20.0f));
    }
    const auto none = Settle(0.0f, -30.0f, 1.0f, 200.0f, Kind::Blow);
    CHECK(none.health == doctest::Approx(30.0f));
    CHECK(none.back == doctest::Approx(0.0f));
}

TEST_CASE("the factor scales the hit before the armour, either sign")
{
    // A guard at 1 star: a quarter of a -40 hit, 10 health, 5 armour points of a 200 health player.
    for (const float reported : { -40.0f, 40.0f }) {
        const auto s = Settle(100.0f, reported, 0.25f, 200.0f, Kind::Blow);
        CHECK(s.armor == doctest::Approx(95.0f));
        CHECK(s.back == doctest::Approx(40.0f));
    }
    // Twice a -10 hit with no armour: 10 more taken.
    const auto twice = Settle(0.0f, -10.0f, 2.0f, 200.0f, Kind::Blow);
    CHECK(twice.health == doctest::Approx(20.0f));
    CHECK(twice.back == doctest::Approx(-10.0f));
}

TEST_CASE("poison, disease and drowning skip the armour; so does everything while it's off")
{
    for (const auto kind : { Kind::Poison, Kind::Disease, Kind::Drowning, Kind::Fall }) {
        const auto s = Settle(100.0f, -12.0f, 1.0f, 200.0f, kind);
        CHECK(s.armor == doctest::Approx(100.0f));
        CHECK(s.health == doctest::Approx(12.0f));
        CHECK(s.back == doctest::Approx(0.0f));
    }
    const auto fire = Settle(100.0f, -12.0f, 1.0f, 200.0f, Kind::Fire);
    CHECK(fire.armor == doctest::Approx(94.0f));
    CHECK(fire.back == doctest::Approx(12.0f));
    const auto busted = Settle(100.0f, -12.0f, 1.0f, 200.0f, Kind::Blow, false);
    CHECK(busted.armor == doctest::Approx(100.0f));
    CHECK(busted.back == doctest::Approx(0.0f));
}
