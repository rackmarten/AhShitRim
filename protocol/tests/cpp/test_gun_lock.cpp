#include <doctest/doctest.h>

#include "../../cpp/gun_lock.h"

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
}

TEST_CASE("the steer closes the gap a share at a time, capped, toward the target")
{
    const auto right = Steer(0.6f, 0.45f, 0.5f, 0.45f, kAspect, 1.0f / 60.0f);
    CHECK(right.x > 0.0);
    CHECK(right.y == doctest::Approx(0.0));
    CHECK(right.x == doctest::Approx(0.1 * kCountsPerScreen * kGain / 60.0).epsilon(0.01));
    const auto below = Steer(0.5f, 0.6f, 0.5f, 0.45f, kAspect, 1.0f / 60.0f);
    CHECK(below.y > 0.0);  // the target below: the mouse moves down
    // A big gap in a long frame: no more than the cap.
    const auto whip = Steer(1.0f, 0.45f, 0.0f, 0.45f, kAspect, 0.05f);
    CHECK(std::hypot(whip.x, whip.y) == doctest::Approx(kMaxCounts * 0.05f).epsilon(0.01));
    // Converges: the remaining gap shrinks each frame when the camera turns by what was sent.
    float target = 0.7f;
    for (int i = 0; i < 120; ++i) {
        const auto c = Steer(target, 0.45f, 0.5f, 0.45f, kAspect, 1.0f / 60.0f);
        target -= static_cast<float>(c.x) / kCountsPerScreen;
    }
    CHECK(target == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("the right stick pushed hard for a moment drops the lock")
{
    Breaker breaker;
    CHECK_FALSE(breaker.Update(0.9f, 0.0f, 0.1f));
    CHECK_FALSE(breaker.Update(0.5f, 0.0f, 0.2f));  // eased off: starts over
    CHECK_FALSE(breaker.Update(0.0f, 0.85f, 0.2f));
    CHECK(breaker.Update(0.0f, 0.85f, 0.1f));
}
