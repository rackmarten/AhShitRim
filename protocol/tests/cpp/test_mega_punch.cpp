#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "../../cpp/mega_punch.h"

using namespace AhShitRim;
using namespace AhShitRim::MegaPunch;

TEST_CASE("mega punch: fists, brass knuckles and the parachute, as in San Andreas")
{
    CHECK(Punches(Weapons::Unarmed));
    CHECK(Punches(Weapons::BrassKnuckle));
    CHECK(Punches(Weapons::Parachute));
    CHECK(Weapons::Parachute == 46);
    CHECK_FALSE(Punches(Weapons::BaseballBat));
    CHECK_FALSE(Punches(Weapons::Knife));
    CHECK_FALSE(Punches(Weapons::Katana));
    CHECK_FALSE(Punches(Weapons::Chainsaw));
    CHECK_FALSE(Punches(Weapons::GolfClub));
    CHECK_FALSE(Punches(Weapons::Pistol));
}

TEST_CASE("mega punch: the blow does the victim's whole health, whatever the difficulty")
{
    CHECK(DamageFor(50.0f) >= 50.0f);
    CHECK(DamageFor(50.0f) * 0.25f > 50.0f);  // Legendary's quarter still kills
    CHECK(DamageFor(3000.0f) * 0.25f > 3000.0f);  // a dragon too
    CHECK(DamageFor(0.0f) > 0.0f);  // a blow on someone at 0 still lands
    CHECK(DamageFor(-5.0f) > 0.0f);
    CHECK(DamageFor(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
}

TEST_CASE("mega punch: people fly, giants and dragons only fall")
{
    CHECK(KnockFor(Damage::Class::Normal) == kKnock);
    CHECK(kKnock > 14.0f);  // further than a rocket throws
    CHECK(KnockFor(Damage::Class::Giant) == 0.0f);
    CHECK(KnockFor(Damage::Class::Dragon) == 0.0f);
}

TEST_CASE("mega punch: the knock goes off behind the fist and below it")
{
    const Vec fist{ 100.0f, 200.0f, 80.0f };
    const Vec from = KnockFrom(fist, { 0.0f, 2.0f, 0.0f });  // facing +y, any length
    CHECK(from.x == doctest::Approx(100.0f));
    CHECK(from.y == doctest::Approx(200.0f - kBehind));
    CHECK(from.z == doctest::Approx(80.0f - kBelow));
    // So the push (away from it) goes forward and up.
    CHECK(fist.y - from.y > 0.0f);
    CHECK(fist.z - from.z > 0.0f);
    // No facing: straight below.
    const Vec below = KnockFrom(fist, { 0.0f, 0.0f, 0.0f });
    CHECK(below.x == doctest::Approx(100.0f));
    CHECK(below.y == doctest::Approx(200.0f));
    CHECK(below.z < fist.z);
}
