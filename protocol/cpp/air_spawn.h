// AIR + a plane's name (AIRAT400, AIRBEAGLE, AIRANDROM) spawns it already flying, CJ at the controls: the game-free half.
// Skyrim's rough ground is no runway, so the big planes could not take off at all. Which models come flying is here (the
// typing that tells AIR apart is Cheats::Typist's); San Andreas's half, in sa/src/vehicles.cpp (Vehicles::Spawn), puts the
// plane kHeight above CJ facing where his camera looks, gear up, engine on, CJ warped in as its driver, at a forward
// speed a little over what holds it up (LaunchSpeed, from its flying handling). Header-only and free of game types, so
// the protocol tests cover it.
//
// Helicopters stay on the ground: they take off from anywhere already. The Vortex, a hovercraft San Andreas counts as
// a plane, has no lift to fly with.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "vehicle_kinds.h"

namespace AhShitRim::AirSpawn
{
    inline constexpr float kHeight = 150.0f;  // metres above CJ's feet: under the floating origin's 300 m (ground.h), with the ground below in reach
    inline constexpr std::uint16_t kVortex = 539;

    // San Andreas's flight model (CVehicle::FlyingControl, gta-reversed): lift is (attack angle * fAttackLift + fFormLift)
    // * mass * step * forward speed squared, against gravity's mass * 0.008 * step, speeds in metres a 1/50 s step.
    inline constexpr float kGravityStep = 0.008f;
    inline constexpr float kStepsPerSecond = 50.0f;
    inline constexpr float kMargin = 1.3f;          // over the speed that just holds it level, so it climbs a little, not sinks
    inline constexpr float kMinSpeed = 20.0f;       // metres a second, whatever its handling says
    inline constexpr float kMaxSpeed = 80.0f;
    inline constexpr float kFallbackSpeed = 45.0f;  // a handling with no lift (a modded one)

    // Whether a_model comes flying after AIR: San Andreas's planes but the Vortex.
    constexpr bool Flies(std::uint16_t a_model)
    {
        return VehicleKinds::KindOf(a_model) == VehicleKinds::Kind::Plane && a_model != kVortex;
    }

    // The forward speed, metres a second, a plane with a_formLift (its flying handling's fFormLift) is launched at: level
    // flight's speed with the gear up and no attack angle, times kMargin, kept between kMinSpeed and kMaxSpeed.
    inline float LaunchSpeed(float a_formLift)
    {
        if (!(a_formLift > 0.0f) || !std::isfinite(a_formLift)) {
            return kFallbackSpeed;
        }
        const float level = std::sqrt(kGravityStep / a_formLift) * kStepsPerSecond;
        return std::clamp(level * kMargin, kMinSpeed, kMaxSpeed);
    }

    // San Andreas's heading (CPlaceable::SetHeading: forward is (-sin, cos)) for flying along a camera's forward a_x, a_y;
    // a_fallback when the camera looks straight up or down.
    inline float HeadingOf(float a_x, float a_y, float a_fallback)
    {
        if (!(std::hypot(a_x, a_y) > 1e-3f)) {
            return a_fallback;
        }
        return std::atan2(-a_x, a_y);
    }
}
