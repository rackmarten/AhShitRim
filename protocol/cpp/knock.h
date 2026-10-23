// CJ knocked down by a blow in Skyrim (v22), the game-free half. Skyrim's player is hidden while CJ has it, so a
// giant's club, a mammoth's charge or a shout that ragdolls it would leave its ragdoll fighting the position CJ's
// pose puts it at every frame, and its camera stuck. Instead the player is left to fall and get up out of sight, and
// San Andreas throws CJ away from the blow as its own explosions throw peds: a launch and its fall-and-get-up, which
// way he falls by where the blow came from. Header-only and free of game types, so the protocol tests cover it;
// skse/src/player/PlayerSync.cpp sends it (an Effect message, EffectKind::KnockCj), sa/src/cj_projectiles.cpp plays it.

#pragma once

#include <cmath>

namespace AhShitRim::Knock
{
    inline constexpr float kSpeed = 10.0f;      // metres a second CJ is thrown at, as a car's blast throws him
    inline constexpr float kRise = 0.6f;        // and upwards, this share of it
    inline constexpr float kBlowSeconds = 1.0f;  // a hit this recent is the blow that knocked the player down

    // San Andreas's falls (CTaskComplexFallAndGetUp's direction): 0 on his front (KO_skid_front), 1 spinning right,
    // 2 on his back (KO_skid_back), 3 spinning left.
    enum Fall : int
    {
        kFront = 0,
        kSpinRight = 1,
        kBack = 2,
        kSpinLeft = 3,
    };

    // Which way CJ, facing (a_faceX, a_faceY), falls when thrown along (a_pushX, a_pushY): pushed on, onto his front;
    // pushed back, onto his back; from the side, spinning.
    inline Fall FallFor(float a_faceX, float a_faceY, float a_pushX, float a_pushY)
    {
        const float face = std::hypot(a_faceX, a_faceY);
        const float push = std::hypot(a_pushX, a_pushY);
        if (face <= 0.0f || push <= 0.0f || !std::isfinite(face) || !std::isfinite(push)) {
            return kBack;
        }
        const float along = (a_faceX * a_pushX + a_faceY * a_pushY) / (face * push);
        if (along > 0.7f) {
            return kFront;
        }
        if (along < -0.7f) {
            return kBack;
        }
        const float side = a_faceX * a_pushY - a_faceY * a_pushX;  // positive: pushed to his left
        return side > 0.0f ? kSpinLeft : kSpinRight;
    }

    struct Vec
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // The launch, in metres a second, away from a blow at a_from for CJ at a_at (flat); straight back along a_faceX,
    // a_faceY when the two are on top of each other.
    inline Vec Launch(Vec a_at, Vec a_from, float a_faceX, float a_faceY, float a_speed = kSpeed)
    {
        float dx = a_at.x - a_from.x;
        float dy = a_at.y - a_from.y;
        float length = std::hypot(dx, dy);
        if (length < 0.01f || !std::isfinite(length)) {
            dx = -a_faceX;
            dy = -a_faceY;
            length = std::hypot(dx, dy);
        }
        if (length < 0.01f || !std::isfinite(length)) {
            return { 0.0f, 0.0f, a_speed * kRise };
        }
        return { dx / length * a_speed, dy / length * a_speed, a_speed * kRise };
    }
}
