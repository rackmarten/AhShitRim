#include <cmath>

#include <doctest/doctest.h>

#include "../../cpp/lock_marker.h"

using namespace AhShitRim::LockMarker;

TEST_CASE("the lock-on marker is green while it locks on and red once locked")
{
    CHECK(ColourOf(false) == 0xFFFFFFFF);  // white: lockon's own green
    CHECK(ColourOf(true) == 0xFF0000FF);   // red, opaque, red in the low byte
    // Tinted, the textures' green (102, 255, 0) goes dark red: only the red channel is kept.
    const auto tint = [](std::uint32_t a_colour, int a_channel, int a_texel) { return ((a_colour >> (8 * a_channel)) & 0xFF) * a_texel / 255; };
    CHECK(tint(kLocked, 0, 102) == 102);
    CHECK(tint(kLocked, 1, 255) == 0);
    CHECK(tint(kLocking, 1, 255) == 255);
}

TEST_CASE("the inner ring turns once every 1.024 s on the game's clock")
{
    CHECK(SpinAngle(0) == 0.0f);
    CHECK(SpinAngle(256) == doctest::Approx(kPi / 2.0f));
    CHECK(SpinAngle(512) == doctest::Approx(kPi));
    CHECK(SpinAngle(1023) == doctest::Approx(2.0f * kPi * 1023.0f / 1024.0f));
    CHECK(SpinAngle(1024) == 0.0f);  // round again
    CHECK(SpinAngle(1024 * 1000 + 256) == doctest::Approx(SpinAngle(256)));
}

TEST_CASE("the marker's draws in San Andreas's order: lockon then lockonFire, each outlined in black first")
{
    const auto layers = Layers(true, 512);
    for (int i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(layers[i].inner == (i >= 3));
        const float size = kHalfSize * (i >= 3 ? kInnerScale : kRingScale);
        const float scale = i % 3 == 0 ? 0.95f : i % 3 == 1 ? 1.05f : 1.0f;
        CHECK(layers[i].halfSize == doctest::Approx(size * scale));
        CHECK(layers[i].colour == (i % 3 == 2 ? kLocked : kOutline));
        CHECK(layers[i].rotation == doctest::Approx(i >= 3 ? kPi : 0.0f));  // only the inner ring turns
    }
    CHECK(layers[2].halfSize == doctest::Approx(36.0f));  // lockon: 20 pixels a half at 640 across, 1.8 times
    CHECK(layers[5].halfSize == doctest::Approx(16.0f));  // lockonFire: 0.8 times
    CHECK(Layers(false, 0)[2].colour == kLocking);
    CHECK(Layers(false, 0)[5].colour == kLocking);
}
