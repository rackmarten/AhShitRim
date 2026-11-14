#include <doctest/doctest.h>

#include "../../cpp/vehicle_lights.h"

#include <cmath>
#include <string>

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

TEST_CASE("vehicle lights: one light per end, clear of the car")
{
    // The ZR-350's dummies (San Andreas's DFF: headlights 0.71, 2.41, 0.16 m; taillights 0.80, -2.76, 0.11 m).
    Protocol::BodyLights lights{};
    lights.front[0] = 51;
    lights.front[1] = 172;
    lights.front[2] = 11;
    lights.rear[0] = 57;
    lights.rear[1] = -197;
    lights.rear[2] = 8;
    const Tuning tuning;

    Glow glow;
    CHECK_FALSE(LightOf(lights, 0, tuning, glow));  // nothing lit
    CHECK_FALSE(LightOf(lights, 1, tuning, glow));
    CHECK_FALSE(LightOf(lights, 2, tuning, glow));

    const auto distance = [](const Glow& a_glow, float a_x, float a_y, float a_z) {
        return std::sqrt((a_glow.at.x - a_x) * (a_glow.at.x - a_x) + (a_glow.at.y - a_y) * (a_glow.at.y - a_y) + (a_glow.at.z - a_z) * (a_glow.at.z - a_z));
    };

    lights.lit = 0xF;
    REQUIRE(LightOf(lights, 0, tuning, glow));  // both headlights: one light, midway between them
    CHECK(glow.at.x == 0.0f);
    CHECK(glow.at.y >= 172.0f + tuning.beamAhead);
    CHECK(glow.at.z == doctest::Approx(11.0f + tuning.beamRaise));
    CHECK(glow.radius == tuning.beamRadius);
    CHECK(glow.intensity == tuning.beamIntensity);
    CHECK(glow.colour[2] < glow.colour[0]);  // a warm white
    // Its reach ends short of either lamp, so the car's front isn't lit by it.
    CHECK(distance(glow, 51.0f, 172.0f, 11.0f) >= glow.radius + kLampClear - 0.01f);
    CHECK(distance(glow, -51.0f, 172.0f, 11.0f) >= glow.radius + kLampClear - 0.01f);
    // A larger beam goes further out, rather than reaching back over the car.
    Tuning wide = tuning;
    wide.beamRadius = 12.0f * Pose::kUnitsPerMeter;
    Glow far;
    REQUIRE(LightOf(lights, 0, wide, far));
    CHECK(distance(far, 51.0f, 172.0f, 11.0f) >= far.radius + kLampClear - 0.01f);

    REQUIRE(LightOf(lights, 1, tuning, glow));  // both tail lights: one red light behind, below them
    CHECK(glow.at.x == 0.0f);
    CHECK(glow.at.y == doctest::Approx(-197.0f - tuning.tailBack));  // just behind the car: the road there reddens
    CHECK(glow.at.z == doctest::Approx(8.0f - tuning.tailDrop));
    CHECK(glow.radius == tuning.tailRadius);
    CHECK(glow.colour[0] == 1.0f);
    CHECK(glow.colour[1] < 0.1f);  // red
    // It reaches the road under the tail lights' height (about 0.6 m below them on a car), and the lamps themselves.
    CHECK(glow.radius > tuning.tailDrop + 0.3f * Pose::kUnitsPerMeter);

    // Braking: the tail light larger, brighter and further out; the headlights' as it was.
    lights.state = Protocol::kLightBraking;
    Glow brake;
    REQUIRE(LightOf(lights, 1, tuning, brake));
    CHECK(brake.radius == tuning.brakeRadius);
    CHECK(brake.intensity == tuning.brakeIntensity);
    CHECK(tuning.brakeIntensity > tuning.tailIntensity);
    CHECK(brake.at.y == glow.at.y);  // in the same place, reaching further
    REQUIRE(LightOf(lights, 0, tuning, glow));
    CHECK(glow.intensity == tuning.beamIntensity);

    // One headlight smashed: the light at the other one.
    lights.lit = Protocol::kLightFrontLeft | Protocol::kLightRearRight;
    REQUIRE(LightOf(lights, 0, tuning, glow));
    CHECK(glow.at.x == -51.0f);
    REQUIRE(LightOf(lights, 1, tuning, glow));
    CHECK(glow.at.x == 57.0f);

    // A bike: its lamps in the middle, the right one only.
    Protocol::BodyLights bike{};
    bike.front[1] = 44;
    bike.front[2] = 24;
    bike.rear[1] = -67;
    bike.rear[2] = 25;
    bike.lit = Protocol::kLightRearRight;
    CHECK_FALSE(LightOf(bike, 0, tuning, glow));
    REQUIRE(LightOf(bike, 1, tuning, glow));
    CHECK(glow.at.x == 0.0f);
}

TEST_CASE("vehicle lights: a burning lamp's mesh glows, brighter while braking")
{
    const Tuning         tuning;
    Protocol::BodyLights lights{};
    CHECK(LampGlowOf(Protocol::kLightFrontRight, lights, tuning) == 0.0f);
    lights.lit = 0xF;
    CHECK(LampGlowOf(Protocol::kLightFrontRight, lights, tuning) == tuning.lampGlow);
    CHECK(LampGlowOf(Protocol::kLightRearLeft, lights, tuning) == tuning.lampGlow);
    lights.state = Protocol::kLightBraking;
    CHECK(LampGlowOf(Protocol::kLightRearLeft, lights, tuning) == doctest::Approx(tuning.lampGlow * tuning.brakeGlow));
    CHECK(LampGlowOf(Protocol::kLightFrontLeft, lights, tuning) == tuning.lampGlow);
    lights.lit = Protocol::kLightRearRight;  // by day, braking: the tail lights only
    CHECK(LampGlowOf(Protocol::kLightFrontLeft, lights, tuning) == 0.0f);
    CHECK(LampGlowOf(Protocol::kLightRearRight, lights, tuning) > 0.0f);
    CHECK(std::string(LampName(Protocol::kLightRearLeft)) == "rear left");
}

TEST_CASE("vehicle lights: the lights-on texture lifted toward full, its hue kept")
{
    // A grey headlight, the ZR-350's dark core, a tail light's red, and full white.
    std::uint8_t pixels[] = { 128, 128, 128, 200, 10, 10, 10, 255, 200, 20, 20, 255, 255, 255, 255, 255 };
    Glowing(pixels, 4);
    CHECK(pixels[0] == pixels[1]);
    CHECK(pixels[0] > 210);       // grey goes bright
    CHECK(pixels[3] == 200);      // alpha kept
    CHECK(pixels[4] > 170);       // the dark core no longer dark
    CHECK(pixels[8] > 235);       // red brighter
    CHECK(pixels[9] < 30);        // and still red
    CHECK(pixels[12] == 255);
}
