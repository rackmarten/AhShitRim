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
//    bike one of each. A wreck shows none.

#pragma once

#include <algorithm>
#include <cmath>
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
        bool twin = false;      // a pair of each, mirrored (CAutomobile and what derives from it: cars, quads, aircraft)
        bool hasFront = false;  // the model has a headlight dummy
        bool hasRear = false;   // and a tail light dummy
        bool smashed[4] = {};   // in kLight order: front right, front left, rear right, rear left
    };

    constexpr std::uint8_t Lit(const Lamps& a_lamps)
    {
        if (!a_lamps.engineOn || a_lamps.wrecked || a_lamps.forceOff || !(a_lamps.wanted || a_lamps.forceOn)) {
            return 0;
        }
        std::uint8_t lit = 0;
        const std::uint8_t bits[4] = { Protocol::kLightFrontRight, Protocol::kLightFrontLeft, Protocol::kLightRearRight, Protocol::kLightRearLeft };
        for (int i = 0; i < 4; i++) {
            const bool front = i < 2;
            const bool mirror = i % 2 == 1;
            if ((front ? a_lamps.hasFront : a_lamps.hasRear) && (!mirror || a_lamps.twin) && !a_lamps.smashed[i]) {
                lit |= bits[i];
            }
        }
        return lit;
    }

    // A dummy in Skyrim units, as BodyLights carries it (whole units: 1.4 cm, up to 468 m out).
    inline std::int16_t Pack(float a_units)
    {
        if (!std::isfinite(a_units)) {
            return 0;
        }
        return static_cast<std::int16_t>(std::clamp(std::lround(a_units), -32767l, 32767l));
    }

    // ---- The light they throw (Skyrim's side) ----

    // Skyrim has no shadowless spot light, so a headlight pair is one point light out on the road ahead, lighting
    // it from the bumper to about 15 m, and the tail lights one red glow just behind them.
    inline constexpr float kBeamAhead = 5.0f * Pose::kUnitsPerMeter;    // ahead of the headlights
    inline constexpr float kBeamRadius = 10.0f * Pose::kUnitsPerMeter;
    inline constexpr float kTailBehind = 0.3f * Pose::kUnitsPerMeter;   // behind the tail lights
    inline constexpr float kTailRadius = 2.5f * Pose::kUnitsPerMeter;

    struct Glow
    {
        Vec3  at;  // in the vehicle's frame (X right, Y forward, Z up), Skyrim units
        float radius = 0.0f;
        float colour[3] = {};
    };

    // The point light for one end of the vehicle: in the middle of the lit lamps of that end (one alone where the
    // other is smashed), a_out along it. False when none of them burns.
    inline bool EndOf(const Protocol::BodyLights& a_lights, bool a_front, Glow& a_out)
    {
        const std::uint8_t right = a_front ? Protocol::kLightFrontRight : Protocol::kLightRearRight;
        const std::uint8_t left = a_front ? Protocol::kLightFrontLeft : Protocol::kLightRearLeft;
        const bool         hasRight = (a_lights.lit & right) != 0;
        const bool         hasLeft = (a_lights.lit & left) != 0;
        if (!hasRight && !hasLeft) {
            return false;
        }
        const std::int16_t* dummy = a_front ? a_lights.front : a_lights.rear;
        const float         x = static_cast<float>(dummy[0]);
        a_out.at.x = hasRight && hasLeft ? 0.0f : hasRight ? x : -x;
        a_out.at.y = static_cast<float>(dummy[1]) + (a_front ? kBeamAhead : -kTailBehind);
        a_out.at.z = static_cast<float>(dummy[2]);
        a_out.radius = a_front ? kBeamRadius : kTailRadius;
        // San Andreas's headlight colour (160, 160, 140) brought up to full, and its tail lights' red.
        const float white[3] = { 1.0f, 1.0f, 0.875f };
        const float red[3] = { 1.0f, 0.06f, 0.03f };
        const float* colour = a_front ? white : red;
        std::copy(colour, colour + 3, a_out.colour);
        return true;
    }
}
