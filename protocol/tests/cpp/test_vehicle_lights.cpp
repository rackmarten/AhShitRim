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

TEST_CASE("vehicle lights: the tail lights burn while braking, by day too")
{
    Lamps car;
    car.engineOn = true;
    car.twin = true;
    car.hasFront = car.hasRear = true;
    CHECK(Lit(car) == 0);
    CHECK(StateOf(car) == 0);
    car.braking = true;
    CHECK(Lit(car) == (Protocol::kLightRearRight | Protocol::kLightRearLeft));
    CHECK(StateOf(car) == Protocol::kLightBraking);
    car.smashed[2] = car.smashed[3] = true;  // both tail lights smashed: nothing brakes red
    CHECK(Lit(car) == 0);
    CHECK(StateOf(car) == 0);
    car.smashed[2] = car.smashed[3] = false;
    car.engineOn = false;
    CHECK(Lit(car) == 0);
    car.engineOn = true;
    car.wanted = true;
    CHECK(Lit(car) == 0xF);
}

TEST_CASE("vehicle lights: San Andreas's lamp materials, each its own lamp")
{
    CHECK(LampOfMaterial(255, 175, 0) == Protocol::kLightFrontLeft);
    CHECK(LampOfMaterial(0, 255, 200) == Protocol::kLightFrontRight);
    CHECK(LampOfMaterial(185, 255, 0) == Protocol::kLightRearLeft);
    CHECK(LampOfMaterial(255, 60, 0) == Protocol::kLightRearRight);
    CHECK(LampOfMaterial(255, 255, 255) == 0);
    CHECK(LampOfMaterial(255, 0, 175) == 0);  // a paint placeholder, not a lamp

    const auto flags = Protocol::kSkinMeshCutout | MeshFlagsOf(Protocol::kLightRearLeft);
    CHECK((flags & Protocol::kSkinMeshCutout) != 0);  // its other flags kept
    CHECK(LampOfMesh(flags) == Protocol::kLightRearLeft);
    CHECK(MeshLit(flags, Protocol::kLightRearLeft | Protocol::kLightFrontRight));
    CHECK_FALSE(MeshLit(flags, Protocol::kLightRearRight));
    CHECK_FALSE(MeshLit(Protocol::kSkinMeshBlend, 0xF));  // glass never lights
    CHECK((MeshFlagsOf(0xF) & ~Protocol::kSkinMeshLamp) == 0);
    CHECK((Protocol::kSkinMeshLamp & (Protocol::kSkinMeshCutout | Protocol::kSkinMeshBlend | Protocol::kSkinMeshAdditive)) == 0);
}

TEST_CASE("vehicle lights: each lamp's light from where it is")
{
    Protocol::BodyLights lights{};
    lights.front[0] = 50;  // 0.7 m right of the middle
    lights.front[1] = 150;
    lights.front[2] = 10;
    lights.rear[0] = 55;
    lights.rear[1] = -160;
    lights.rear[2] = 20;
    const Tuning tuning;

    Glow glow;
    for (int lamp = 0; lamp < 4; lamp++) {
        CHECK_FALSE(LampOf(lights, lamp, tuning, glow));  // nothing lit
    }
    CHECK_FALSE(LampOf(lights, 4, tuning, glow));

    lights.lit = 0xF;
    REQUIRE(LampOf(lights, 0, tuning, glow));  // the right headlight
    CHECK(glow.at.x == 50.0f);
    CHECK(glow.at.y == doctest::Approx(150.0f + tuning.beamAhead));
    CHECK(glow.at.z == doctest::Approx(10.0f + tuning.beamRaise));
    CHECK(glow.radius == tuning.beamRadius);
    CHECK(glow.intensity == tuning.beamIntensity);
    CHECK(glow.colour[2] < glow.colour[0]);  // a warm white
    REQUIRE(LampOf(lights, 1, tuning, glow));  // the left one, mirrored
    CHECK(glow.at.x == -50.0f);

    REQUIRE(LampOf(lights, 2, tuning, glow));  // the right tail light, at its side, not the middle
    CHECK(glow.at.x == 55.0f);
    CHECK(glow.at.y == doctest::Approx(-160.0f - tuning.tailBehind));
    CHECK(glow.at.z == 20.0f);
    CHECK(glow.radius == tuning.tailRadius);
    CHECK(glow.colour[0] == 1.0f);
    CHECK(glow.colour[1] < 0.1f);  // red
    REQUIRE(LampOf(lights, 3, tuning, glow));
    CHECK(glow.at.x == -55.0f);

    // Braking: the tail lights larger and brighter, the headlights as they were.
    lights.state = Protocol::kLightBraking;
    REQUIRE(LampOf(lights, 3, tuning, glow));
    CHECK(glow.radius == tuning.brakeRadius);
    CHECK(glow.intensity == tuning.brakeIntensity);
    CHECK(tuning.brakeIntensity > tuning.tailIntensity);
    REQUIRE(LampOf(lights, 0, tuning, glow));
    CHECK(glow.intensity == tuning.beamIntensity);

    // One headlight smashed: only the other's light.
    lights.lit = Protocol::kLightFrontLeft;
    CHECK_FALSE(LampOf(lights, 0, tuning, glow));
    REQUIRE(LampOf(lights, 1, tuning, glow));
    CHECK(glow.at.x == -50.0f);
}
