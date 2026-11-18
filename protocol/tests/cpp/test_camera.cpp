#include <doctest/doctest.h>

#include "../../cpp/camera.h"

using namespace AhShitRim::Camera;

namespace
{
    // A ZR-350's size: 2 m wide, 4.6 m long, 1.3 m tall from its middle.
    constexpr float kMin[3] = { -1.0f, -2.3f, -0.6f };
    constexpr float kMax[3] = { 1.0f, 2.3f, 0.7f };
}

TEST_CASE("the first-person view in a vehicle moves out of its box the way it looks, at San Andreas's height")
{
    const float seat[3] = { 0.0f, 0.3f, 0.5f };  // the front seat, head height, the middle across
    float       out[3];

    const float forward[3] = { 0.0f, 1.0f, 0.0f };
    OutOfBox(seat, forward, kMin, kMax, kBumperMargin, out);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(2.3f + kBumperMargin));  // in front of the bonnet
    CHECK(out[2] == doctest::Approx(0.5f));

    const float back[3] = { 0.0f, -1.0f, 0.0f };
    OutOfBox(seat, back, kMin, kMax, kBumperMargin, out);
    CHECK(out[1] == doctest::Approx(-2.3f - kBumperMargin));  // behind the boot

    const float left[3] = { -1.0f, 0.0f, 0.0f };
    OutOfBox(seat, left, kMin, kMax, kBumperMargin, out);
    CHECK(out[0] == doctest::Approx(-1.0f - kBumperMargin));  // beside the door
    CHECK(out[1] == doctest::Approx(0.3f));
}

TEST_CASE("the view looking down a little goes out level, and straight down out the front")
{
    const float seat[3] = { 0.0f, 0.3f, 0.5f };
    float       out[3];

    const float down[3] = { 0.0f, 0.8f, -0.6f };
    OutOfBox(seat, down, kMin, kMax, kBumperMargin, out);
    CHECK(out[1] == doctest::Approx(2.3f + kBumperMargin));
    CHECK(out[2] == doctest::Approx(0.5f));

    const float straightDown[3] = { 0.0f, 0.0f, -1.0f };
    OutOfBox(seat, straightDown, kMin, kMax, kBumperMargin, out);
    CHECK(out[1] == doctest::Approx(2.3f + kBumperMargin));
}

TEST_CASE("a view looking out at a slant leaves by the nearest side")
{
    const float seat[3] = { 0.0f, 2.0f, 0.5f };
    const float slant[3] = { 0.6f, 0.8f, 0.0f };  // 0.3 m from the front, 1 m from the side: 0.375 m and 1.67 m along it
    float       out[3];
    OutOfBox(seat, slant, kMin, kMax, kBumperMargin, out);
    const float t = 0.3f / 0.8f + kBumperMargin;
    CHECK(out[0] == doctest::Approx(0.6f * t));
    CHECK(out[1] == doctest::Approx(2.0f + 0.8f * t));
}

TEST_CASE("a source over the box moves too, one beside it stays")
{
    float out[3];
    // A bike's view is over its box (at CJ's neck), still within it seen from above: it moves past the handlebars.
    const float bikeMin[3] = { -0.4f, -1.1f, -0.6f };
    const float bikeMax[3] = { 0.4f, 1.1f, 0.5f };
    const float neck[3] = { 0.0f, -0.2f, 1.1f };
    const float forward[3] = { 0.0f, 1.0f, 0.0f };
    OutOfBox(neck, forward, bikeMin, bikeMax, kBumperMargin, out);
    CHECK(out[1] == doctest::Approx(1.1f + kBumperMargin));
    CHECK(out[2] == doctest::Approx(1.1f));

    // Already outside it: as it was.
    const float beside[3] = { 1.5f, 0.0f, 0.5f };
    OutOfBox(beside, forward, kMin, kMax, kBumperMargin, out);
    CHECK(out[0] == doctest::Approx(1.5f));
    CHECK(out[1] == doctest::Approx(0.0f));
}
