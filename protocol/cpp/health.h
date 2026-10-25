// CJ's health and armour (v11), the game-free half. Skyrim's health is the only health there is:
// San Andreas keeps CJ at a fixed full health plus a buffer, works out the damage it would do to him
// (a fall, a crash, an explosion) and reports what it took off as a running total; Skyrim takes that
// off its player, armour first. Header-only and free of game types, so the protocol tests cover it;
// the SKSE plugin (skse/src/player/Vitals.cpp) and the San Andreas ASI (sa/src/health.cpp) feed it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace AhShitRim::Health
{
    // San Andreas's health points in a full bar (and in a full armour bar): the unit the damage comes in.
    inline constexpr float kSaFullHealth = 100.0f;

    // CJ's health in San Andreas, reset every frame. A hit that would have killed a full bar takes a
    // tenth of this, so he is never dead in San Andreas; only Skyrim's player can die.
    inline constexpr float kSaHealth = 1000.0f;

    // What a frame of San Andreas took off CJ: the health set before it minus what he has after (never
    // negative, so healing does nothing, and never not a number).
    inline float Lost(float a_before, float a_after)
    {
        const float lost = a_before - a_after;
        return std::isfinite(lost) && lost > 0.0f ? lost : 0.0f;
    }

    // Turns the running total of damage into new damage, so a missed frame loses nothing. The first
    // value only seeds it (a San Andreas that has run a while has a total already); a total that went
    // down (San Andreas restarted) seeds it again.
    class Meter
    {
    public:
        float Take(float a_total)
        {
            if (!std::isfinite(a_total)) {
                return 0.0f;
            }
            if (!_seeded || a_total < _last) {
                _seeded = true;
                _last = a_total;
                return 0.0f;
            }
            const float damage = a_total - _last;
            _last = a_total;
            return damage;
        }

        void Reset() { _seeded = false; }

    private:
        bool  _seeded = false;
        float _last = 0.0f;
    };

    struct Split
    {
        float armor = 0.0f;   // what is left of the armour, San Andreas points (0..100)
        float health = 0.0f;  // Skyrim health points to take off
    };

    // Damage in San Andreas points (100 = a full bar) hits the armour first, as in San Andreas, and
    // what it doesn't absorb is a share of Skyrim's maximum health: a fall that would empty a San
    // Andreas health bar empties Skyrim's.
    inline Split Absorb(float a_armor, float a_damage, float a_skyrimMax)
    {
        Split s;
        a_armor = std::clamp(std::isfinite(a_armor) ? a_armor : 0.0f, 0.0f, kSaFullHealth);
        a_damage = std::isfinite(a_damage) ? (std::max)(a_damage, 0.0f) : 0.0f;
        const float absorbed = (std::min)(a_armor, a_damage);
        s.armor = a_armor - absorbed;
        s.health = (a_damage - absorbed) / kSaFullHealth * (std::max)(a_skyrimMax, 0.0f);
        return s;
    }

    // ---- What hurt CJ ----

    // What took Skyrim's health off the player while CJ has it, as far as Skyrim tells.
    enum class Kind : std::uint8_t
    {
        Blow,      // a weapon, an arrow, a creature's bite, claw or club, a trap's blade, spikes or rocks, a blast
        Fire,      // a fire spell, a fire trap, burning oil
        Frost,
        Shock,
        Magic,     // any other harmful magic (an absorb, a drain)
        Poison,
        Disease,
        Fall,
        Drowning,
    };

    // The actor value that resists a magic effect (EffectSetting's resist variable), as far as it matters here.
    enum class Resist : std::uint8_t
    {
        None,
        Fire,
        Frost,
        Shock,
        Magic,
        Poison,
        Disease,
    };

    // San Andreas's armour takes every kind of damage but drowning and falls (CPedDamageResponseCalculator::
    // AccountForPedArmour); Skyrim's poison and disease have nothing like them there and go to health too.
    constexpr bool ArmourTakes(Kind a_kind)
    {
        return a_kind != Kind::Poison && a_kind != Kind::Disease && a_kind != Kind::Fall && a_kind != Kind::Drowning;
    }

    // Fire sets CJ alight, as San Andreas's does.
    constexpr bool SetsAlight(Kind a_kind) { return a_kind == Kind::Fire; }

    // A harmful magic effect's kind, by what resists it.
    constexpr Kind OfEffect(Resist a_resist)
    {
        switch (a_resist) {
        case Resist::Fire: return Kind::Fire;
        case Resist::Frost: return Kind::Frost;
        case Resist::Shock: return Kind::Shock;
        case Resist::Poison: return Kind::Poison;
        case Resist::Disease: return Kind::Disease;
        default: return Kind::Magic;
        }
    }

    // A hit with no magic effect behind it: a blow, whoever (or whatever trap) dealt it, but drowning when nobody did
    // and the player is under water. Skyrim's own falls never land while CJ has the player (PlayerSync zeroes them).
    constexpr Kind OfHit(bool a_attacker, bool a_underwater) { return !a_attacker && a_underwater ? Kind::Drowning : Kind::Blow; }

    // A hit Skyrim's people landed (a_damage of Skyrim's health) through CJ's armour first, as San Andreas's
    // AccountForPedArmour takes a bullet or a blow: its share of a full bar is that many San Andreas points, the
    // armour takes them point for point and the rest is the health to take.
    inline Split AbsorbSkyrim(float a_armor, float a_damage, float a_skyrimMax)
    {
        if (!(a_skyrimMax > 0.0f) || !std::isfinite(a_skyrimMax)) {
            return { std::clamp(std::isfinite(a_armor) ? a_armor : 0.0f, 0.0f, kSaFullHealth), std::isfinite(a_damage) ? (std::max)(a_damage, 0.0f) : 0.0f };
        }
        a_damage = std::isfinite(a_damage) ? (std::max)(a_damage, 0.0f) : 0.0f;
        return Absorb(a_armor, a_damage / a_skyrimMax * kSaFullHealth, a_skyrimMax);
    }
}
