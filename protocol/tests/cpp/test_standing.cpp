#include <doctest/doctest.h>

#include <limits>

#include "../../cpp/standing.h"

using namespace AhShitRim::Standing;
using Kind = AhShitRim::VehicleKinds::Kind;

namespace
{
    Frame OnFoot(float a_rise = 0.0f, bool a_grounded = true) { return { true, a_grounded, false, Kind::Unknown, a_rise }; }
    Frame In(Kind a_kind, float a_rise = 0.0f) { return { true, true, true, a_kind, a_rise }; }
}

TEST_CASE("CJ on his feet stands, also walking up or down a slope")
{
    CHECK(Stands(OnFoot()));
    CHECK(Stands(OnFoot(60.0f)));
    CHECK(Stands(OnFoot(-60.0f)));
}

TEST_CASE("jumping, falling or off the ground isn't standing")
{
    CHECK_FALSE(Stands(OnFoot(0.0f, false)));  // mid-jump at its top: not on anything
    CHECK_FALSE(Stands(OnFoot(300.0f)));       // jumping
    CHECK_FALSE(Stands(OnFoot(-500.0f)));      // falling
    CHECK_FALSE(Stands(OnFoot(std::numeric_limits<float>::quiet_NaN())));
    Frame none = OnFoot();
    none.valid = false;
    CHECK_FALSE(Stands(none));  // no pose this handover: the player is left to Skyrim
}

TEST_CASE("in a vehicle on the ground or the water stands; a car in the air, a plane or a helicopter doesn't")
{
    CHECK(Stands(In(Kind::Car)));
    CHECK(Stands(In(Kind::Bike, 40.0f)));
    CHECK(Stands(In(Kind::Boat)));
    CHECK_FALSE(Stands(In(Kind::Car, -800.0f)));  // off a ramp
    CHECK_FALSE(Stands(In(Kind::Plane)));         // level flight has no vertical speed either
    CHECK_FALSE(Stands(In(Kind::Heli)));
    CHECK(Aircraft(Kind::Plane));
    CHECK_FALSE(Aircraft(Kind::Quad));
}
