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
