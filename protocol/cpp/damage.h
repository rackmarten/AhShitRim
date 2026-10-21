// What CJ's weapons do to Skyrim's actors (v13), the game-free half: San Andreas's damage per weapon,
// the body part a hit bone is, the headshot, limb, level and size multipliers that turn San Andreas's
// health points into Skyrim's, the reach of a melee blow, which blows land (once per blow, the chainsaw
// over and over), and the hits counted towards San Andreas's weapon skills. Header-only and free of game
// types, so the protocol tests cover it.
//
// Tuning: San Andreas's peds have 100 health and a pistol does 25. Skyrim's early bandits and draugr have
// about 50 to 100, so a pistol body shot does kScale of its San Andreas damage and kills them in 3 to 5.
// Skyrim's health grows with level; on top of that LevelMultiplier takes damage down for targets above
// kLevelFull, so later on the bigger guns matter, and giants and dragons take a share of it (ClassOf).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "weapons.h"

namespace AhShitRim::Damage
{
    // San Andreas's damage per bullet (weapon.dat's damage, the same at every skill level but the Desert
    // Eagle's, which doubles from gangster up), or per blow for the melee weapons (melee.dat's first
    // attack; San Andreas sends each blow's own). 0 for what isn't a bullet or a blow.
    constexpr float SanAndreasDamage(std::uint8_t a_type, std::uint8_t a_skill = 0)
    {
        using namespace Weapons;
        switch (a_type) {
        case Unarmed:
        case BrassKnuckle: return 6.0f;
        case GolfClub:
        case Nightstick:
        case BaseballBat:
        case Shovel:
        case PoolCue:
        case Cane: return 10.0f;
        case Knife: return 10.0f;
        case Katana: return 10.0f;
        case Chainsaw: return 50.0f;
        case Dildo1:
        case Dildo2:
        case Vibe1:
        case Vibe2:
        case Flowers: return 5.0f;
        case Pistol: return 25.0f;
        case PistolSilenced: return 40.0f;
        case DesertEagle: return a_skill >= 1 ? 140.0f : 70.0f;
        case Shotgun:
        case Sawnoff: return 10.0f;
        case Spas12: return 15.0f;
        case MicroUzi:
        case Tec9: return 20.0f;
        case Mp5: return 25.0f;
        case Ak47:
        case M4: return 30.0f;
        case CountryRifle: return 75.0f;
        case SniperRifle: return 125.0f;
        case Minigun: return 140.0f;
        default: return 0.0f;
        }
    }

    // ---- Shotguns ----

    // San Andreas sends a shotgun's shot as one line; Skyrim fans it out into this many pellets, each doing
    // the shot's damage (weapon.dat's is per pellet), in a cone of SpreadDegrees around the line.
    constexpr int Pellets(std::uint8_t a_type)
    {
        switch (a_type) {
        case Weapons::Shotgun:
        case Weapons::Sawnoff: return 8;
        case Weapons::Spas12: return 6;
        default: return 1;
        }
    }

    constexpr float SpreadDegrees(std::uint8_t a_type)
    {
        switch (a_type) {
        case Weapons::Shotgun: return 3.5f;
        case Weapons::Sawnoff: return 6.0f;
        case Weapons::Spas12: return 3.0f;
        default: return 0.0f;
        }
    }

    struct Dir
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // The a_index-th of a_count pellets around a_dir (a unit vector): the first straight along it, the rest
    // spread evenly over the cone by the golden angle, so every shot has the same pattern.
    inline Dir PelletDirection(const Dir& a_dir, int a_index, int a_count, float a_spreadDegrees)
    {
        if (a_index <= 0 || a_count <= 1) {
            return a_dir;
        }
        // Two axes across the line.
        Dir side = std::fabs(a_dir.z) < 0.9f ? Dir{ -a_dir.y, a_dir.x, 0.0f } : Dir{ 0.0f, -a_dir.z, a_dir.y };
        const float sideLength = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        side = { side.x / sideLength, side.y / sideLength, side.z / sideLength };
        const Dir up{ a_dir.y * side.z - a_dir.z * side.y, a_dir.z * side.x - a_dir.x * side.z, a_dir.x * side.y - a_dir.y * side.x };
        const float off = std::tan(a_spreadDegrees * 3.14159265f / 180.0f) * std::sqrt(static_cast<float>(a_index) / static_cast<float>(a_count - 1));
        const float turn = static_cast<float>(a_index) * 2.39996323f;
        Dir out{ a_dir.x + off * (std::cos(turn) * side.x + std::sin(turn) * up.x), a_dir.y + off * (std::cos(turn) * side.y + std::sin(turn) * up.y),
            a_dir.z + off * (std::cos(turn) * side.z + std::sin(turn) * up.z) };
        const float length = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
        return { out.x / length, out.y / length, out.z / length };
    }

