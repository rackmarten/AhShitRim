// When Skyrim's hidden player counts as standing, the game-free half. The player is put at CJ's feet every frame
// with no speed of its own, so its character controller rarely finds the ground under it (San Andreas's ground is
// Skyrim's, sampled, a little off here and there, and a seat is above it) and keeps calling it in the air. Skyrim
// then refuses fast travel ("you can't fast travel while jumping or falling") while CJ just stands there. So the
// player is made to stand whenever CJ does: on his feet or in a vehicle on the ground or the water, not climbing or
// dropping faster than a walk up a slope. Never in a plane or a helicopter. Header-only and free of game types, so
// the protocol tests cover it; skse/src/player/PlayerSync.cpp applies it.

#pragma once

#include <cmath>

#include "vehicle_kinds.h"

namespace AhShitRim::Standing
{
    inline constexpr float kStillRise = 100.0f;  // Skyrim units a second (about 1.4 m/s) up or down: still standing

    struct Frame
    {
        bool               valid = false;     // a pose of this handover
        bool               grounded = false;  // San Andreas says CJ stands on something (always in a vehicle)
        bool               inVehicle = false;
        VehicleKinds::Kind vehicle = VehicleKinds::Kind::Unknown;
        float              rise = 0.0f;       // CJ's vertical speed, Skyrim units a second
    };

    constexpr bool Aircraft(VehicleKinds::Kind a_kind) { return a_kind == VehicleKinds::Kind::Heli || a_kind == VehicleKinds::Kind::Plane; }

    inline bool Stands(const Frame& a_frame)
    {
        if (!a_frame.valid || !a_frame.grounded || !std::isfinite(a_frame.rise) || std::abs(a_frame.rise) > kStillRise) {
            return false;
        }
        return !a_frame.inVehicle || !Aircraft(a_frame.vehicle);
    }
}
