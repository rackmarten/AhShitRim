#include <doctest/doctest.h>

#include "../../cpp/gun_lock.h"

#include <cmath>
#include <utility>
#include <vector>

using namespace AhShitRim;
using namespace AhShitRim::GunLock;

namespace
{
    Candidate At(std::uint32_t a_id, float a_x, float a_y, float a_distance, bool a_hostile = false)
    {
        Candidate c;
        c.id = a_id;
        c.screen.x = a_x;
        c.screen.y = a_y;
        c.screen.visible = true;
        c.screen.distance = a_distance;
        c.hostile = a_hostile;
        return c;
    }

    constexpr float kAspect = 16.0f / 9.0f;
}

TEST_CASE("LT picks someone fighting CJ first, then whoever is nearest the crosshair, in range and sight")
{
    std::vector<Candidate> all = { At(1, 0.52f, 0.45f, 10.0f), At(2, 0.62f, 0.5f, 20.0f, true), At(3, 0.5f, 0.45f, 60.0f, true) };
    CHECK(all[Pick(all, 0.5f, 0.45f, kAspect)].id == 2);  // 3 is out of range, 1 isn't fighting
    all[1].inSight = false;
    CHECK(all[Pick(all, 0.5f, 0.45f, kAspect)].id == 1);
    all[0].screen.visible = false;  // behind the camera
    CHECK(Pick(all, 0.5f, 0.45f, kAspect) == all.size());
    // Too far round the screen: not locked on to.
    std::vector<Candidate> wide = { At(4, 0.95f, 0.45f, 10.0f, true) };
    CHECK(Pick(wide, 0.5f, 0.45f, kAspect) == wide.size());
}

TEST_CASE("the d-pad shifts the lock to the nearest one on that side")
{
    std::vector<Candidate> all = { At(1, 0.5f, 0.5f, 10.0f), At(2, 0.7f, 0.5f, 10.0f), At(3, 0.6f, 0.4f, 15.0f), At(4, 0.2f, 0.5f, 10.0f) };
    CHECK(all[Shift(all, 0, 1)].id == 3);
    CHECK(all[Shift(all, 2, 1)].id == 2);
    CHECK(all[Shift(all, 1, 1)].id == 2);   // nobody further right: it stays
    CHECK(all[Shift(all, 0, -1)].id == 4);
    CHECK(all[Shift(all, 0, 0)].id == 1);
    // Just off the screen's edge (the lock turned the camera onto 1): still found; far off it, not.
    std::vector<Candidate> off = { At(1, 0.5f, 0.5f, 10.0f), At(2, 1.3f, 0.5f, 10.0f), At(3, -0.9f, 0.5f, 10.0f) };
    CHECK(off[Shift(off, 0, 1)].id == 2);
    CHECK(off[Shift(off, 0, -1)].id == 1);
}

TEST_CASE("the steer closes the gap a share at a time, capped, toward the target")
{
    const auto right = Steer(0.6f, 0.45f, 0.5f, 0.45f, kAspect, 1.0f / 60.0f);
    CHECK(right.x > 0.0);
    CHECK(right.y == doctest::Approx(0.0));
    CHECK(right.x == doctest::Approx(0.1 * kCountsPerWidth * kGain / 60.0).epsilon(0.01));
    const auto below = Steer(0.5f, 0.6f, 0.5f, 0.45f, kAspect, 1.0f / 60.0f);
    CHECK(below.y > 0.0);  // the target below: the mouse moves down
    CHECK(below.y == doctest::Approx(0.15 * kCountsPerHeight * kGain / 60.0).epsilon(0.01));
    // A long frame closes no more than kMaxShare of the gap.
    const auto slow = Steer(0.6f, 0.45f, 0.5f, 0.45f, kAspect, 0.1f);
    CHECK(slow.x == doctest::Approx(0.1 * kCountsPerWidth * kMaxShare).epsilon(0.01));
    // A target a screen width away: no more than the cap.
    const auto whip = Steer(1.0f, 0.45f, 0.0f, 0.45f, kAspect, 1.0f / 60.0f);
    CHECK(std::hypot(whip.x, whip.y) == doctest::Approx(kMaxCounts / 60.0f).epsilon(0.01));
}

