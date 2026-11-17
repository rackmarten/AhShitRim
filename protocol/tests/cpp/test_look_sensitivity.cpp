#include <doctest/doctest.h>

#include <limits>

#include "../../cpp/look_sensitivity.h"

using LookSensitivity::kMouseRange;
using LookSensitivity::kPadRange;

TEST_CASE("look sensitivity: the slider's middle is San Andreas's own speed, its ends a quarter and four times it")
{
    CHECK(LookSensitivity::Mouse(LookSensitivity::Middle(kMouseRange)) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Pad(LookSensitivity::Middle(kPadRange)) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Middle(kMouseRange) == doctest::Approx(0.03f));
    CHECK(LookSensitivity::Mouse(kMouseRange.min) == doctest::Approx(0.25));
    CHECK(LookSensitivity::Mouse(kMouseRange.max) == doctest::Approx(4.0));
    CHECK(LookSensitivity::Pad(kPadRange.min) == doctest::Approx(0.25));
    CHECK(LookSensitivity::Pad(kPadRange.max) == doctest::Approx(4.0));
    CHECK(LookSensitivity::Mouse(0.0200f) == doctest::Approx(0.5));  // a quarter of the way up: half speed
    CHECK(LookSensitivity::Mouse(0.0400f) == doctest::Approx(2.0));  // three quarters: twice
}

TEST_CASE("look sensitivity: the slider is even, each tenth of it the same step faster")
{
    for (const auto range : { kMouseRange, kPadRange }) {
        const auto at = [&](double a_t) { return static_cast<float>(range.min + a_t * (range.max - range.min)); };
        const double step = LookSensitivity::Scale(at(0.1), range) / LookSensitivity::Scale(at(0.0), range);
        CHECK(step == doctest::Approx(1.32).epsilon(0.01));  // about a third faster
        for (int i = 1; i < 10; ++i) {
            CHECK(LookSensitivity::Scale(at((i + 1) / 10.0), range) / LookSensitivity::Scale(at(i / 10.0), range) ==
                  doctest::Approx(step).epsilon(0.001));
        }
    }
}

TEST_CASE("look sensitivity: Skyrim's own default sits low, with room below the middle either way")
{
    // The defaults turn CJ about a third (mouse) and under a half (pad) of San Andreas's speed: the reason for the reset.
    CHECK(LookSensitivity::Position(LookSensitivity::kMouseDefault, kMouseRange) == doctest::Approx(0.0625));
    CHECK(LookSensitivity::Position(LookSensitivity::kPadDefault, kPadRange) == doctest::Approx(0.2).epsilon(0.001));
    CHECK(LookSensitivity::Mouse(LookSensitivity::kMouseDefault) == doctest::Approx(0.297).epsilon(0.002));
    CHECK(LookSensitivity::Pad(LookSensitivity::kPadDefault) == doctest::Approx(0.435).epsilon(0.002));
}

TEST_CASE("look sensitivity: a missing or stray value can't freeze or spin the camera")
{
    CHECK(LookSensitivity::Mouse(0.0f) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Mouse(-0.01f) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Mouse(std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Mouse(0.0001f) == doctest::Approx(LookSensitivity::kSlowest));  // below the slider (an edited ini)
    CHECK(LookSensitivity::Mouse(5.0f) == doctest::Approx(LookSensitivity::kFastest));
    CHECK(LookSensitivity::Mouse(std::numeric_limits<float>::infinity()) == doctest::Approx(LookSensitivity::kFastest));
    CHECK(LookSensitivity::Scale(0.02f, { 0.0f, 0.0f }) == doctest::Approx(1.0));
    CHECK(LookSensitivity::Scale(0.02f, { 0.05f, 0.01f }) == doctest::Approx(1.0));
}

TEST_CASE("look sensitivity: the slider's live ends are used when they make sense")
{
    const LookSensitivity::Range wide{ 0.005f, 0.1f };
    CHECK(LookSensitivity::Checked(wide, kMouseRange).max == wide.max);
    CHECK(LookSensitivity::Checked({ 0.0f, 0.0f }, kMouseRange).max == kMouseRange.max);
    CHECK(LookSensitivity::Checked({ 0.02f, 0.01f }, kMouseRange).min == kMouseRange.min);
    CHECK(LookSensitivity::Checked({ -1.0f, 0.05f }, kMouseRange).min == kMouseRange.min);
    CHECK(LookSensitivity::Checked({ 0.01f, std::numeric_limits<float>::infinity() }, kMouseRange).max == kMouseRange.max);
    CHECK(LookSensitivity::Checked({ 0.01f, std::numeric_limits<float>::quiet_NaN() }, kMouseRange).max == kMouseRange.max);
}

TEST_CASE("look sensitivity: the one-time reset is due until it's recorded")
{
    CHECK(LookSensitivity::ResetDue(""));
    CHECK(LookSensitivity::ResetDue("0"));
    CHECK(LookSensitivity::ResetDue("yes"));
    CHECK_FALSE(LookSensitivity::ResetDue("1"));
}
