#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "../../cpp/air_spawn.h"

using namespace AhShitRim;
using namespace AhShitRim::AirSpawn;

TEST_CASE("air spawn: the planes come flying, not the Vortex, the helicopters or the cars")
{
    for (std::uint16_t plane : { 460, 476, 511, 512, 513, 519, 520, 553, 577, 592, 593 }) {  // Skimmer, Rustler, Beagle, Cropduster, Stuntplane, Shamal, Hydra, Nevada, AT-400, Andromada, Dodo
        CAPTURE(plane);
        CHECK(Flies(plane));
    }
    CHECK_FALSE(Flies(kVortex));
    CHECK_FALSE(Flies(487));  // Maverick
    CHECK_FALSE(Flies(425));  // Hunter
    CHECK_FALSE(Flies(411));  // Infernus
    CHECK_FALSE(Flies(468));  // Sanchez
    CHECK_FALSE(Flies(VehicleKinds::kJetpack));
}

TEST_CASE("air spawn: launched a little over the speed that holds it up")
{
    // San Andreas 1.0's handling.cfg fFormLift.
    const float at400 = LaunchSpeed(0.008f);
    const float dodo = LaunchSpeed(0.020f);
    const float cropduster = LaunchSpeed(0.030f);
    CHECK(at400 == doctest::Approx(50.0f * kMargin));  // level at 1 m a step: 50 m/s
    CHECK(dodo < at400);  // a light plane flies slower
    CHECK(dodo > std::sqrt(kGravityStep / 0.020f) * kStepsPerSecond);
    CHECK(cropduster >= kMinSpeed);
    // The lift at that speed beats gravity.
    const float step = at400 / kStepsPerSecond;
    CHECK(0.008f * step * step > kGravityStep);
    // Kept sensible.
    CHECK(LaunchSpeed(1e-6f) == kMaxSpeed);
    CHECK(LaunchSpeed(100.0f) == kMinSpeed);
    CHECK(LaunchSpeed(0.0f) == kFallbackSpeed);
    CHECK(LaunchSpeed(-1.0f) == kFallbackSpeed);
    CHECK(LaunchSpeed(std::numeric_limits<float>::quiet_NaN()) == kFallbackSpeed);
}

TEST_CASE("air spawn: facing where the camera looks")
{
    // San Andreas's forward for a heading h is (-sin h, cos h).
    for (const float h : { 0.0f, 0.5f, -1.2f, 2.8f }) {
        CAPTURE(h);
        const float back = HeadingOf(-std::sin(h), std::cos(h), 9.0f);
        CHECK(-std::sin(back) == doctest::Approx(-std::sin(h)));
        CHECK(std::cos(back) == doctest::Approx(std::cos(h)));
    }
    CHECK(HeadingOf(0.0f, 0.0f, 1.5f) == 1.5f);  // straight down: the fallback
}
