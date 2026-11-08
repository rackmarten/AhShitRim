#include <doctest/doctest.h>

#include "../../cpp/vehicle_lights.h"

#include <cmath>

using namespace AhShitRim;
using namespace AhShitRim::VehicleLights;

TEST_CASE("vehicle lights: Skyrim's time of day becomes San Andreas's clock")
{
    CHECK(MinuteOfDay(0.0f) == 0);
    CHECK(MinuteOfDay(23.0f) == 23 * 60);
    CHECK(MinuteOfDay(20.5f) == 20 * 60 + 30);
    CHECK(MinuteOfDay(23.999f) == 1439);
    CHECK(MinuteOfDay(24.0f) == 0);        // midnight again
    CHECK(MinuteOfDay(30.25f) == 6 * 60 + 15);
    CHECK(MinuteOfDay(-1.0f) == 23 * 60);
    CHECK(MinuteOfDay(NAN) == 0);

    CHECK(ClockOf(23 * 60).hours == 23);
    CHECK(ClockOf(23 * 60).minutes == 0);
    CHECK(ClockOf(6 * 60 + 59).hours == 6);
    CHECK(ClockOf(6 * 60 + 59).minutes == 59);
    CHECK(ClockOf(1440 + 61).hours == 1);
}

TEST_CASE("vehicle lights: Skyrim's sky for San Andreas's weather")
{
    CHECK(SkyOf(false, false) == Protocol::kSkyClock);
    CHECK((SkyOf(true, false) & Protocol::kSkyWet) != 0);
    CHECK((SkyOf(false, true) & Protocol::kSkyDark) != 0);
    CHECK(WetRoadsOf(SkyOf(true, false)) == 1.0f);
    CHECK(WetRoadsOf(SkyOf(false, true)) == 0.0f);  // dry by day, whatever San Andreas's own weather is
}

TEST_CASE("vehicle lights: San Andreas's rule for which lamps burn")
{
    Lamps car;
    car.engineOn = true;
    car.wanted = true;
    car.twin = true;
    car.hasFront = car.hasRear = true;
    const std::uint8_t all = Protocol::kLightFrontRight | Protocol::kLightFrontLeft | Protocol::kLightRearRight | Protocol::kLightRearLeft;
    CHECK(Lit(car) == all);

    // Off by day, on in an interior (the tunnel rule), out with the engine, on a wreck or forced off.
    Lamps day = car;
    day.wanted = false;
    CHECK(Lit(day) == 0);
    day.forceOn = true;
    CHECK(Lit(day) == all);
    Lamps off = car;
    off.engineOn = false;
    CHECK(Lit(off) == 0);
    off = car;
    off.wrecked = true;
    CHECK(Lit(off) == 0);
    off = car;
    off.forceOff = true;
    off.forceOn = true;
    CHECK(Lit(off) == 0);

    // A smashed lamp stays dark, the others burn.
    Lamps smashed = car;
    smashed.smashed[1] = true;  // front left
    smashed.smashed[2] = true;  // rear right
    CHECK(Lit(smashed) == (Protocol::kLightFrontRight | Protocol::kLightRearLeft));

    // A bike has one of each; a BMX, with no light dummies, none.
    Lamps bike = car;
    bike.twin = false;
    CHECK(Lit(bike) == (Protocol::kLightFrontRight | Protocol::kLightRearRight));
    bike.hasFront = bike.hasRear = false;
    CHECK(Lit(bike) == 0);
}

TEST_CASE("vehicle lights: dummies packed in whole Skyrim units")
{
    CHECK(Pack(0.4f) == 0);
    CHECK(Pack(-70.6f) == -71);
    CHECK(Pack(1e9f) == 32767);
    CHECK(Pack(-1e9f) == -32767);
    CHECK(Pack(NAN) == 0);
}

TEST_CASE("vehicle lights: a beam on the road ahead and a red glow behind")
{
    Protocol::BodyLights lights{};
    lights.front[0] = 50;  // 0.7 m right of the middle
    lights.front[1] = 150;
    lights.front[2] = 10;
    lights.rear[0] = 55;
    lights.rear[1] = -160;
    lights.rear[2] = 20;

    Glow glow;
    CHECK_FALSE(EndOf(lights, true, glow));  // nothing lit
    CHECK_FALSE(EndOf(lights, false, glow));

    lights.lit = Protocol::kLightFrontRight | Protocol::kLightFrontLeft | Protocol::kLightRearRight | Protocol::kLightRearLeft;
    REQUIRE(EndOf(lights, true, glow));
    CHECK(glow.at.x == 0.0f);  // between the pair
    CHECK(glow.at.y == doctest::Approx(150.0f + kBeamAhead));
    CHECK(glow.at.z == 10.0f);
    CHECK(glow.radius == kBeamRadius);
    CHECK(glow.colour[2] < glow.colour[0]);  // a warm white

    REQUIRE(EndOf(lights, false, glow));
    CHECK(glow.at.y == doctest::Approx(-160.0f - kTailBehind));
    CHECK(glow.radius == kTailRadius);
    CHECK(glow.colour[0] == 1.0f);
    CHECK(glow.colour[1] < 0.1f);  // red

    // One headlight smashed: the beam comes from the other.
    lights.lit = Protocol::kLightFrontLeft;
    REQUIRE(EndOf(lights, true, glow));
    CHECK(glow.at.x == -50.0f);
    lights.lit = Protocol::kLightFrontRight;
    REQUIRE(EndOf(lights, true, glow));
    CHECK(glow.at.x == 50.0f);
}