    // ---- Where it hit ----

    enum class Part : std::uint8_t
    {
        Torso,
        Head,
        Limb,
    };

    inline std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        for (auto& c : out) {
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        return out;
    }

    // The body part a skeleton node is, by its name: Skyrim's humanoid skeleton ("NPC Head [Head]",
    // "NPC L Thigh [LThg]") and the creatures' ("Canine_Head", "NPC Tail1", a dragon's "NPC Head"). A node
    // that says nothing (the actor's root, its character controller) is the torso.
    inline Part PartOfNode(std::string_view a_node)
    {
        const auto name = Lower(a_node);
        const auto has = [&](std::string_view a_part) { return name.find(a_part) != std::string::npos; };
        if (has("head") && !has("headtrack")) {
            return Part::Head;
        }
        for (const auto limb : { "arm", "hand", "finger", "thumb", "thigh", "calf", "leg", "foot", "toe", "tail", "wing", "clavicle" }) {
            if (has(limb)) {
                return Part::Limb;
            }
        }
        return Part::Torso;
    }

    // ---- Who it hit ----

    enum class Class : std::uint8_t
    {
        Normal,
        Giant,   // giants and mammoths
        Dragon,  // dragons, Alduin, the undead ones (not dragon priests)
    };

    // The class of an actor by its race's editor id and whether the race has the ActorTypeDragon keyword.
    inline Class ClassOf(std::string_view a_raceEditorId, bool a_dragonKeyword)
    {
        const auto race = Lower(a_raceEditorId);
        if (a_dragonKeyword || ((race.find("dragon") != std::string::npos || race.find("alduin") != std::string::npos) &&
                                   race.find("priest") == std::string::npos)) {
            return Class::Dragon;
        }
        if (race.find("giant") != std::string::npos || race.find("mammoth") != std::string::npos) {
            return Class::Giant;
        }
        return Class::Normal;
    }

    // ---- How much ----

    inline constexpr float          kScale = 0.8f;      // San Andreas health points to Skyrim's
    inline constexpr std::uint16_t  kLevelFull = 6;     // up to this level a target takes full damage
    inline constexpr float          kLevelFalloff = 0.03f;  // then a share less per level above it
    inline constexpr float          kLevelFloor = 0.4f;
    inline constexpr float          kHeadshot = 3.0f;
    inline constexpr float          kHeadshotRifle = 10.0f;  // the sniper and country rifles: a head is a kill
    inline constexpr float          kLimb = 0.75f;
    inline constexpr float          kGiant = 0.5f;
    inline constexpr float          kDragon = 0.25f;

    inline float LevelMultiplier(std::uint16_t a_level)
    {
        if (a_level <= kLevelFull) {
            return 1.0f;
        }
        return (std::max)(kLevelFloor, 1.0f / (1.0f + kLevelFalloff * static_cast<float>(a_level - kLevelFull)));
    }

    constexpr float PartMultiplier(Part a_part, std::uint8_t a_type)
    {
        switch (a_part) {
        case Part::Head: return a_type == Weapons::SniperRifle || a_type == Weapons::CountryRifle ? kHeadshotRifle : kHeadshot;
        case Part::Limb: return kLimb;
        default: return 1.0f;
        }
    }

    constexpr float ClassMultiplier(Class a_class)
    {
        switch (a_class) {
        case Class::Giant: return kGiant;
        case Class::Dragon: return kDragon;
        default: return 1.0f;
        }
    }

