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
//  - The lamps' meshes are drawn lit while their lamp burns (LampOfMaterial), each lamp's light comes from where
//    it is (LampOf).

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

    // ---- The light they throw (Skyrim's side) ----

    // Skyrim has no shadowless spot light, so each lamp burning is a point light of its own, placed from its dummy (the
    // left ones mirrored, x negated, as DoLightEffectImpl places them):
    //  - a headlight's out on the road ahead of it and a little up, lighting the ground as San Andreas's light pool
    //    does (DoHeadLightReflection's StoreCarLightShadow: about 7 m ahead of the lamps and 13 m long, plus a 20 m
    //    directional light), so clearly, not a glimmer;
    //  - a tail light's just behind it, red, small; larger and brighter while braking (San Andreas adds a red light
    //    behind the car then).
    // The numbers are a matter of taste, so AhShitRim.ini's [Headlights] may change them (skse/src/player/Headlights.cpp).
    struct Tuning
    {
        float beamAhead = 6.0f * Pose::kUnitsPerMeter;   // ahead of a headlight
        float beamRaise = 0.8f * Pose::kUnitsPerMeter;   // above it, so the pool spreads over the road
        float beamRadius = 12.0f * Pose::kUnitsPerMeter;
        float beamIntensity = 2.5f;                       // the point light's fade: Skyrim's torch is about 1
        float tailBehind = 0.25f * Pose::kUnitsPerMeter;
        float tailRadius = 1.5f * Pose::kUnitsPerMeter;
        float tailIntensity = 1.0f;
        float brakeRadius = 3.0f * Pose::kUnitsPerMeter;
        float brakeIntensity = 2.5f;
        float lampGlow = 2.0f;  // a burning lamp's mesh: its emissive strength (Skyrim's shader)
    };

    struct Glow
    {
        Vec3  at;  // in the vehicle's frame (X right, Y forward, Z up), Skyrim units
        float radius = 0.0f;
        float intensity = 0.0f;
        float colour[3] = {};
    };

    // The point light for lamp a_lamp (0..3, in kLight order: front right, front left, rear right, rear left). False
    // while it doesn't burn.
    inline bool LampOf(const Protocol::BodyLights& a_lights, int a_lamp, const Tuning& a_tuning, Glow& a_out)
    {
        if (a_lamp < 0 || a_lamp >= 4 || (a_lights.lit & kLampBits[a_lamp]) == 0) {
            return false;
        }
        const bool          front = a_lamp < 2;
        const bool          left = a_lamp % 2 == 1;
        const std::int16_t* dummy = front ? a_lights.front : a_lights.rear;
        a_out.at.x = static_cast<float>(left ? -dummy[0] : dummy[0]);
        a_out.at.y = static_cast<float>(dummy[1]) + (front ? a_tuning.beamAhead : -a_tuning.tailBehind);
        a_out.at.z = static_cast<float>(dummy[2]) + (front ? a_tuning.beamRaise : 0.0f);
        const bool braking = !front && (a_lights.state & Protocol::kLightBraking) != 0;
        a_out.radius = front ? a_tuning.beamRadius : braking ? a_tuning.brakeRadius : a_tuning.tailRadius;
        a_out.intensity = front ? a_tuning.beamIntensity : braking ? a_tuning.brakeIntensity : a_tuning.tailIntensity;
        // San Andreas's headlight colour (160, 160, 140) brought up to full, and its tail lights' red.
        const float white[3] = { 1.0f, 1.0f, 0.875f };
        const float red[3] = { 1.0f, 0.06f, 0.03f };
        const float* colour = front ? white : red;
        std::copy(colour, colour + 3, a_out.colour);
        return true;
    }
}
