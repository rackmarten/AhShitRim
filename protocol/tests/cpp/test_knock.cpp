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

TEST_CASE("after a knock-down, whoever fought the player is set on it again, for a while, and nobody's fight is stopped")
{
    CHECK(Rejoins(true, true, false));         // in no fight any more: back on CJ
    CHECK_FALSE(Rejoins(true, true, true));    // in a fight (CJ's, or someone else's for the leash): left alone
    CHECK_FALSE(Rejoins(false, true, false));  // dead
    CHECK_FALSE(Rejoins(true, false, false));  // gone out of the loaded world

    Rejoin rejoin;
    CHECK_FALSE(rejoin.Active());
    CHECK_FALSE(rejoin.Tick(0.1f));
    rejoin.Start();
    CHECK(rejoin.Active());
    CHECK(rejoin.Tick(0.016f));  // a check at once
    int checks = 1;
    float t = 0.016f;
    while (rejoin.Active() && t < 30.0f) {
        checks += rejoin.Tick(0.1f) ? 1 : 0;
        t += 0.1f;
    }
    CHECK(t == doctest::Approx(kRejoinSeconds).epsilon(0.05));
    CHECK(checks == doctest::Approx(kRejoinSeconds / kRejoinEvery + 1.0f).epsilon(0.15));
    CHECK_FALSE(rejoin.Tick(0.1f));  // over
    rejoin.Start();  // a new knock-down starts it again
    CHECK(rejoin.Tick(0.1f));
    rejoin.Reset();
    CHECK_FALSE(rejoin.Active());
    CHECK(kRejoinSeconds >= 5.0f);  // long enough for the AI to give up on a target it lost
}

TEST_CASE("a skeleton left behind after a knock-down is reset, a few times at most, never while it's on its body")
{
    Skeleton s;
    CHECK_FALSE(s.Tick(0.1f, 30.0f));  // the head on the body
    CHECK_FALSE(s.Tick(0.1f, -1.0f));  // no 3D
    CHECK_FALSE(s.Tick(0.1f, std::numeric_limits<float>::quiet_NaN()));
    int   resets = 0;
    float first = -1.0f;
    for (int i = 1; i <= 200; i++) {
        if (s.Tick(0.1f, 900.0f)) {  // left where it fell
            resets++;
            if (first < 0.0f) {
                first = 0.1f * static_cast<float>(i);
            }
        }
    }
    CHECK(first == doctest::Approx(kStaleFor).epsilon(0.25));
    CHECK(resets == kStaleTries);  // then left alone, not every frame
    CHECK_FALSE(s.Tick(0.1f, 20.0f));  // back on its body: starts over
    CHECK(s.Tries() == 0);
    bool again = false;
    for (int i = 0; i < 10; i++) {
        again = s.Tick(0.1f, 900.0f) || again;
    }
    CHECK(again);  // the next knock-down's skeleton is watched afresh
}

TEST_CASE("a skeleton reset is tried again only after a gap")
{
    Skeleton s;
    for (int i = 0; i < 6; i++) {
        s.Tick(0.1f, 500.0f);
    }
    REQUIRE(s.Tries() == 1);
    for (int i = 0; i < 18; i++) {  // 1.8 s, short of kStaleRetry
        CHECK_FALSE(s.Tick(0.1f, 500.0f));
    }
    bool second = false;
    for (int i = 0; i < 4 && !second; i++) {
        second = s.Tick(0.1f, 500.0f);
    }
    CHECK(second);
}
