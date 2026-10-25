// What CJ's shots, blows, fire and blasts do to Skyrim's objects, the game-free half: spider webs, the
// hanging oil lamps and the oil on the ground, and anything else destructible. Header-only and free of
// game types, so the protocol tests cover it.
//
// Skyrim's own traps listen for two things (their scripts in Skyrim - Misc.bsa):
//  - a hit (Papyrus OnHit, a TESHitEvent): TrapFallingOilLamp falls on any hit; TrapOilPool (TrapExplosiveGas)
//    ignites when the hit's source is Torch01, has the fire keyword or is on its lists of igniting weapons,
//  - the object's destruction (OnDestructionStageChanged, from damage to its destructible data): the webs
//    (WebDestructibleSCRIPT) tear on it and free whoever is stuck in them, a lamp falls at stage 1, the oil
//    pool burns from stage 2 (its own GasExplode damages it by 5).
// So every hit CJ lands on an object is sent as a hit, from the player, with a source Skyrim's scripts know:
// a bow for a bullet, a sword, a mace or bare hands for a blow, a torch for his fire and blasts. A destructible
// object also takes damage, a bullet's or a blow's as Skyrim's health (Damage::ToSkyrim) and enough from fire
// or a blast to destroy a web outright, as Skyrim's fire spells do.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "damage.h"
#include "weapons.h"

namespace AhShitRim::Traps
{
    enum class Hit : std::uint8_t
    {
        Bullet,
        Blow,
        Fire,   // the flamethrower, a Molotov's burst and its oil fire
        Blast,  // a rocket, a grenade, a satchel charge, a vehicle blowing up
    };

    // The source a hit is sent with: Skyrim's own weapon forms (Skyrim.esm).
    inline constexpr std::uint32_t kHuntingBow = 0x00013985;
    inline constexpr std::uint32_t kIronSword = 0x00012EB7;
    inline constexpr std::uint32_t kIronMace = 0x00013982;
    inline constexpr std::uint32_t kUnarmed = 0x000001F4;  // bare hands (the WEAP the game uses for fists)
    inline constexpr std::uint32_t kTorch = 0x0001D4EC;    // Torch01: TrapExplosiveGas ignites on a hit from it

    // Which of San Andreas's blows cut (the knife, the katana, the chainsaw) and which hit blunt.
    constexpr bool Cuts(std::uint8_t a_weapon)
    {
        return a_weapon == Weapons::Knife || a_weapon == Weapons::Katana || a_weapon == Weapons::Chainsaw;
    }

    constexpr std::uint32_t SourceOf(Hit a_hit, std::uint8_t a_weapon)
    {
        switch (a_hit) {
        case Hit::Bullet: return kHuntingBow;
        case Hit::Blow: return a_weapon == Weapons::Unarmed || a_weapon == Weapons::BrassKnuckle ? kUnarmed : Cuts(a_weapon) ? kIronSword : kIronMace;
        default: return kTorch;
        }
    }

    // Damage to a destructible object: Skyrim health, as the hit would take from an actor of level 1; fire and
    // blasts at least kBurns, which tears any web.
    inline constexpr float kBurns = 100.0f;

    inline float ObjectDamage(Hit a_hit, float a_skyrimDamage)
    {
        const float damage = std::isfinite(a_skyrimDamage) ? (std::max)(a_skyrimDamage, 0.0f) : 0.0f;
        return a_hit == Hit::Fire || a_hit == Hit::Blast ? (std::max)(damage, kBurns) : damage;
    }

    // A bullet's or a blow's San Andreas damage as an object takes it.
    inline float SkyrimDamage(float a_sanAndreas, std::uint8_t a_weapon)
    {
        return Damage::ToSkyrim(a_sanAndreas, a_weapon, Damage::Part::Torso, 1, Damage::Class::Normal);
    }

    // The objects a hit is sent to: those whose scripts may listen (activators: the lamps, the oil, levers,
    // switches; movable statics; furniture and containers) and anything destructible. Plain statics and the
    // land aren't told.
    enum class Base : std::uint8_t
    {
        Static,
        Activator,
        MovableStatic,
        Furniture,
        Container,
        Door,
        Other,
    };

    constexpr bool Told(Base a_base, bool a_destructible)
    {
        return a_destructible || a_base == Base::Activator || a_base == Base::MovableStatic || a_base == Base::Furniture || a_base == Base::Container;
    }

    // Collision around CJ (StaticMeshes): a destructible object's bodies on the props, trap or clutter layer
    // (the webs) are kept until it is destroyed, as they stop Skyrim's player. Skyrim's COL_LAYER numbers.
    inline constexpr int kLayerClutter = 4;
    inline constexpr int kLayerProps = 10;
    inline constexpr int kLayerTrap = 14;

    constexpr bool KeepsCollision(int a_layer, bool a_destructible)
    {
        return a_destructible && (a_layer == kLayerProps || a_layer == kLayerTrap || a_layer == kLayerClutter);
    }

    // After damage, the chunks round the object are read again once its destroyed model is in.
    inline constexpr float kRereadAfter = 1.0f;  // seconds

    // One object takes CJ's fire at most this often: the flamethrower puffs every frame, a Molotov's patch every
    // half second, and an oil pool or a web needs it once.
    inline constexpr double kFireGap = 1.0;  // seconds

    // The objects fire has reached lately, by form id: whether a_object takes it again at a_now (seconds).
    class Scorched
    {
    public:
        static constexpr std::size_t kRemembered = 32;

        bool Takes(std::uint32_t a_object, double a_now)
        {
            for (auto& entry : _entries) {
                if (entry.object == a_object && entry.used) {
                    if (a_now - entry.at < kFireGap) {
                        return false;
                    }
                    entry.at = a_now;
                    return true;
                }
            }
            _entries[_next] = { true, a_object, a_now };
            _next = (_next + 1) % kRemembered;
            return true;
        }

        void Clear() { *this = {}; }

    private:
        struct Entry
        {
            bool          used = false;
            std::uint32_t object = 0;
            double        at = 0.0;
        };
        std::array<Entry, kRemembered> _entries{};
        std::size_t                    _next = 0;
    };
}