    // The explosives (rockets, grenades, Molotovs, satchel charges) hit everyone in full: a blast doesn't care
    // how big or seasoned its target is, so a mammoth takes about five rockets and a dragon not many more.
    constexpr bool Explosive(std::uint8_t a_type)
    {
        return a_type == Weapons::Grenade || a_type == Weapons::Molotov || a_type == Weapons::RocketLauncher || a_type == Weapons::RocketLauncherHs ||
               a_type == Weapons::SatchelCharge;
    }

    // Skyrim health a hit takes: San Andreas's damage for it (the shot's own, else the table's) and where
    // and whom it hit; the explosives without the level and size falloff. Skyrim's difficulty doesn't scale it.
    inline float ToSkyrim(float a_sanAndreas, std::uint8_t a_type, Part a_part, std::uint16_t a_level, Class a_class)
    {
        const float base = std::isfinite(a_sanAndreas) && a_sanAndreas > 0.0f ? a_sanAndreas : SanAndreasDamage(a_type);
        if (Explosive(a_type)) {
            return base * kScale * PartMultiplier(a_part, a_type);
        }
        return base * kScale * PartMultiplier(a_part, a_type) * LevelMultiplier(a_level) * ClassMultiplier(a_class);
    }

    // How many hits of a_damage a target with a_health takes to die.
    inline int HitsToKill(float a_health, float a_damage)
    {
        if (!(a_damage > 0.0f)) {
            return 0;
        }
        return static_cast<int>(std::ceil(a_health / a_damage - 1e-4f));
    }

    // ---- Melee ----

    struct Vec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // Whether a blow centred at a_point with a_radius reaches an actor standing at a_feet as a capsule of
    // a_height and a_radius (Skyrim units). Returns the height of the contact above the feet in a_atHeight.
    inline bool Reaches(const Vec3& a_point, float a_strikeRadius, const Vec3& a_feet, float a_height, float a_radius, float* a_atHeight = nullptr)
    {
        const float top = (std::max)(a_height - a_radius, 0.0f);
        const float bottom = (std::min)(a_radius, top);
        const float z = std::clamp(a_point.z - a_feet.z, bottom, top);
        const float dx = a_point.x - a_feet.x;
        const float dy = a_point.y - a_feet.y;
        const float dz = a_point.z - (a_feet.z + z);
        if (a_atHeight) {
            *a_atHeight = std::clamp(a_point.z - a_feet.z, 0.0f, a_height);
        }
        const float reach = a_strikeRadius + a_radius;
        return dx * dx + dy * dy + dz * dz <= reach * reach;
    }

    inline constexpr float kMeleeScale = 2.5f;      // a blow does this many times its melee.dat damage (on top of kScale)
    inline constexpr float kChainsawTick = 0.1f;    // seconds between the chainsaw's cuts on one actor, at least
    inline constexpr float kChainsawGap = 0.4f;     // a longer pause than this between cuts counts as this long
    inline constexpr float kChainsawPerSecond = 150.0f;  // San Andreas health points the chainsaw cuts a second

    // The blows that land: each blow on each actor once, the chainsaw on each actor every kChainsawTick or
    // more for as long as it cuts. Keyed by whatever names an actor (its form id).
    class Blows
    {
    public:
        static constexpr std::size_t kRemembered = 16;

        // Whether the blow a_strike lands on a_actor at a_now (seconds), a_continuous for the chainsaw: 0 when
        // it doesn't; 1 for a blow; for the chainsaw the seconds it cut since its last cut on the actor
        // (kChainsawTick for the first, kChainsawGap at most).
        float Lands(std::uint32_t a_actor, std::uint32_t a_strike, double a_now, bool a_continuous)
        {
            for (auto& entry : _entries) {
                if (entry.used && entry.actor == a_actor) {
                    const double since = a_now - entry.at;
                    if (a_continuous ? since < kChainsawTick - 1e-6 : entry.strike == a_strike) {
                        return 0.0f;
                    }
                    entry.strike = a_strike;
                    entry.at = a_now;
                    return a_continuous ? static_cast<float>((std::min)(since, static_cast<double>(kChainsawGap))) : 1.0f;
                }
            }
            auto& slot = _entries[_next];
            _next = (_next + 1) % kRemembered;
            slot = { true, a_actor, a_strike, a_now };
            return a_continuous ? kChainsawTick : 1.0f;
        }

        void Clear() { *this = {}; }

