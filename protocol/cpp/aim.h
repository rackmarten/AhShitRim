// How CJ aims a gun, the game-free half: which line a shot of his takes, and how an arm is posed to point a
// one-handed gun along an aim line (San Andreas's arm IK, which hidden leaves the arm half raised). Header-only
// and free of game types, so the protocol tests cover it. Vectors are San Andreas's (metres, X east, Y north,
// Z up); the San Andreas plugin turns its bones with them (sa/src/cj_body.cpp, sa/src/cj_shots.cpp).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "body.h"
#include "pose.h"
#include "weapons.h"

namespace AhShitRim::Aim
{
    using Pose::Vec3;

    // ---- Where a shot goes ----

    enum class Line : std::uint8_t
    {
        AsFired,    // San Andreas's own line: at a target it picked (lock-on), from a vehicle, or through a sight
        Crosshair,  // through San Andreas's crosshair: its free-aim camera is up (right mouse held)
        Ahead,      // from the hip: straight ahead of him, level
    };

    // The line a third-person shot of CJ's takes (CWeapon::FireInstantHit). Without a target and without the aim
    // camera San Andreas fires along his facing and zeroes his look pitch (CTaskSimplePlayerOnFoot::ProcessPlayerWeapon),
    // but a gun aimed with the arm can fire along the hand instead, which the hidden arm IK leaves pointing at
    // the sky: from the hip it is always ahead of him, level, standing still as when he walks.
    constexpr Line LineOf(std::uint8_t a_cameraMode, bool a_onFoot, bool a_target)
    {
        if (a_target || !a_onFoot) {
            return Line::AsFired;
        }
        switch (Weapons::ScopeOf(a_cameraMode)) {
        case Weapons::Scope::Aim: return Line::Crosshair;
        case Weapons::Scope::None: return Line::Ahead;
        default: return Line::AsFired;
        }
    }

    // Straight ahead of him, level: his facing flattened onto the ground (north when it has no length there).
    inline Vec3 Ahead(const Vec3& a_forward) { return Body::Normalize({ a_forward.x, a_forward.y, 0.0f }, { 0.0f, 1.0f, 0.0f }); }

    // Which way his chest faces, level: square to the line from his left shoulder to his right one. San Andreas's
    // gun stances can turn his body away from his heading (with twin guns both arms then pointed the same way, off
    // to his right) and only turn the torso back past 45 degrees (CPedIK::RotateTorsoForArm), so "ahead" for his
    // arms and his shots from the hip is where his chest faces. His heading when one shoulder is above the other.
    inline Vec3 ChestFacing(const Vec3& a_rightShoulder, const Vec3& a_leftShoulder, const Vec3& a_heading)
    {
        const Vec3 across{ a_rightShoulder.x - a_leftShoulder.x, a_rightShoulder.y - a_leftShoulder.y, 0.0f };
        if (Body::Dot(across, across) < 1e-4f) {
            return Ahead(a_heading);
        }
        const Vec3 right = Body::Normalize(across);
        return { -right.y, right.x, 0.0f };  // up x right
    }

    inline constexpr float kHipAimDistance = 10.0f;  // metres ahead of his chest that the guns from the hip meet

    // The one point both his guns aim at from the hip: ahead of the middle of his chest, so the two arms of a
    // twin gun mirror each other and meet there instead of pointing the same way side by side.
    inline Vec3 HipAimPoint(const Vec3& a_rightShoulder, const Vec3& a_leftShoulder, const Vec3& a_facing)
    {
        const Vec3 middle = Body::Scale(Body::Add(a_rightShoulder, a_leftShoulder), 0.5f);
        return Body::Add(middle, Body::Scale(Ahead(a_facing), kHipAimDistance));
    }

    // ---- Posing an arm ----

    inline constexpr float kArmAcross = 0.26f;  // sin 15 degrees: how far an arm reaches across his front
    inline constexpr float kArmReach = 0.98f;   // the hand this far of the arm's length out: the elbow bent about 23 degrees
    inline constexpr float kElbowOut = 0.5f;    // the elbow drops below the aim line and this much as far out to his side

