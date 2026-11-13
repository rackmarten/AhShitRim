// A vehicle's headlights and tail lights (protocol v36), the game-free half. San Andreas switches them by its
// own clock and weather, so Skyrim sends its time of day and sky (InputState.minuteOfDay, sky), San Andreas
// runs its own rule on them, and each vehicle slot carries the model's light dummies and the lamps burning
// now (BodyLights). Skyrim lights the scene there with point lights. Header-only and free of game types, so
// the protocol tests cover it.
//
// San Andreas's rule (gta-reversed, Entity/Vehicle/Vehicle.cpp):
//  - CVehicle::GetVehicleLightsStatus (0x6D55C0): on from 21:00 to 6:00, from 20:xx past the vehicle's own
//    minute (its random seed % 64) and on 6:xx until it, in fog or on wet roads (CWeather::Foggyness or
//    WetRoads over seed / 50000), and in a dark tunnel. San Andreas's own function is called, on the clock and
//    wet roads Skyrim sent, with the ground under the vehicle counted as lit (Skyrim's has no lighting); an
//    interior stands in for the tunnel (FORCE_CAR_LIGHTS_ON, which also raises the ZR-350's pop-up lamps).
//  - CVehicle::DoVehicleLights (0x6E1A60): nothing with the engine off; FORCE_CAR_LIGHTS_OFF puts them out
//    and FORCE_CAR_LIGHTS_ON lights them; then each headlight and tail light burns unless it is smashed (a
//    car's damage manager; a bike's never break). A car has a pair of each, mirrored across its middle, a
//    bike one of each. A wreck shows none. The tail lights also burn while the driver brakes, by day too.
//  - The lamps' meshes are drawn lit while their lamp burns (LampOfMaterial, Glowing, LampGlowOf); each end's
//    light comes from where its lamps are (LightOf).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../ahshitrim_protocol.h"
#include "pose.h"

namespace AhShitRim::VehicleLights
{
    using Pose::Vec3;

    // ---- Skyrim's sky, for San Andreas's clock and weather ----

    inline constexpr std::uint16_t kMinutesPerDay = 24 * 60;

    // Skyrim's GameHour (0..24, the minutes as its fraction) as the minute of the day.
    inline std::uint16_t MinuteOfDay(float a_gameHour)
    {
        if (!std::isfinite(a_gameHour)) {
            return 0;
        }
        float hour = std::fmod(a_gameHour, 24.0f);
        if (hour < 0.0f) {
            hour += 24.0f;
        }
        const int minute = static_cast<int>(std::floor(hour * 60.0f));
        return static_cast<std::uint16_t>(std::clamp(minute, 0, kMinutesPerDay - 1));
    }

    struct Clock
    {
        std::uint8_t hours = 0;
        std::uint8_t minutes = 0;
    };

    constexpr Clock ClockOf(std::uint16_t a_minuteOfDay)
    {
        const auto minute = static_cast<std::uint16_t>(a_minuteOfDay % kMinutesPerDay);
        return { static_cast<std::uint8_t>(minute / 60), static_cast<std::uint8_t>(minute % 60) };
    }

    // InputState.sky: the clock is sent; rain or snow (San Andreas's wet roads); an interior (its dark tunnels).
    constexpr std::uint8_t SkyOf(bool a_wet, bool a_interior)
    {
        return static_cast<std::uint8_t>(Protocol::kSkyClock | (a_wet ? Protocol::kSkyWet : 0) | (a_interior ? Protocol::kSkyDark : 0));
    }

    // San Andreas's CWeather::WetRoads while its lights are worked out: full in Skyrim's rain or snow, dry otherwise
    // (its own random weather would switch them on by day). Foggyness is put to 0: Skyrim's weathers say nothing of fog.
    constexpr float WetRoadsOf(std::uint8_t a_sky) { return (a_sky & Protocol::kSkyWet) != 0 ? 1.0f : 0.0f; }

    // ---- Which lamps burn (San Andreas's side) ----