    private:
        struct Entry
        {
            bool          used = false;
            std::uint32_t actor = 0;
            std::uint32_t strike = 0;
            double        at = 0.0;
        };
        Entry       _entries[kRemembered] = {};
        std::size_t _next = 0;
    };

    // A landed blow's damage in San Andreas health points, for ToSkyrim: melee.dat's times kMeleeScale (San
    // Andreas's peds have far less health than Skyrim's people), the chainsaw's by the seconds it cut.
    constexpr float BlowDamage(float a_damage, std::uint8_t a_type, float a_landed)
    {
        return a_type == Weapons::Chainsaw ? kChainsawPerSecond * a_landed : a_damage * kMeleeScale * a_landed;
    }

    // The chance a landed blow staggers its target (Skyrim's stagger): the heavier the weapon the likelier; the
    // chainsaw always, so whoever it cuts can't fight back.
    constexpr float StaggerChance(std::uint8_t a_type)
    {
        using namespace Weapons;
        switch (a_type) {
        case Chainsaw: return 1.0f;
        case Unarmed:
        case BrassKnuckle: return 0.25f;
        case Knife:
        case Katana: return 0.35f;
        case GolfClub:
        case Nightstick:
        case BaseballBat:
        case Shovel:
        case PoolCue:
        case Cane: return 0.5f;
        default: return 0.15f;  // the gifts
        }
    }

    // Skyrim's staggerDirection for a blow from a_fromHeading (where the attacker stands, as a heading from the
    // victim) on a victim facing a_facing (radians, Skyrim's: 0 north, clockwise): 0 from the front, 0.5 from
    // behind, going round clockwise.
    inline float StaggerDirection(float a_facing, float a_fromHeading)
    {
        constexpr float kTwoPi = 6.28318531f;
        float turn = std::fmod(a_fromHeading - a_facing, kTwoPi);
        if (turn < 0.0f) {
            turn += kTwoPi;
        }
        return turn / kTwoPi;
    }

    // ---- Headshots ----

    inline constexpr float kHeadRadius = 11.0f;  // Skyrim units round a person's head bone, at scale 1
    inline constexpr float kLegs = 0.4f;         // a body hit below this share of the actor's height is a leg

    // How big a head is for an actor of a_class at a_scale: giants' and dragons' are bigger.
    constexpr float HeadRadius(Class a_class, float a_scale)
    {
        return kHeadRadius * a_scale * (a_class == Class::Dragon ? 4.0f : a_class == Class::Giant ? 1.8f : 1.0f);
    }

    // Whether the line from a_origin along a_dir (a unit vector) passes within a_radius of a_centre, ahead of a_origin.
    inline bool PassesWithin(const Vec3& a_origin, const Vec3& a_dir, const Vec3& a_centre, float a_radius)
    {
        const Vec3  to{ a_centre.x - a_origin.x, a_centre.y - a_origin.y, a_centre.z - a_origin.z };
        const float along = to.x * a_dir.x + to.y * a_dir.y + to.z * a_dir.z;
        if (along < -a_radius) {
            return false;
        }
        const float dx = to.x - a_dir.x * along, dy = to.y - a_dir.y * along, dz = to.z - a_dir.z * along;
        return dx * dx + dy * dy + dz * dz <= a_radius * a_radius;
    }

    // The body part of a hit on an actor's body: the head when the line passed through it, a leg low down
    // (a_height is the hit's height over the actor's), else the torso.
    constexpr Part PartOfHit(bool a_head, float a_height)
    {
        return a_head ? Part::Head : a_height < kLegs ? Part::Limb : Part::Torso;
    }

    // ---- Skills ----

    // Hits counted per weapon until they go to San Andreas (CStats::UpdateStatsWhenWeaponHit, once each).
    class SkillHits
    {
    public:
        void Add(std::uint8_t a_type) { _count[a_type]++; }

        // Calls a_send(type, hits) for each weapon with hits and forgets those it sent (a_send returns true).
        template <class F>
        void Flush(F&& a_send)
        {
            for (std::uint32_t type = 0; type < 256; ++type) {
                if (_count[type] && a_send(static_cast<std::uint8_t>(type), _count[type])) {
                    _count[type] = 0;
                }
            }
        }

        void Clear() { *this = {}; }

    private:
        std::uint32_t _count[256] = {};
    };
}
