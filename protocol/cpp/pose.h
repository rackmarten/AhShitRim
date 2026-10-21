// Converts CJ's San Andreas pose into the PlayerState Skyrim reads. Header-only and free of game
// types, so the protocol tests cover it; the San Andreas ASI feeds it every frame.
//
// Both games put X east, Y north and Z up. San Andreas measures in metres and turns headings
// counter-clockwise; Skyrim measures in game units (70 to the metre, PerkeleRim's scale) and turns
// clockwise from north. Directions come in as unit vectors (a matrix's forward row), so neither
// game's angle convention leaks into the other.

#pragma once

#include <cmath>
#include <cstdint>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Pose
{
    inline constexpr float kPi = 3.14159265358979f;
    inline constexpr float kMetersPerUnit = 0.0142875f;
    inline constexpr float kUnitsPerMeter = 1.0f / kMetersPerUnit;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Skyrim heading (radians, 0 = north, clockwise, 0..2pi) of a horizontal direction.
    inline float HeadingOf(float a_x, float a_y)
    {
        float h = std::atan2(a_x, a_y);
        if (h < 0.0f) {
            h += 2.0f * kPi;
        }
        return h;
    }

    // Skyrim pitch (radians, positive looks down) of a direction.
    inline float PitchOf(const Vec3& a_dir)
    {
        return std::atan2(-a_dir.z, std::hypot(a_dir.x, a_dir.y));
    }

    // San Andreas heading (CPed::m_fCurrentRotation: radians, counter-clockwise, 0 = north) that
    // faces the way a Skyrim heading does.
    inline float SanAndreasHeading(float a_skyrimHeading)
    {
        float h = -a_skyrimHeading;
        while (h <= -kPi) h += 2.0f * kPi;
        while (h > kPi) h -= 2.0f * kPi;
        return h;
    }

    // What the ASI reads from San Andreas each frame, in its own units (metres, m/s).
    struct SanAndreasPose
    {
        Vec3 position;  // CJ's feet
        Vec3 spawn;     // where this handover put his feet
        Vec3 velocity;  // metres per second
        Vec3 forward;   // CJ's facing (his matrix's forward row)
        Vec3 view;      // the camera's look direction
        bool grounded = false;
        bool onFire = false;  // v15: CJ burns
        bool crouching = false;

        // The vehicle CJ is in (v11), if any.
        std::uint16_t vehicleModel = 0;          // San Andreas's model id, 0 = on foot
        std::uint8_t  vehicleColours[4] = {};
        Vec3          vehicleOffset;             // its position minus CJ's feet, metres
        Vec3          vehicleForward{ 0.0f, 1.0f, 0.0f };  // the way it faces
        float         damage = 0.0f;             // what San Andreas took off CJ so far, its health points

        // CJ's weapons (v12).
        std::uint8_t  weaponType = 0;            // the one in his hands, San Andreas's eWeaponType
        std::uint8_t  weaponSlot = 0;
        std::uint16_t weaponFlags = 0;           // kWeapon*
        std::uint16_t shots = 0;                 // the shot counter
        std::uint8_t  cameraMode = 0;            // San Andreas's eCamMode
        std::uint8_t  ammoStyle = 0;             // Protocol::kAmmo*
        Vec3          muzzle[2];                 // where the shots leave, relative to his feet, metres
        float         crosshair[2] = { 0.5f, 0.4f };  // where San Andreas puts its crosshair, fractions of the screen
        Protocol::WeaponEntry loadout[Protocol::kWeaponSlotCount] = {};
        std::uint16_t skills[Protocol::kWeaponSkillCount] = {};  // v13: the weapon skill stats, 0..1000
        float         aimRadius = 0.2f;  // v13: San Andreas's crosshair size (GetWeaponRadiusOnScreen)
    };

    inline Protocol::PlayerState ToPlayerState(const SanAndreasPose& a_pose, std::uint32_t a_frame, std::uint32_t a_handover)
    {
        Protocol::PlayerState s{};
        s.frame = a_frame;
        s.flags = Protocol::kPlayerValid | (a_pose.grounded ? Protocol::kPlayerGrounded : 0) |
                  (a_pose.crouching ? Protocol::kPlayerCrouching : 0) | (a_pose.onFire ? Protocol::kPlayerOnFire : 0);
        s.offset[0] = (a_pose.position.x - a_pose.spawn.x) * kUnitsPerMeter;
        s.offset[1] = (a_pose.position.y - a_pose.spawn.y) * kUnitsPerMeter;
        s.offset[2] = (a_pose.position.z - a_pose.spawn.z) * kUnitsPerMeter;
        s.velocity[0] = a_pose.velocity.x * kUnitsPerMeter;
        s.velocity[1] = a_pose.velocity.y * kUnitsPerMeter;
        s.velocity[2] = a_pose.velocity.z * kUnitsPerMeter;
        s.heading = HeadingOf(a_pose.forward.x, a_pose.forward.y);
        s.viewHeading = HeadingOf(a_pose.view.x, a_pose.view.y);
        s.viewPitch = PitchOf(a_pose.view);
        s.handover = a_handover;
        if (a_pose.vehicleModel) {
            s.vehicleModel = a_pose.vehicleModel;
            for (int i = 0; i < 4; ++i) {
                s.vehicleColours[i] = a_pose.vehicleColours[i];
            }
            s.vehicleOffset[0] = a_pose.vehicleOffset.x * kUnitsPerMeter;
            s.vehicleOffset[1] = a_pose.vehicleOffset.y * kUnitsPerMeter;
            s.vehicleOffset[2] = a_pose.vehicleOffset.z * kUnitsPerMeter;
            s.vehicleHeading = HeadingOf(a_pose.vehicleForward.x, a_pose.vehicleForward.y);
        }
        s.damage = a_pose.damage;
        s.weaponType = a_pose.weaponType;
        s.weaponSlot = a_pose.weaponSlot;
        s.weaponFlags = a_pose.weaponFlags;
        s.shots = a_pose.shots;
        s.cameraMode = a_pose.cameraMode;
        s.ammoStyle = a_pose.ammoStyle;
        for (int hand = 0; hand < 2; ++hand) {
            s.muzzle[hand][0] = a_pose.muzzle[hand].x * kUnitsPerMeter;
            s.muzzle[hand][1] = a_pose.muzzle[hand].y * kUnitsPerMeter;
            s.muzzle[hand][2] = a_pose.muzzle[hand].z * kUnitsPerMeter;
        }
        s.crosshair[0] = a_pose.crosshair[0];
        s.crosshair[1] = a_pose.crosshair[1];
        for (std::uint32_t i = 0; i < Protocol::kWeaponSlotCount; ++i) {
            s.loadout[i] = a_pose.loadout[i];
        }
        for (std::uint32_t i = 0; i < Protocol::kWeaponSkillCount; ++i) {
            s.skills[i] = a_pose.skills[i];
        }
        const float radius = std::isfinite(a_pose.aimRadius) ? a_pose.aimRadius : 0.0f;
        s.aimRadius = static_cast<std::uint16_t>(radius <= 0.0f ? 0.0f : radius >= 60.0f ? 60000.0f : radius * 1000.0f + 0.5f);
        return s;
    }

    inline bool Finite(const Protocol::PlayerState& a_pose)
    {
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(a_pose.offset[i]) || !std::isfinite(a_pose.velocity[i])) {
                return false;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(a_pose.vehicleOffset[i])) {
                return false;
            }
        }
        for (int hand = 0; hand < 2; ++hand) {
            for (int i = 0; i < 3; ++i) {
                if (!std::isfinite(a_pose.muzzle[hand][i])) {
                    return false;
                }
            }
        }
        return std::isfinite(a_pose.heading) && std::isfinite(a_pose.viewHeading) && std::isfinite(a_pose.viewPitch) &&
               std::isfinite(a_pose.vehicleHeading) && std::isfinite(a_pose.damage) && std::isfinite(a_pose.crosshair[0]) &&
               std::isfinite(a_pose.crosshair[1]);
    }
}