    struct Lamps
    {
        bool engineOn = false;
        bool wrecked = false;
        bool wanted = false;    // GetVehicleLightsStatus: night, wet roads
        bool forceOn = false;   // m_nOverrideLights FORCE_CAR_LIGHTS_ON (an interior)
        bool forceOff = false;  // FORCE_CAR_LIGHTS_OFF
        bool braking = false;   // a driver on the brake, the handbrake off (DoLightEffectImpl's brake lights)
        bool twin = false;      // a pair of each, mirrored (CAutomobile and what derives from it: cars, quads, aircraft)
        bool hasFront = false;  // the model has a headlight dummy
        bool hasRear = false;   // and a tail light dummy
        bool smashed[4] = {};   // in kLight order: front right, front left, rear right, rear left
    };

    inline constexpr std::uint8_t kLampBits[4] = { Protocol::kLightFrontRight, Protocol::kLightFrontLeft, Protocol::kLightRearRight, Protocol::kLightRearLeft };

    // The headlights burn with the lights on; the tail lights with them, and while braking (by day too), as
    // DoVehicleLights draws them (m_renderLights).
    constexpr std::uint8_t Lit(const Lamps& a_lamps)
    {
        if (!a_lamps.engineOn || a_lamps.wrecked || a_lamps.forceOff) {
            return 0;
        }
        const bool on = a_lamps.wanted || a_lamps.forceOn;
        std::uint8_t lit = 0;
        for (int i = 0; i < 4; i++) {
            const bool front = i < 2;
            const bool mirror = i % 2 == 1;
            if ((front ? on : on || a_lamps.braking) && (front ? a_lamps.hasFront : a_lamps.hasRear) && (!mirror || a_lamps.twin) && !a_lamps.smashed[i]) {
                lit |= kLampBits[i];
            }
        }
        return lit;
    }

    // BodyLights.state: kLightBraking while the tail lights burn for the brake.
    constexpr std::uint8_t StateOf(const Lamps& a_lamps)
    {
        return a_lamps.braking && (Lit(a_lamps) & (Protocol::kLightRearRight | Protocol::kLightRearLeft)) != 0 ? Protocol::kLightBraking : 0;
    }

    // ---- The lamps' meshes ----

    // San Andreas's lamp materials are the lights texture's (vehiclelights128) in a placeholder colour per lamp, and draw
    // lit (vehiclelightson128, full ambient) while the lamp burns (CVehicleModelInfo::SetEditableMaterialsCB with
    // SetupLightFlags' ms_lightsOn): (255, 175, 0) the left headlight, (0, 255, 200) the right one, (185, 255, 0) the
    // left tail light, (255, 60, 0) the right one. Which lamp a material of the lights texture is, as a kLight bit; 0
    // for none (a material of that texture in another colour never lights).
    constexpr std::uint8_t LampOfMaterial(std::uint8_t a_r, std::uint8_t a_g, std::uint8_t a_b)
    {
        if (a_r == 255 && a_g == 175 && a_b == 0) {
            return Protocol::kLightFrontLeft;
        }
        if (a_r == 0 && a_g == 255 && a_b == 200) {
            return Protocol::kLightFrontRight;
        }
        if (a_r == 185 && a_g == 255 && a_b == 0) {
            return Protocol::kLightRearLeft;
        }
        if (a_r == 255 && a_g == 60 && a_b == 0) {
            return Protocol::kLightRearRight;
        }
        return 0;
    }

    // A mesh's flags (Protocol::kSkinMesh*) for that lamp, and whether a mesh with a_flags is drawn lit while a_lit burn.
    constexpr std::uint32_t MeshFlagsOf(std::uint8_t a_lamp) { return static_cast<std::uint32_t>(a_lamp) << Protocol::kSkinMeshLampShift; }

    constexpr std::uint8_t LampOfMesh(std::uint32_t a_flags) { return static_cast<std::uint8_t>((a_flags & Protocol::kSkinMeshLamp) >> Protocol::kSkinMeshLampShift); }

    constexpr bool MeshLit(std::uint32_t a_flags, std::uint8_t a_lit) { return (LampOfMesh(a_flags) & a_lit) != 0; }

