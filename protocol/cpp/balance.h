// How hard CJ hits and how hard he is hit, the game-free half: AhShitRim.ini's [Balance] factors and Skyrim's
// difficulty on top of the damage the rest works out. Header-only and free of game types, so the protocol tests
// cover it; the SKSE plugin feeds it (skse/src/player/CombatBalance.cpp).
//
// Balanced for the fights a player has most, bandits, draugr, spiders, giants: the wanted level's share for guards
// and ours (Wanted::GuardDamage) is a layer of its own on top, for them only.
// - CJ's guns and explosives (Damage::ToSkyrim, Shots): times CJDamage, and kDealt for the difficulty when
//   FollowDifficulty is on: Skyrim's own "damage by the player" multipliers (fDiffMultHPByPC*) a difficulty harder,
//   since Skyrim's own hit doesn't apply its own to ours as it does to the player's sword. Melee is left as it is.
// - Skyrim's people's and creatures' hits on the player: Skyrim has already put its difficulty on them
//   (fDiffMultHPToPC*), so only times DamageTaken, then CJ's armour takes them first (Health::AbsorbSkyrim).
//
// Tuning: at Master a Colt 45 body shot took 20 off a level 6 bandit (91 health). Skyrim's own multipliers made Adept
// feel like Apprentice and Master hard, so CJ deals what the next harder difficulty would give the player: 0.75 at
// Adept, 0.25 at Master and at Legendary (the floor). Unarmed CJ already took about the right damage, so
// DamageTaken stays 1. kTargets below are the example fights; the doctests hold them.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "damage.h"

namespace AhShitRim::Balance
{
    // Skyrim's difficulties, as the player's difficulty (and SkyrimPrefs.ini's iDifficulty) number them.
    inline constexpr int kDifficulties = 6;  // Novice, Apprentice, Adept, Expert, Master, Legendary
    inline constexpr int kAdept = 2;
    inline constexpr int kMaster = 4;

    // What CJ's guns deal at each difficulty: Skyrim.esm's fDiffMultHPByPC* (2, 1.5, 1, 0.75, 0.5, 0.25) one step down,
    // Legendary's staying at Master's 0.25, the floor.
    inline constexpr std::array<float, kDifficulties> kDealt = { 1.5f, 1.0f, 0.75f, 0.5f, 0.25f, 0.25f };

    // Skyrim.esm's fDiffMultHPToPC*: what Skyrim already does to what the player takes (for the example fights).
    inline constexpr std::array<float, kDifficulties> kToPlayer = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f };

    inline constexpr float kMaxFactor = 10.0f;

    struct Settings
    {
        float cjDamage = 1.0f;          // [Balance] CJDamage: what CJ's guns do, times
        float damageTaken = 1.0f;       // [Balance] DamageTaken: what Skyrim's people and creatures do to CJ, times
        bool  followDifficulty = true;  // [Balance] FollowDifficulty
    };

    // An ini factor: a number from 0 to kMaxFactor, else a_fallback (empty, not a number, negative).
    inline float ParseFactor(std::string_view a_text, float a_fallback)
    {
        const std::string text(a_text);
        if (text.empty()) {
            return a_fallback;
        }
        char*       end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || !std::isfinite(value) || value < 0.0f) {
            return a_fallback;
        }
        return (std::min)(value, kMaxFactor);
    }

    // An ini switch: 1/0, true/false, on/off, yes/no (any case), else a_fallback.
    inline bool ParseSwitch(std::string_view a_text, bool a_fallback)
    {
        std::string text(a_text);
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "1" || text == "true" || text == "on" || text == "yes") {
            return true;
        }
        if (text == "0" || text == "false" || text == "off" || text == "no") {
            return false;
        }
        return a_fallback;
    }

    inline Settings Parse(std::string_view a_cjDamage, std::string_view a_damageTaken, std::string_view a_followDifficulty)
    {
        const Settings defaults;
        return { ParseFactor(a_cjDamage, defaults.cjDamage), ParseFactor(a_damageTaken, defaults.damageTaken),
            ParseSwitch(a_followDifficulty, defaults.followDifficulty) };
    }

    // A difficulty out of range is Adept's.
    constexpr int Clamp(int a_difficulty) { return a_difficulty >= 0 && a_difficulty < kDifficulties ? a_difficulty : kAdept; }

    // What the damage of CJ's weapon a_type is multiplied by at a_difficulty. Melee (fists, bats, knives, the chainsaw)
    // as it is.
    inline float Dealt(const Settings& a_settings, std::uint8_t a_type, int a_difficulty)
    {
        if (Weapons::IsMelee(a_type)) {
            return 1.0f;
        }
        return a_settings.cjDamage * (a_settings.followDifficulty ? kDealt[Clamp(a_difficulty)] : 1.0f);
    }

    // What a hit of Skyrim's people on the player is multiplied by, a_share being the stars' share for guards and ours
    // (Wanted::GuardDamage), 1 for everyone else.
    inline float Taken(const Settings& a_settings, float a_share)
    {
        return a_settings.damageTaken * (std::isfinite(a_share) ? (std::max)(a_share, 0.0f) : 1.0f);
    }

    // ---- The example fights (docs/DESIGN.md) ----

    struct Target
    {
        std::string_view name;
        std::uint16_t    level;
        float            health;  // Skyrim.esm's (UESP)
        Damage::Class    cls;
    };

    inline constexpr std::array<Target, 5> kTargets = { {
        { "bandit", 6, 90.0f, Damage::Class::Normal },
        { "restless draugr", 6, 150.0f, Damage::Class::Normal },
        { "large frostbite spider", 6, 150.0f, Damage::Class::Normal },
        { "giant frostbite spider", 14, 380.0f, Damage::Class::Normal },
        { "giant", 32, 591.0f, Damage::Class::Giant },
    } };

    // Body shots of a_type for CJ to kill a_target, every shotgun pellet landing.
    inline int ShotsToKill(const Settings& a_settings, std::uint8_t a_type, const Target& a_target, int a_difficulty)
    {
        const float hit = Damage::ToSkyrim(0.0f, a_type, Damage::Part::Torso, a_target.level, a_target.cls) * Dealt(a_settings, a_type, a_difficulty);
        return Damage::HitsToKill(a_target.health, hit * static_cast<float>(Damage::Pellets(a_type)));
    }

    // Hits of a_damage (Skyrim's, at Adept) for one of Skyrim's people to kill CJ with a_health and no armour.
    inline int HitsToDie(const Settings& a_settings, float a_damage, float a_toPlayer, float a_health)
    {
        return Damage::HitsToKill(a_health, a_damage * a_toPlayer * Taken(a_settings, 1.0f));
    }
}
