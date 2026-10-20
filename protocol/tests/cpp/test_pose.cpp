#include <doctest/doctest.h>

#include "../../cpp/pose.h"

using namespace AhShitRim;
using namespace AhShitRim::Pose;

TEST_CASE("headings are Skyrim's: 0 north, clockwise")
{
    CHECK(HeadingOf(0.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(HeadingOf(1.0f, 0.0f) == doctest::Approx(kPi / 2));      // east
    CHECK(HeadingOf(0.0f, -1.0f) == doctest::Approx(kPi));         // south
    CHECK(HeadingOf(-1.0f, 0.0f) == doctest::Approx(3 * kPi / 2)); // west, never negative
}

TEST_CASE("pitch is positive looking down")
{
    CHECK(PitchOf({ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.0f));
    CHECK(PitchOf({ 0.0f, 1.0f, -1.0f }) == doctest::Approx(kPi / 4));
    CHECK(PitchOf({ 0.0f, 1.0f, 1.0f }) == doctest::Approx(-kPi / 4));
}

TEST_CASE("a Skyrim heading becomes San Andreas's counter-clockwise rotation")
{
    CHECK(SanAndreasHeading(0.0f) == doctest::Approx(0.0f));
    CHECK(SanAndreasHeading(kPi / 2) == doctest::Approx(-kPi / 2));  // east is -90 degrees in San Andreas
    CHECK(SanAndreasHeading(3 * kPi / 2) == doctest::Approx(kPi / 2));  // west, wrapped into -pi..pi
    // San Andreas faces (-sin r, cos r): the same way as Skyrim's (sin h, cos h).
    const float h = 0.7f;
    const float r = SanAndreasHeading(h);
    CHECK(-std::sin(r) == doctest::Approx(std::sin(h)));
    CHECK(std::cos(r) == doctest::Approx(std::cos(h)));
}

TEST_CASE("CJ's pose becomes a Skyrim offset from the handover spawn")
{
    SanAndreasPose sa;
    sa.spawn = { 10.0f, 20.0f, 500.0f };
    sa.position = { 11.0f, 18.0f, 500.5f };
    sa.velocity = { 0.0f, -2.0f, 0.0f };
    sa.forward = { 0.0f, -1.0f, 0.0f };
    sa.view = { 1.0f, 0.0f, -1.0f };
    sa.grounded = true;

    const auto s = ToPlayerState(sa, 99, 4);
    CHECK(s.frame == 99);
    CHECK(s.handover == 4);
    CHECK(s.flags == (Protocol::kPlayerValid | Protocol::kPlayerGrounded));
    CHECK(s.offset[0] == doctest::Approx(kUnitsPerMeter));
    CHECK(s.offset[1] == doctest::Approx(-2.0f * kUnitsPerMeter));
    CHECK(s.offset[2] == doctest::Approx(0.5f * kUnitsPerMeter));
    CHECK(s.velocity[1] == doctest::Approx(-2.0f * kUnitsPerMeter));
    CHECK(s.heading == doctest::Approx(kPi));
    CHECK(s.viewHeading == doctest::Approx(kPi / 2));
    CHECK(s.viewPitch == doctest::Approx(kPi / 4));
    CHECK(Finite(s));

    sa.grounded = false;
    CHECK(ToPlayerState(sa, 1, 1).flags == Protocol::kPlayerValid);
    sa.crouching = true;
    CHECK(ToPlayerState(sa, 1, 1).flags == (Protocol::kPlayerValid | Protocol::kPlayerCrouching));
}

TEST_CASE("70 Skyrim units to the metre")
{
    CHECK(kUnitsPerMeter == doctest::Approx(69.99f).epsilon(0.001));
}

TEST_CASE("a pose with NaN in it is not finite")
{
    Protocol::PlayerState s{};
    CHECK(Finite(s));
    s.velocity[2] = std::nanf("");
    CHECK_FALSE(Finite(s));
}

TEST_CASE("the vehicle CJ is in and the damage San Andreas worked out reach Skyrim in its units")
{
    SanAndreasPose sa;
    sa.forward = { 0.0f, 1.0f, 0.0f };
    sa.view = { 0.0f, 1.0f, 0.0f };
    CHECK(ToPlayerState(sa, 1, 1).vehicleModel == 0);

    sa.vehicleModel = 495;
    sa.vehicleColours[0] = 3;
    sa.vehicleColours[3] = 9;
    sa.vehicleOffset = { 1.0f, 0.0f, -0.5f };
    sa.vehicleForward = { 1.0f, 0.0f, 0.0f };  // east
    sa.damage = 12.5f;
    const auto s = ToPlayerState(sa, 1, 1);
    CHECK(s.vehicleModel == 495);
    CHECK(s.vehicleColours[0] == 3);
    CHECK(s.vehicleColours[3] == 9);
    CHECK(s.vehicleOffset[0] == doctest::Approx(kUnitsPerMeter));
    CHECK(s.vehicleOffset[2] == doctest::Approx(-0.5f * kUnitsPerMeter));
    CHECK(s.vehicleHeading == doctest::Approx(kPi / 2));
    CHECK(s.damage == doctest::Approx(12.5f));
    CHECK(Finite(s));

    Protocol::PlayerState bad = s;
    bad.damage = std::nanf("");
    CHECK_FALSE(Finite(bad));
}
