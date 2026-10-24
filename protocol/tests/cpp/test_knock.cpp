#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "../../cpp/knock.h"

using namespace AhShitRim::Knock;

TEST_CASE("which way CJ falls: on his front pushed on, on his back pushed back, spinning from the side")
{
    // Facing north (+y).
    CHECK(FallFor(0.0f, 1.0f, 0.0f, 1.0f) == kFront);   // hit from behind
    CHECK(FallFor(0.0f, 1.0f, 0.0f, -1.0f) == kBack);   // hit from the front
    CHECK(FallFor(0.0f, 1.0f, -1.0f, 0.0f) == kSpinLeft);  // pushed west, to his left
    CHECK(FallFor(0.0f, 1.0f, 1.0f, 0.0f) == kSpinRight);  // pushed east, to his right
    CHECK(FallFor(0.0f, 0.0f, 1.0f, 0.0f) == kBack);    // no facing: on his back
    CHECK(FallFor(0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f) == kBack);
}

TEST_CASE("the launch: away from the blow, up a little, straight back when on top of it")
{
    const auto away = Launch({ 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f);
    CHECK(away.x == doctest::Approx(kSpeed));
    CHECK(away.y == doctest::Approx(0.0f));
    CHECK(away.z == doctest::Approx(kSpeed * kRise));
    const auto back = Launch({ 5.0f, 5.0f, 0.0f }, { 5.0f, 5.0f, 0.0f }, 0.0f, 1.0f);
    CHECK(back.y == doctest::Approx(-kSpeed));  // facing north, thrown south
    const auto none = Launch({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f);
    CHECK(none.x == 0.0f);
    CHECK(none.z > 0.0f);
}

namespace
{
    // Ticks a_down a_seconds in 0.1 s frames, knocked or not; returns how many of each step came.
    struct Steps
    {
        int fell = 0, force = 0, up = 0;
    };

    Steps Run(Down& a_down, float a_seconds, bool a_knocked)
    {
        Steps steps;
        for (float t = 0.0f; t < a_seconds - 1e-4f; t += 0.1f) {
            switch (a_down.Tick(0.1f, a_knocked)) {
            case Step::Fell: steps.fell++; break;
            case Step::Force: steps.force++; break;
            case Step::Up: steps.up++; break;
            default: break;
            }
        }
        return steps;
    }
}

TEST_CASE("knocked down: left where it falls, up on its own before the bound is just up")
{
    Down down;
    CHECK(down.Tick(0.1f, false) == Step::None);
    CHECK_FALSE(down.IsDown());
    CHECK(down.Tick(0.1f, true) == Step::Fell);
    CHECK(down.Unpinned());
    const auto steps = Run(down, 2.0f, true);
    CHECK(steps.fell == 0);  // still down is no new blow
    CHECK(steps.force == 0);
    CHECK(down.Tick(0.1f, false) == Step::Up);
    CHECK_FALSE(down.Unpinned());
    CHECK_FALSE(down.IsDown());
}

TEST_CASE("knocked down too long: made to get up, back at CJ, tried again a few times, never a new blow")
{
    Down down;
    CHECK(down.Tick(0.1f, true) == Step::Fell);
    auto steps = Run(down, kDownMost - 0.2f, true);
    CHECK(steps.force == 0);
    CHECK(down.Unpinned());
    steps = Run(down, 0.3f, true);
    CHECK(steps.force == 1);
    CHECK_FALSE(down.Unpinned());  // pinned to CJ again
    CHECK(down.IsDown());
    CHECK(down.Tries() == 1);
    steps = Run(down, 10.0f, true);  // Skyrim still says it's down
    CHECK(steps.fell == 0);
    CHECK(steps.force == kFixTries - 1);
    CHECK(down.Tries() == kFixTries);
    CHECK_FALSE(down.Unpinned());
    CHECK(down.Tick(0.1f, false) == Step::Up);
    CHECK_FALSE(down.IsDown());
    CHECK(down.Tick(0.1f, true) == Step::Fell);  // a new blow after it was up
}

TEST_CASE("the bound covers CJ's fall and get-up")
{
    CHECK(kDownMost >= 2.5f);
    CHECK(kDownMost <= 4.0f);
}
