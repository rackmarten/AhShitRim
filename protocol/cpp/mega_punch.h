// IAVENJQ, mega punch, the game-free half. San Andreas's CHEAT_MEGA_PUNCH (eCheats 67) is a toggle with no function:
// with it on, a blow from fists, brass knuckles or the parachute does the victim's whole health
// (CPedDamageResponseCalculator::ComputeWillKillPed) and its knock force times 20 (CEventDamage), so they fly off. CJ's
// blows hurt Skyrim's actors on Skyrim's side, so Skyrim does the same there (skse/src/player/Shots.cpp, with the toggle
// in CheatCodes): the blow kills (an essential actor still only kneels, unless NOMERCY) and AIProcess::KnockExplosion
// throws the victim far, away from CJ and upwards. Giants, mammoths and dragons, which no blast throws either
// (explosives.h), only take the kill. Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <cmath>
#include <cstdint>

#include "damage.h"
#include "pose.h"
#include "weapons.h"

namespace AhShitRim::MegaPunch
{
    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // Skyrim's knock magnitude for a mega punch: a rocket's blast throws people a few metres with 14
    // (explosives.h), this sends a bandit flying several times as far.
    inline constexpr float kKnock = 50.0f;

    // The damage is the victim's health times this, plus one: Skyrim's difficulty can cut the player's damage to a
    // quarter (Legendary), and the blow has to kill all the same.
    inline constexpr float kOverkill = 4.0f;

    // The knock's middle sits this far behind the fist and below it, so the victim goes away from CJ and up.
    inline constexpr float kBehind = 1.0f * kMeter;
    inline constexpr float kBelow = 0.6f * kMeter;

    // Whether a blow with a_weapon (San Andreas's eWeaponType) is a mega punch: fists, brass knuckles and the
    // parachute, as in San Andreas. A bat, a knife or a katana isn't.
    constexpr bool Punches(std::uint8_t a_weapon)
    {
        return a_weapon == Weapons::Unarmed || a_weapon == Weapons::BrassKnuckle || a_weapon == Weapons::Parachute;
    }

    // Skyrim damage that kills a victim with a_health left.
    inline float DamageFor(float a_health)
    {
        if (!(a_health > 0.0f) || !std::isfinite(a_health)) {
            return 1.0f;
        }
        return a_health * kOverkill + 1.0f;
    }

    // How hard the blow throws a victim of a_class: 0 for giants, mammoths and dragons.
    constexpr float KnockFor(Damage::Class a_class) { return a_class == Damage::Class::Normal ? kKnock : 0.0f; }

    struct Vec
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // Where the knock goes off for a blow landing at a_fist, CJ facing a_facing (flat, any length): behind the fist and
    // below it. Straight below the fist when the facing is no direction.
    inline Vec KnockFrom(Vec a_fist, Vec a_facing)
    {
        const float length = std::hypot(a_facing.x, a_facing.y);
        Vec         from{ a_fist.x, a_fist.y, a_fist.z - kBelow };
        if (length > 1e-4f && std::isfinite(length)) {
            from.x -= a_facing.x / length * kBehind;
            from.y -= a_facing.y / length * kBehind;
        }
        return from;
    }
}
