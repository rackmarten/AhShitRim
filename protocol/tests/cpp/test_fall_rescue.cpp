#include <doctest/doctest.h>

#include "../../cpp/fall_rescue.h"

using namespace AhShitRim;
using namespace AhShitRim::FallRescue;

namespace
{
    Around Standing(Vec3 a_feet)
    {
        Around a;
        a.feet = a_feet;
        a.standing = true;
        a.surfaceBelow = true;
        return a;
    }

    Around Under(Vec3 a_feet, float a_aboveZ)
    {
        Around a;
        a.feet = a_feet;
        a.surfaceAbove = true;
        a.aboveZ = a_aboveZ;
        return a;
    }
}

TEST_CASE("standing or falling onto something leaves CJ alone")
{
    Watch w;
    CHECK_FALSE(w.Update(0.1f, Standing({ 0, 0, 500 })).has_value());
    Around jumping;
    jumping.feet = { 0, 0, 501 };
    jumping.surfaceBelow = true;
    for (int i = 0; i < 20; ++i) {
        CHECK_FALSE(w.Update(0.1f, jumping).has_value());
    }
}

TEST_CASE("below the ground, CJ goes back where he last stood once the grace time is over")
{
    Watch w;
    w.Update(0.03f, Standing({ 10, 20, 500 }));
    CHECK_FALSE(w.Update(0.1f, Under({ 11, 20, 499 }, 500.2f)).has_value());
    CHECK_FALSE(w.Update(0.1f, Under({ 11, 20, 498.5f }, 500.2f)).has_value());
    const auto at = w.Update(0.1f, Under({ 11, 20, 498 }, 500.2f));
    REQUIRE(at.has_value());
    CHECK(at->x == 10.0f);
    CHECK(at->z == 500.0f);
}

TEST_CASE("a moment below the ground is forgiven")
{
    Watch w;
    w.Update(0.03f, Standing({ 0, 0, 500 }));
    CHECK_FALSE(w.Update(0.2f, Under({ 0, 0, 499.9f }, 500.0f)).has_value());
    CHECK_FALSE(w.Update(0.03f, Standing({ 0, 0, 500 })).has_value());
    CHECK_FALSE(w.Update(0.2f, Under({ 0, 0, 499.9f }, 500.0f)).has_value());  // the timer started over
}

TEST_CASE("too far from the last spot, only a floor just over the feet catches him, never a ceiling")
{
    Watch w;
    w.Update(0.03f, Standing({ 0, 0, 500 }));
    const auto floor = w.Update(0.3f, Under({ 40, 0, 499.5f }, 500.3f));
    REQUIRE(floor.has_value());
    CHECK(floor->x == 40.0f);
    CHECK(floor->z == 500.3f);

    CHECK_FALSE(w.Update(0.3f, Under({ 40, 0, 490 }, 505.0f)).has_value());  // 15 m up: a ceiling
}

TEST_CASE("with nothing remembered, only the floor just over the feet")
{
    Watch w;
    CHECK(w.Update(0.3f, Under({ 0, 0, 499.5f }, 500.0f)).has_value());
    CHECK_FALSE(w.Update(0.3f, Under({ 0, 0, 490 }, 500.0f)).has_value());
}

TEST_CASE("falling through a hole with nothing above or below goes back to the last spot however far")
{
    Watch w;
    w.Update(0.03f, Standing({ 5, 5, 500 }));
    Around hole;
    hole.feet = { 6, 5, 300 };
    const auto at = w.Update(0.3f, hole);
    REQUIRE(at.has_value());
    CHECK(at->z == 500.0f);

    Watch fresh;
    CHECK_FALSE(fresh.Update(0.3f, hole).has_value());  // nowhere to go
}

TEST_CASE("the remembered spot moves with the origin, and a handover forgets it")
{
    Watch w;
    w.Update(0.03f, Standing({ 1100, 0, 500 }));
    w.Shift({ 1000, 0, 0 });
    const auto at = w.Update(0.3f, Under({ 101, 0, 499 }, 500.5f));
    REQUIRE(at.has_value());
    CHECK(at->x == 100.0f);

    w.Reset();
    CHECK_FALSE(w.HaveStood());
}
