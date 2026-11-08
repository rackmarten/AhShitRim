#include <doctest/doctest.h>

#include <limits>

#include "../../cpp/look_sensitivity.h"

TEST_CASE("look sensitivity: Skyrim's default leaves the counts as they are")
{
    CHECK(LookSensitivity::Mouse(LookSensitivity::kMouseDefault) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Pad(LookSensitivity::kPadDefault) == doctest::Approx(1.0));
}

TEST_CASE("look sensitivity: the counts follow Skyrim's live value")
{
    CHECK(LookSensitivity::Mouse(0.0100f) == doctest::Approx(0.8));   // a slider a fifth down turns a fifth slower
    CHECK(LookSensitivity::Mouse(0.0250f) == doctest::Approx(2.0));
    CHECK(LookSensitivity::Pad(0.3333f) == doctest::Approx(0.5).epsilon(0.001));
    CHECK(LookSensitivity::Pad(1.3334f) == doctest::Approx(2.0).epsilon(0.001));
    CHECK(150.0 * LookSensitivity::Mouse(0.0100f) == doctest::Approx(120.0));
}

TEST_CASE("look sensitivity: a missing or stray value can't freeze or spin the camera")
{
    CHECK(LookSensitivity::Mouse(0.0f) == 1.0);
    CHECK(LookSensitivity::Mouse(-0.01f) == 1.0);
    CHECK(LookSensitivity::Mouse(std::numeric_limits<float>::quiet_NaN()) == 1.0);
    CHECK(LookSensitivity::Mouse(0.0001f) == LookSensitivity::kMinScale);
    CHECK(LookSensitivity::Mouse(5.0f) == LookSensitivity::kMaxScale);
    CHECK(LookSensitivity::Mouse(std::numeric_limits<float>::infinity()) == LookSensitivity::kMaxScale);
    CHECK(LookSensitivity::Scale(0.5f, 0.0f) == 1.0);
}
