#include <doctest/doctest.h>

#include <limits>

#include "../../cpp/settle.h"

using namespace AhShitRim::Settle;

namespace
{
    Frame Fine(float a_delta = 0.1f) { return { true, false, false, true, true, false, a_delta }; }

    // Ticks a_gate with a_frame for a_seconds; returns after how long it opened, or -1.
    float OpensAfter(Gate& a_gate, Frame a_frame, float a_seconds)
    {
        float t = 0.0f;
        while (t < a_seconds - 1e-4f) {
            t += a_frame.delta;
            if (a_gate.Tick(a_frame)) {
                return t;
            }
        }
        return -1.0f;
    }
}

TEST_CASE("the handover waits for a couple of seconds of normal frames after a load, then comes once")
{
    Gate gate;
    CHECK_FALSE(gate.Tick(Fine()));  // not armed: no load, nothing to resume
    gate.Start();
    CHECK(OpensAfter(gate, Fine(), 10.0f) == doctest::Approx(kSeconds).epsilon(0.06));
    CHECK_FALSE(gate.Armed());
    CHECK(OpensAfter(gate, Fine(), 10.0f) < 0.0f);  // once
}

TEST_CASE("the loading screen, a pause, the player's 3D or cell not in, the link down: the wait starts over")
{
    Frame bad[6] = { Fine(), Fine(), Fine(), Fine(), Fine(), Fine() };
    bad[0].linkUp = false;
    bad[1].loadingMenu = true;
    bad[2].paused = true;
    bad[3].player3D = false;
    bad[4].cellAttached = false;
    bad[5].busy = true;
    for (const auto& frame : bad) {
        CHECK_FALSE(Normal(frame));
        Gate gate;
        gate.Start();
        CHECK(OpensAfter(gate, frame, 10.0f) < 0.0f);
    }
    // Normal for a while, then the loading menu again: the count starts from zero.
    Gate gate;
    gate.Start();
    CHECK(OpensAfter(gate, Fine(), kSeconds - 0.5f) < 0.0f);
    CHECK_FALSE(gate.Tick(bad[1]));
    CHECK(gate.NormalSeconds() == 0.0f);
    CHECK(OpensAfter(gate, Fine(), kSeconds + 0.5f) == doctest::Approx(kSeconds).epsilon(0.06));
}

TEST_CASE("a frame with no sensible time doesn't count")
{
    Gate gate;
    gate.Start();
    CHECK_FALSE(gate.Tick(Fine(std::numeric_limits<float>::quiet_NaN())));
    CHECK_FALSE(gate.Tick(Fine(-1.0f)));
    CHECK(gate.NormalSeconds() == 0.0f);
    gate.Reset();
    CHECK_FALSE(gate.Armed());
}
