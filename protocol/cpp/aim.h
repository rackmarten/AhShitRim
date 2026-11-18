// How CJ aims a gun, the game-free half: which line a shot of his takes, how an arm is posed to point a
// one-handed gun along an aim line and how far his head turns to it (San Andreas's arm and look IK, which never
// run while he is hidden: the arm stays half raised and the head where the animation has it). Header-only
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
    inline constexpr float kElbowOut = 0.5f;    // the elbow drops below the aim line and this much as far out to his side

    // How far the elbow bends from straight while he points a gun with his arm. San Andreas's arm IK
    // (IKChain_c::MoveBonesToTarget, CTaskSimpleIKPointArm) turns the forearm and the upper arm, one after the
    // other every frame, until each one's line to the hand points at the aim point, which for a point metres away
    // straightens the arm as far as the elbow goes: 1 degree short of straight (its bone limits, BoneNodeManager_c's
    // table, forearm Z from -149 to -1 degrees). Its clavicle and hand are held at a fixed pose meanwhile.
    inline constexpr float kElbowBend = 1.0f * Pose::kPi / 180.0f;

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

    // How far from the shoulder the hand is with the elbow bent a_bend from straight (law of cosines).
    inline float ReachOf(float a_upper, float a_fore, float a_bend)
    {
        return std::sqrt((std::max)(0.0f, a_upper * a_upper + a_fore * a_fore + 2.0f * a_upper * a_fore * std::cos(a_bend)));
    }

    // Where the elbow and the hand go for an arm (upper arm a_upper long, forearm a_fore) reaching from a_shoulder
    // along a_line: the hand on the line with the elbow bent a_bend (San Andreas's kElbowBend unless told
    // otherwise), the little bend there is below the line and out to his side (a_out, the arm's own side).
    inline Arm ReachAlong(const Vec3& a_shoulder, const Vec3& a_line, float a_upper, float a_fore, const Vec3& a_out, float a_bend = kElbowBend)
    {
        const Vec3  dir = Body::Normalize(a_line, { 0.0f, 1.0f, 0.0f });
        const float upper = (std::max)(a_upper, 1e-3f), fore = (std::max)(a_fore, 1e-3f);
        const float reach = (std::max)(std::abs(upper - fore) + 1e-3f, ReachOf(upper, fore, a_bend));
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

    // How far a gun is rolled over about its barrel, seen from behind it: the angle from straight up to its top
    // (a_top), positive with the top leaning to the right. 0 for a barrel straight up or down.
    inline float RollOf(const Vec3& a_barrel, const Vec3& a_top)
    {
        const Vec3 b = Body::Normalize(a_barrel, {});
        const Vec3 up{ 0.0f, 0.0f, 1.0f };
        const Vec3 u = Body::Sub(up, Body::Scale(b, Body::Dot(b, up)));
        const Vec3 t = Body::Sub(a_top, Body::Scale(b, Body::Dot(b, a_top)));
        if (Body::Dot(u, u) < 1e-6f || Body::Dot(t, t) < 1e-6f) {
            return 0.0f;
        }
        return std::atan2(Body::Dot(b, Body::Cross(u, t)), Body::Dot(u, t));
    }

    // A gun's top square to its barrel, rolled a_roll over (RollOf's sense). Straight up for a barrel straight
    // up or down (no roll to speak of).
    inline Vec3 RolledTop(const Vec3& a_barrel, float a_roll)
    {
        const Vec3 b = Body::Normalize(a_barrel, {});
        const Vec3 u = Body::Normalize(Body::Sub({ 0.0f, 0.0f, 1.0f }, Body::Scale(b, b.z)), {});
        if (Body::Dot(u, u) < 0.5f) {
            return { 0.0f, 0.0f, 1.0f };
        }
        return Body::Add(Body::Scale(u, std::cos(a_roll)), Body::Scale(Body::Cross(b, u), std::sin(a_roll)));
    }

    // The right gun's roll about its barrel: leaning in toward his middle, as the left gun of a pair leans. San
    // Andreas's stance rolls the left gun's top to the right (in), but the right hand's own pose rolls its gun right
    // too (out), alone or as the right one of a pair. So a pair's right gun takes the left one's roll mirrored, and a
    // gun on its own its own lean turned in, as the right one of a pair (a_twin, a_leftRoll: the left gun's roll).
    inline float RightGunRoll(float a_rightRoll, bool a_twin, float a_leftRoll) { return a_twin ? -a_leftRoll : -std::abs(a_rightRoll); }

    // ---- His head ----

    // Aiming a gun with his arm, San Andreas also turns his head to the aim point (CTaskSimpleUseGun::AimGun's
    // LookAt), as far as its head bone goes (BoneNodeManager_c's table, BoneNode_c::Limit): up to 65 degrees to
    // either side, and a nod up or down of up to 45 degrees that shrinks as the head turns, gone at 45 degrees.
    inline constexpr float kHeadTurn = 65.0f * Pose::kPi / 180.0f;
    inline constexpr float kHeadNod = 45.0f * Pose::kPi / 180.0f;
    inline constexpr float kHeadNodGone = 45.0f * Pose::kPi / 180.0f;

    struct Look
    {
        float turn = 0.0f;  // radians to his right (left is negative)
        float nod = 0.0f;   // radians up (down is negative)
    };

    // How far his head turns and nods from a_facing (his chest's, level) to look along a_wanted, within its limits.
    inline Look HeadLook(const Vec3& a_facing, const Vec3& a_wanted)
    {
        const Vec3  forward = Ahead(a_facing);
        const Vec3  right{ forward.y, -forward.x, 0.0f };
        const float horizontal = std::hypot(a_wanted.x, a_wanted.y);
        if (horizontal < 1e-4f && std::abs(a_wanted.z) < 1e-4f) {
            return {};
        }
        Look look;
        look.turn = std::clamp(std::atan2(Body::Dot(a_wanted, right), Body::Dot(a_wanted, forward)), -kHeadTurn, kHeadTurn);
        const float nod = kHeadNod * (std::max)(0.0f, 1.0f - std::abs(look.turn) / kHeadNodGone);
        look.nod = std::clamp(std::atan2(a_wanted.z, horizontal), -nod, nod);
        return look;
    }

    // The way his head looks, turned and nodded a_look from a_facing (a unit vector).
    inline Vec3 LookLine(const Vec3& a_facing, const Look& a_look)
    {
        const Vec3  forward = Ahead(a_facing);
        const Vec3  right{ forward.y, -forward.x, 0.0f };
        const float level = std::cos(a_look.nod);
        return Body::Add(Body::Add(Body::Scale(forward, level * std::cos(a_look.turn)), Body::Scale(right, level * std::sin(a_look.turn))),
            { 0.0f, 0.0f, std::sin(a_look.nod) });
    }
}
