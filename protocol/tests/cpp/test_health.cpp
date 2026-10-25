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