namespace
{
    // The lock's loop through San Andreas and back: the counts sent show in the camera a_lag frames later, turning it
    // a_sensitivity times what Steer expects. Returns the largest gap over the last of a_seconds, and the final one.
    struct Settled
    {
        float worst = 0.0f;
        float last = 0.0f;
    };
    Settled Simulate(float a_fps, int a_lag, float a_sensitivity, float a_gapX, float a_gapY, float a_seconds = 3.0f)
    {
        const float                          dt = 1.0f / a_fps;
        std::vector<Counts>                  inFlight(static_cast<std::size_t>(a_lag));
        float                                tx = 0.5f + a_gapX, ty = 0.4f + a_gapY;
        Settled                              out;
        const int                            frames = static_cast<int>(a_seconds * a_fps);
        for (int i = 0; i < frames; i++) {
            inFlight.push_back(Steer(tx, ty, 0.5f, 0.4f, kAspect, dt));
            const auto seen = inFlight.front();
            inFlight.erase(inFlight.begin());
            tx -= static_cast<float>(seen.x) * a_sensitivity / kCountsPerWidth;  // the camera turns right: the target moves left
            ty -= static_cast<float>(seen.y) * a_sensitivity / kCountsPerHeight;
            const float gap = std::hypot((tx - 0.5f) * kAspect, ty - 0.4f);
            if (i >= frames - static_cast<int>(a_fps)) {
                out.worst = (std::max)(out.worst, gap);
            }
            out.last = gap;
        }
        return out;
    }
}

TEST_CASE("the steer settles with San Andreas's lag, at any frame rate, up to twice the measured sensitivity")
{
    // In game the camera pitched up and down ever harder with the lock on (a gain of 9 a second, 500 counts a screen):
    // 3 or 4 frames of lag made the loop overshoot further each time.
    for (const float fps : { 30.0f, 60.0f, 144.0f }) {
        for (const int lag : { 1, 2, 3, 4, static_cast<int>(std::ceil(0.067f * fps)) }) {
            for (const float sensitivity : { 0.5f, 1.0f, 2.0f }) {
                for (const auto gap : { std::pair{ 0.1f, 0.15f }, std::pair{ -0.4f, -0.3f } }) {
                    const auto s = Simulate(fps, lag, sensitivity, gap.first, gap.second);
                    CAPTURE(fps);
                    CAPTURE(lag);
                    CAPTURE(sensitivity);
                    CAPTURE(gap.first);
                    CHECK(s.worst < 0.03f);  // settled in the last second, not swinging
                    CHECK(s.last < 0.02f);
                }
            }
        }
    }
    // Without the lag the gap is closed within about half a second.
    CHECK(Simulate(60.0f, 0, 1.0f, 0.1f, 0.15f, 0.6f).last < 0.01f);
}

TEST_CASE("the right stick pushed hard for a moment drops the lock")
{
    Breaker breaker;
    CHECK_FALSE(breaker.Update(0.9f, 0.0f, 0.1f));
    CHECK_FALSE(breaker.Update(0.5f, 0.0f, 0.2f));  // eased off: starts over
    CHECK_FALSE(breaker.Update(0.0f, 0.85f, 0.2f));
    CHECK(breaker.Update(0.0f, 0.85f, 0.1f));
}

TEST_CASE("a moment out of sight keeps the lock; longer moves it on")
{
    Grace grace;
    CHECK(grace.Keep(true, 0.1f));
    CHECK(grace.Keep(false, 0.2f));
    CHECK(grace.Keep(false, 0.2f));
    CHECK(grace.Keep(true, 0.1f));   // back in sight: starts over
    CHECK(grace.Keep(false, 0.4f));
    CHECK_FALSE(grace.Keep(false, 0.2f));
}
