// How hard CJ hits and how hard he is hit, the game-free half: AhShitRim.ini's [Balance] factors and Skyrim's
// difficulty on top of the damage the rest works out. Header-only and free of game types, so the protocol tests
// cover it; the SKSE plugin feeds it (skse/src/player/CombatBalance.cpp).
//
// Balanced for the fights a player has most, bandits, draugr, spiders, giants: the wanted level's share for guards
// and ours (Wanted::GuardDamage) is a layer of its own on top, for them only.
// - CJ's guns and explosives (Damage::ToSkyrim, Shots): times CJDamage, and Skyrim's "damage by the player"
//   multiplier for the difficulty (fDiffMultHPByPC*: half at Master) when FollowDifficulty is on, since Skyrim's
//   own hit doesn't apply it to ours as it does to the player's sword. Melee is left as it is.
// - Skyrim's people's and creatures' hits on the player: Skyrim has already put its difficulty on them
//   (fDiffMultHPToPC*), so only times DamageTaken, then CJ's armour takes them first (Health::AbsorbSkyrim).
//
// Tuning: the user plays at Master, where a Colt 45 body shot took 20 off a level 6 bandit (91 health). Following the
// difficulty halves the guns there (10 a body shot); unarmed CJ already took about the right damage, so DamageTaken
// stays 1. kFights below are the example fights at Master with the defaults; the doctests hold them.

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

    // The suffix of each difficulty's game settings: fDiffMultHPByPC<suffix>, fDiffMultHPToPC<suffix>.
    inline constexpr std::array<std::string_view, kDifficulties> kSuffixes = { "VE", "E", "N", "H", "VH", "L" };

    // Skyrim.esm's values, for when a setting can't be read: what the player deals and takes.
    inline constexpr std::array<float, kDifficulties> kByPlayer = { 2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.25f };
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

    // A multiplier read from the game, else Skyrim.esm's for that difficulty.
    inline float OrDefault(float a_read, const std::array<float, kDifficulties>& a_table, int a_difficulty)
    {
        return std::isfinite(a_read) && a_read > 0.0f ? a_read : a_table[Clamp(a_difficulty)];
    }

    // What the damage of CJ's weapon a_type is multiplied by: a_byPlayer is the difficulty's fDiffMultHPByPC. Melee
    // (fists, bats, knives, the chainsaw) as it is.
    inline float Dealt(const Settings& a_settings, std::uint8_t a_type, float a_byPlayer)
    {
        if (Weapons::IsMelee(a_type)) {
            return 1.0f;
        }
        return a_settings.cjDamage * (a_settings.followDifficulty ? a_byPlayer : 1.0f);
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
    inline int ShotsToKill(const Settings& a_settings, std::uint8_t a_type, const Target& a_target, float a_byPlayer)
    {
        const float hit = Damage::ToSkyrim(0.0f, a_type, Damage::Part::Torso, a_target.level, a_target.cls) * Dealt(a_settings, a_type, a_byPlayer);
        return Damage::HitsToKill(a_target.health, hit * static_cast<float>(Damage::Pellets(a_type)));
    }

    // Hits of a_damage (Skyrim's, at Adept) for one of Skyrim's people to kill CJ with a_health and no armour.
    inline int HitsToDie(const Settings& a_settings, float a_damage, float a_toPlayer, float a_health)
    {
        return Damage::HitsToKill(a_health, a_damage * a_toPlayer * Taken(a_settings, 1.0f));
    }
}