    // The line an arm aiming along a_wanted takes (a_forward and a_right his facing and his right, level): kept
    // on its own side of his body (the right arm from a bit left of straight ahead round to his right, the left
    // arm the other way), so aiming across him or behind him the arm stops and the wrist turns the rest of the way.
    inline Vec3 ArmLine(const Vec3& a_forward, const Vec3& a_right, const Vec3& a_wanted, bool a_left)
    {
        const Vec3  forward = Body::Normalize({ a_forward.x, a_forward.y, 0.0f }, {});
        const Vec3  right = Body::Normalize({ a_right.x, a_right.y, 0.0f }, {});
        const float horizontal = std::hypot(a_wanted.x, a_wanted.y);
        if (Body::Dot(forward, forward) < 0.5f || Body::Dot(right, right) < 0.5f || horizontal < 1e-3f) {
            return a_wanted;
        }
        const float side = a_left ? -1.0f : 1.0f;
        const float ahead = (a_wanted.x * forward.x + a_wanted.y * forward.y) / horizontal;
        const float out = side * (a_wanted.x * right.x + a_wanted.y * right.y) / horizontal;
        if (out >= -kArmAcross && ahead >= 0.0f) {
            return a_wanted;
        }
        // Across his body or behind him: as far across as the arm goes, at the same height.
        const float across = ahead >= 0.0f ? -kArmAcross : (out >= 0.0f ? 1.0f : -kArmAcross);
        const float straight = std::sqrt((std::max)(0.0f, 1.0f - across * across));
        const Vec3  flat = Body::Add(Body::Scale(forward, straight), Body::Scale(right, side * across));
        return { flat.x * horizontal, flat.y * horizontal, a_wanted.z };
    }

    struct Arm
    {
        Vec3 elbow;
        Vec3 hand;
    };

    // Where the elbow and the hand go for an arm (upper arm a_upper long, forearm a_fore) reaching from a_shoulder
    // along a_line: the hand on the line, kArmReach of the arm's length out, the elbow bent below the line and out
    // to his side (a_out, the arm's own side), as San Andreas's gun stances hold a pistol, not locked straight.
    inline Arm ReachAlong(const Vec3& a_shoulder, const Vec3& a_line, float a_upper, float a_fore, const Vec3& a_out)
    {
        const Vec3  dir = Body::Normalize(a_line, { 0.0f, 1.0f, 0.0f });
        const float upper = (std::max)(a_upper, 1e-3f), fore = (std::max)(a_fore, 1e-3f);
        const float reach = (std::max)(std::abs(upper - fore) + 1e-3f, kArmReach * (upper + fore));
        const Vec3  hand = Body::Add(a_shoulder, Body::Scale(dir, reach));
        // The angle at the shoulder between the line and the upper arm (law of cosines).
        const float cos = std::clamp((upper * upper + reach * reach - fore * fore) / (2.0f * upper * reach), -1.0f, 1.0f);
        const float sin = std::sqrt((std::max)(0.0f, 1.0f - cos * cos));
        // Down and out, without the part along the line.
        const Vec3 pole = Body::Add({ 0.0f, 0.0f, -1.0f }, Body::Scale(Body::Normalize({ a_out.x, a_out.y, 0.0f }, {}), kElbowOut));
        Vec3       bend = Body::Sub(pole, Body::Scale(dir, Body::Dot(pole, dir)));
        if (Body::Dot(bend, bend) < 1e-6f) {
            // Aiming straight down or up: the elbow goes out instead.
            const Vec3 out = Body::Sub(a_out, Body::Scale(dir, Body::Dot(a_out, dir)));
            bend = Body::Dot(out, out) > 1e-6f ? out : Body::Cross(dir, { 1.0f, 0.0f, 0.0f });
        }
        bend = Body::Normalize(bend);
        const Vec3 elbow = Body::Add(a_shoulder, Body::Add(Body::Scale(dir, upper * cos), Body::Scale(bend, upper * sin)));
        return { elbow, hand };
    }

    // The way the barrel has to point: from the muzzle to the aim point. A gun's barrel lies along its hand's X
    // axis (San Andreas fires along it, and the weapon's model sits on the hand as it is); its fire offset only
    // says where the muzzle is (above the grip), so pointing the hand-to-muzzle line at the target instead tips
    // the barrel down by its angle, 15 degrees for the sawn-off.
    inline Vec3 BarrelLine(const Vec3& a_muzzle, const Vec3& a_target) { return Body::Normalize(Body::Sub(a_target, a_muzzle), { 0.0f, 1.0f, 0.0f }); }
}