    // A dummy in Skyrim units, as BodyLights carries it (whole units: 1.4 cm, up to 468 m out).
    inline std::int16_t Pack(float a_units)
    {
        if (!std::isfinite(a_units)) {
            return 0;
        }
        return static_cast<std::int16_t>(std::clamp(std::lround(a_units), -32767l, 32767l));
    }

    // ---- How a burning lamp's mesh looks (Skyrim's side) ----

    // San Andreas's lights-on texture is only a little brighter than the unlit one (the ZR-350's pop-up glass keeps a dark
    // core), and San Andreas makes its lamps look lit with the corona it draws over each. Skyrim draws no corona, so a
    // burning lamp's mesh shows the lights-on texture lifted toward full brightness, its hue kept (a tail light stays red),
    // glowing by itself (LampGlow), solid (the Sandking's headlight glass is half see-through) and twice as bright while
    // braking (BrakeGlow).
    inline constexpr float kGlowLift = 0.7f;  // how far each pixel's brightest channel goes toward full

    inline void Glowing(std::uint8_t* a_rgba, std::size_t a_pixels)
    {
        for (std::size_t i = 0; i < a_pixels; i++) {
            std::uint8_t* p = a_rgba + i * 4;
            const int     top = (std::max)({ p[0], p[1], p[2] });
            const float   lifted = static_cast<float>(top) + (255.0f - static_cast<float>(top)) * kGlowLift;
            for (int c = 0; c < 3; c++) {
                // A grey (or black) pixel lifts as grey; a coloured one keeps its channels' ratio.
                const float value = top > 0 ? static_cast<float>(p[c]) * lifted / static_cast<float>(top) : lifted;
                p[c] = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
            }
        }
    }

    // ---- The light they throw (Skyrim's side) ----

    // Skyrim has no shadowless spot light, so the headlights' light on the road is a point light out ahead of them and a
    // little up, lighting the ground as San Andreas's light pool does (DoHeadLightReflection's StoreCarLightShadow: about
    // 7 m ahead of the lamps and 13 m long), and the tail lights' a small red one behind, larger and brighter while
    // braking (San Andreas adds a red light behind the car then).
    //  - One light per end, not per lamp: Skyrim lights each mesh (the ground under the car among them) with about 4
    //    dynamic lights, so 4 per vehicle crowded each other out (one tail light lit, or none beside a second vehicle).
    //    It sits midway between a pair, or at the one lamp burning (a bike's, or a pair with one smashed).
    //  - Far enough out that its reach ends kLampClear short of the lamps: the car itself isn't lit by its own lights
    //    (a light-blue car lit up all over from in front, the plate glowing pink).
    // The numbers are a matter of taste, so AhShitRim.ini's [Headlights] may change them (skse/src/player/Headlights.cpp).
    inline constexpr float kLampClear = 0.3f * Pose::kUnitsPerMeter;

    struct Tuning
    {
        // Set from the test kit's night shots in Riverwood (scenarios/vehicle-lights*.json): Skyrim's moonlit road is
        // already bright and blue, so a pool the player clearly sees takes several times a torch's light, warm white.
        float beamAhead = 6.0f * Pose::kUnitsPerMeter;   // at least this far ahead of the headlights
        float beamRaise = 0.8f * Pose::kUnitsPerMeter;   // above them, so the pool spreads over the road
        float beamRadius = 10.0f * Pose::kUnitsPerMeter;
        float beamIntensity = 10.0f;                      // the point light's fade: Skyrim's torch is about 1
        float tailDrop = 0.45f * Pose::kUnitsPerMeter;   // below the tail lights, toward the road behind
        float tailRadius = 2.2f * Pose::kUnitsPerMeter;
        float tailIntensity = 5.0f;
        float brakeRadius = 3.0f * Pose::kUnitsPerMeter;
        float brakeIntensity = 10.0f;
        float lampGlow = 6.0f;   // a burning lamp's mesh and its corona: their emissive strength (Skyrim's shader)
        float brakeGlow = 2.0f;  // a tail lamp's, times this while braking
    };

    struct Glow
    {
        Vec3  at;  // in the vehicle's frame (X right, Y forward, Z up), Skyrim units
        float radius = 0.0f;
        float intensity = 0.0f;
        float colour[3] = {};
    };

    // How far ahead of (behind) the lamps a light of a_radius sits, a_up above (below) them, so its reach ends
    // kLampClear short of them, and at least a_least.
    inline float OutFrom(float a_radius, float a_up, float a_least)
    {
        const float reach = a_radius + kLampClear;
        const float out = reach > std::fabs(a_up) ? std::sqrt(reach * reach - a_up * a_up) : 0.0f;
        return (std::max)(out, a_least);
    }

    // The point light for one end, a_end 0 the headlights, 1 the tail lights. False while neither of its lamps burns.
    inline bool LightOf(const Protocol::BodyLights& a_lights, int a_end, const Tuning& a_tuning, Glow& a_out)
    {
        if (a_end < 0 || a_end > 1) {
            return false;
        }
        const bool          front = a_end == 0;
        const bool          right = (a_lights.lit & kLampBits[a_end * 2]) != 0;
        const bool          left = (a_lights.lit & kLampBits[a_end * 2 + 1]) != 0;
        const std::int16_t* dummy = front ? a_lights.front : a_lights.rear;
        if (!right && !left) {
            return false;
        }
        const bool braking = !front && (a_lights.state & Protocol::kLightBraking) != 0;
        a_out.radius = front ? a_tuning.beamRadius : braking ? a_tuning.brakeRadius : a_tuning.tailRadius;
        a_out.intensity = front ? a_tuning.beamIntensity : braking ? a_tuning.brakeIntensity : a_tuning.tailIntensity;
        // Midway between a pair; at the one burning otherwise (the left one mirrored, x negated, as DoLightEffectImpl does).
        a_out.at.x = right && left ? 0.0f : static_cast<float>(left ? -dummy[0] : dummy[0]);
        if (front) {
            a_out.at.y = static_cast<float>(dummy[1]) + OutFrom(a_out.radius, a_tuning.beamRaise, a_tuning.beamAhead);
            a_out.at.z = static_cast<float>(dummy[2]) + a_tuning.beamRaise;
        } else {
            a_out.at.y = static_cast<float>(dummy[1]) - OutFrom(a_out.radius, a_tuning.tailDrop, 0.0f);
            a_out.at.z = static_cast<float>(dummy[2]) - a_tuning.tailDrop;
        }
        // San Andreas's headlight colour (160, 160, 140) brought up to full and a little warmer, so the pool stands out
        // against Skyrim's blue moonlight, and its tail lights' red.
        const float  white[3] = { 1.0f, 0.92f, 0.7f };
        const float  red[3] = { 1.0f, 0.06f, 0.03f };
        const float* colour = front ? white : red;
        std::copy(colour, colour + 3, a_out.colour);
        return true;
    }

    // A lamp mesh's own glow (its emissive strength) for lamp a_lamp (a kLight bit): none while it doesn't burn, a tail
    // lamp's brighter while braking.
    inline float LampGlowOf(std::uint8_t a_lamp, const Protocol::BodyLights& a_lights, const Tuning& a_tuning)
    {
        if ((a_lamp & a_lights.lit) == 0) {
            return 0.0f;
        }
        const bool rear = (a_lamp & (Protocol::kLightRearRight | Protocol::kLightRearLeft)) != 0;
        return a_tuning.lampGlow * (rear && (a_lights.state & Protocol::kLightBraking) != 0 ? a_tuning.brakeGlow : 1.0f);
    }

    // A lamp's name in the logs.
    constexpr const char* LampName(std::uint8_t a_lamp)
    {
        switch (a_lamp) {
        case Protocol::kLightFrontRight: return "front right";
        case Protocol::kLightFrontLeft: return "front left";
        case Protocol::kLightRearRight: return "rear right";
        case Protocol::kLightRearLeft: return "rear left";
        default: return "several";
        }
    }
}
