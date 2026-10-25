// San Andreas's pickups in Skyrim's world (v27), the game-free half: the spots they float at, taking one by
// walking into it as San Andreas's CPickup::Update does, what stops CJ taking one, the ammo a weapon comes with,
// when a taken one comes back, the one-offs the co-save remembers, and the heart that turns up when CJ is
// hurt. Header-only and free of game types, so the protocol tests cover it; the SKSE plugin
// (skse/src/player/Pickups.cpp) draws them with San Andreas's own models and gives what they hold.
//
// The spots are Skyrim world coordinates (Skyrim units, 70 to the metre) read from Skyrim.esm: most stand on a
// location's map marker (LCTN's world location marker), where the map's fast travel puts the player, a parachute
// on the high ones. The plugin sets each down on whatever is under it, so a spot's height only needs to be near.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "damage.h"
#include "pose.h"
#include "shop.h"
#include "weapons.h"

namespace AhShitRim::Pickups
{
    using Kind = Protocol::PickupKind;
    using Vec3 = Damage::Vec3;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // ---- How one looks ----

    inline constexpr float kSize = 2.0f;                 // drawn at twice San Andreas's size: in Skyrim's wide world they were too small to spot
    inline constexpr float kFloat = 1.0f * kMeter;       // its middle over the ground it rests on
    inline constexpr float kTurnSeconds = 2.0f;           // a whole turn round its upright, as San Andreas's spin
    inline constexpr float kDrawRange = 100.0f * kMeter;  // drawn this close to CJ (CPickup::IsVisible's 100 m)

    // How far round its upright one is a_seconds into the game: all of them turn together, as in San Andreas.
    inline float SpinAt(double a_seconds)
    {
        const double turns = a_seconds / kTurnSeconds;
        return static_cast<float>((turns - std::floor(turns)) * 6.283185307179586);
    }

    // ---- Taking one ----

    // CPickup::Update: CJ takes one on foot when it is within the square root of 1.8 m of him across and less
    // than 2 m above or below his middle (a metre over his feet). In a vehicle he drives through it.
    inline constexpr float kTouchAcross = 1.3416408f * kMeter;
    inline constexpr float kTouchUpDown = 2.0f * kMeter;
    inline constexpr float kCjMiddle = 1.0f * kMeter;

    inline bool Touches(const Vec3& a_feet, const Vec3& a_pickup)
    {
        const float dx = a_pickup.x - a_feet.x, dy = a_pickup.y - a_feet.y;
        return dx * dx + dy * dy < kTouchAcross * kTouchAcross && std::abs(a_pickup.z - (a_feet.z + kCjMiddle)) < kTouchUpDown;
    }

    // What CJ has, for what he would take.
    struct Cj
    {
        float health = 0.0f;     // Skyrim's
        float healthMax = 0.0f;
        float armour = 0.0f;     // San Andreas points, 0..100
        bool  jetpack = false;   // has it on
        bool  inVehicle = false;
        Weapons::Loadout loadout{};  // what he carries, by San Andreas's slot (the pose's)
    };

    // CPickup::Update's CanPlayerPick: a heart isn't taken at full health, armour at full armour, the jetpack
    // while he wears one; nothing from a vehicle.
    inline bool Wants(Kind a_kind, const Cj& a_cj)
    {
        if (a_cj.inVehicle) {
            return false;
        }
        switch (a_kind) {
        case Kind::Health: return a_cj.healthMax > 0.0f && a_cj.health < a_cj.healthMax - 0.2f;
        case Kind::Armour: return a_cj.armour < 100.0f - 0.2f;
        case Kind::Jetpack: return !a_cj.jetpack;
        case Kind::None: return false;
        default: return true;
        }
    }

    // The ammo a weapon picked up off the street comes with: San Andreas's AmmoForWeapon_OnStreet, by weapon type.
    inline constexpr std::array<std::uint16_t, Weapons::kLastCarried + 1> kStreetAmmo{ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 8, 8, 8, 8, 4, 4,
        30, 10, 10, 15, 10, 10, 60, 60, 80, 80, 60, 20, 10, 4, 3, 100, 500, 5, 1, 500, 500, 36, 0, 0, 1 };

    inline std::uint32_t StreetAmmo(std::uint8_t a_weapon) { return a_weapon < kStreetAmmo.size() ? (std::max)(kStreetAmmo[a_weapon], std::uint16_t{ 1 }) : 1; }

    // ---- Walking into a weapon ----
    //
    // As San Andreas's CPickup::Update, by what CJ already holds in the weapon's slot (the pose's loadout):
    // - the same weapon, an empty slot, or a weapon with no rounds left outside the shotgun, SMG and rifle slots: he takes
    //   it (the same weapon's rounds go on top: CPed::GiveWeapon),
    // - another weapon with rounds left: he keeps his, the help box says E takes this one instead (PU_CF1's offer), and
    //   pressing E there swaps (CollectPickupJustDown),
    // - another weapon in the shotgun, SMG or rifle slot, with rounds or none: the pickup's rounds go into his first
    //   (ExtractAmmoFromPickup, once: it is empty after), then the same offer.
    // A swap is CPed::GiveWeapon with the pickup's rounds left: in the shotgun, SMG and rifle slots the old gun's rounds
    // go over to the new one, in every other slot (the handguns too) they go with the old gun, which is gone.

    enum class Walk : std::uint8_t
    {
        Take,
        Offer,  // nothing taken: E swaps
        Ammo,   // its rounds into his, then the offer
    };

    // The slots whose rounds a swap keeps and whose pickups give rounds to the gun he has: shotguns, SMGs and rifles.
    constexpr bool SharesRounds(std::uint32_t a_slot) { return a_slot >= 3 && a_slot <= 5; }

    // a_held: what CJ holds in the slot of a_weapon. a_emptied: the pickup gave its rounds away already.
    inline Walk OnWalk(const Protocol::WeaponEntry& a_held, std::uint8_t a_weapon, bool a_emptied)
    {
        const auto slot = Shop::SlotOf(a_weapon);
        if (a_held.type == a_weapon || a_held.type == Weapons::Unarmed || (a_held.ammo == 0 && !SharesRounds(slot))) {
            return Walk::Take;
        }
        return SharesRounds(slot) && !a_emptied ? Walk::Ammo : Walk::Offer;
    }

    // What CJ holds in the slot after swapping for a pickup with a_rounds left in it: its weapon, with his old gun's
    // rounds on top in the shotgun, SMG and rifle slots.
    inline Protocol::WeaponEntry AfterSwap(const Protocol::WeaponEntry& a_held, std::uint8_t a_weapon, std::uint32_t a_rounds)
    {
        Protocol::WeaponEntry out{};
        out.type = a_weapon;
        out.ammo = (std::min)(a_rounds + (SharesRounds(Shop::SlotOf(a_weapon)) ? a_held.ammo : 0u), Weapons::kAmmoMax);
        return out;
    }

    // San Andreas shows its offer once for each pickup and 10 times a game at most (CPickups::DisplayHelpMessage).
    inline constexpr int kOfferHelpMax = 10;

    // ---- Coming back ----

    enum class Respawn : std::uint8_t
    {
        Once,    // taken for good, and the co-save remembers it
        Street,  // San Andreas's PICKUP_ON_STREET: back 30 s after it was taken
        Slow,    // armour and the heavy weapons: back after 6 minutes, so they aren't there for every fight
    };

    inline constexpr float kStreetSeconds = 30.0f;
    inline constexpr float kSlowSeconds = 360.0f;
    inline constexpr float kBackAway = 10.0f * kMeter;  // and only once CJ is this far off (CPickup::Update's 10 m)

    inline float RespawnSeconds(Respawn a_respawn)
    {
        switch (a_respawn) {
        case Respawn::Street: return kStreetSeconds;
        case Respawn::Slow: return kSlowSeconds;
        default: return INFINITY;
        }
    }

    // ---- The spots ----

    // The worldspaces the spots are in (their form ids in Skyrim.esm).
    enum class World : std::uint8_t
    {
        Tamriel,
        Whiterun,  // WhiterunWorld, the city behind its gate
    };

    inline constexpr std::uint32_t WorldForm(World a_world) { return a_world == World::Whiterun ? 0x0001A26Fu : 0x0000003Cu; }

    // InputState.world (v36): the world a spot is in, as Skyrim tells San Andreas where the player is.
    inline constexpr std::uint8_t WorldByte(World a_world) { return a_world == World::Whiterun ? Protocol::kWorldWhiterun : Protocol::kWorldTamriel; }

    // Where the player is, for InputState.world: a_form the worldspace's form id, kWorldNone in an interior or a
    // worldspace no table uses (another city's, Solstheim's).
    inline constexpr std::uint8_t WorldByteOf(std::uint32_t a_form, bool a_interior)
    {
        if (a_interior) {
            return Protocol::kWorldNone;
        }
        for (auto world : { World::Tamriel, World::Whiterun }) {
            if (a_form == WorldForm(world)) {
                return WorldByte(world);
            }
        }
        return Protocol::kWorldNone;
    }

    // Skyrim's nine holds, so the tables can be checked to cover each.
    enum class Hold : std::uint8_t
    {
        Whiterun,
        Falkreath,
        Reach,
        Haafingar,
        Hjaalmarch,
        Pale,
        Winterhold,
        Eastmarch,
        Rift,
    };

    inline constexpr int kHolds = 9;

    struct Spot
    {
        std::uint16_t    id;        // for good: the co-save names one-offs by it
        Kind             kind;
        std::uint8_t     weapon;    // a weapon's type
        std::uint16_t    amount;    // a weapon's ammo (0: its street ammo), a wad's gold
        Respawn          respawn;
        World            world;
        Hold             hold;
        Vec3             at;        // Skyrim units
        bool             top;       // on top of something (a roof): set down on the first thing under it from high above
        std::string_view name;
    };

    // A spot set down 2 m to one side of the marker it shares with another.
    inline constexpr float kBeside = 2.0f * kMeter;

    // clang-format off
    inline constexpr Spot kSpots[] = {
        // Parachutes on high spots, for base jumps.
        { 1, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 56368.0f, -48511.0f, 36379.0f }, false, "the Throat of the World" },
        { 2, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 52071.0f, -36267.0f, 22569.0f }, false, "High Hrothgar's terrace" },
        { 3, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 1482.0f, -41162.0f, 5727.0f }, false, "the top of Bleak Falls Barrow" },
        { 4, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Whiterun, Hold::Whiterun, { 28313.0f, 1734.0f, 602.0f }, true, "the top of Dragonsreach" },
        { 5, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 66950.0f, -99471.0f, 17302.0f }, false, "Arcwind Point" },
        { 6, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Rift, { 184396.0f, -113417.0f, 18674.0f }, false, "Forelhost" },
        { 7, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 111395.0f, 77770.0f, 2380.0f }, false, "the Shrine of Azura" },
        { 8, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 116435.0f, 111418.0f, -7744.0f }, false, "the College of Winterhold" },
        { 9, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 95163.0f, 67275.0f, 259.0f }, false, "Mount Anthor" },
        { 10, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Rift, { 149285.0f, -115997.0f, 12543.0f }, false, "Darklight Tower" },
        { 11, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -91689.0f, -35972.0f, 1956.0f }, false, "Bard's Leap Summit" },
        { 12, Kind::Parachute, Weapons::Parachute, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -139695.0f, -12857.0f, -1337.0f }, false, "Reachwind Eyrie" },

        // Armour, three or four spots in each hold.
        { 20, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Whiterun, { 22243.0f, -44616.0f, -136.0f }, false, "Riverwood" },
        { 21, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Whiterun, { 31343.0f, 12926.0f, -4995.0f }, false, "Whitewatch Tower" },
        { 22, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Whiterun, { -83782.0f, 7902.0f, -4118.0f }, false, "Rorikstead" },
        { 23, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Falkreath, { -30566.0f, -86850.0f, -3124.0f }, false, "Falkreath" },
        { 24, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Falkreath, { -35219.0f, -72845.0f, 1475.0f }, false, "Falkreath Watchtower" },
        { 25, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Rift, { 77183.0f, -65456.0f, 10869.0f }, false, "Ivarstead" },
        { 26, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Rift, { 167976.0f, -72409.0f, 9118.0f }, false, "Fort Greenwall" },
        { 27, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Eastmarch, { 121354.0f, -34306.0f, -9809.0f }, false, "Darkwater Crossing" },
        { 28, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Eastmarch, { 149758.0f, 35761.0f, -13237.0f }, false, "Hollyfrost Farm" },
        { 29, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Eastmarch, { 100336.0f, -22861.0f, -9313.0f }, false, "Fort Amol" },
        { 30, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Winterhold, { 109299.0f, 102777.0f, -8867.0f }, false, "Winterhold" },
        { 31, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Winterhold, { 112486.0f, 53761.0f, -3032.0f }, false, "Fort Kastav" },
        { 32, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Pale, { 33278.0f, 105558.0f, -13633.0f }, false, "Dawnstar" },
        { 33, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Pale, { 28971.0f, 61699.0f, -8536.0f }, false, "Fort Dunstad" },
        { 34, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Hjaalmarch, { -38384.0f, 63751.0f, -13910.0f }, false, "Morthal" },
        { 35, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Hjaalmarch, { -57034.0f, 69662.0f, -11155.0f }, false, "Fort Snowhawk" },
        { 36, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Haafingar, { -66223.0f, 98839.0f, -13882.0f }, false, "the Solitude docks" },
        { 37, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Haafingar, { -101407.0f, 84423.0f, -12003.0f }, false, "Dragon Bridge" },
        { 38, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Reach, { -171151.0f, 6555.0f, -3920.0f }, false, "the Markarth stables" },
        { 39, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Reach, { -132797.0f, 38451.0f, -6200.0f }, false, "Karthwasten" },
        { 40, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Falkreath, { 19737.0f, -79337.0f, 8449.0f }, false, "Helgen" },
        { 41, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Falkreath, { 2807.0f, -59216.0f, 1436.0f }, false, "the Guardian Stones" },
        { 42, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Whiterun, { 19072.0f, -18080.0f, -4360.0f }, false, "Pelagia Farm" },
        { 43, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Haafingar, { -74344.0f, 96470.0f, -11512.0f }, false, "Katla's Farm" },
        { 44, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Haafingar, { -112292.0f, 114083.0f, -5402.0f }, false, "Fort Hraggstad" },
        { 45, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Reach, { -114906.0f, -7748.0f, -5452.0f }, false, "Old Hroldan" },
        { 46, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Reach, { -165398.0f, 861.0f, -4501.0f }, false, "Left Hand Mine" },
        { 47, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Hjaalmarch, { -6557.0f, 64347.0f, -11432.0f }, false, "Stonehills" },
        { 48, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Pale, { 73031.0f, 45290.0f, -6517.0f }, false, "Heljarchen" },
        { 49, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Winterhold, { 126661.0f, 76981.0f, -7957.0f }, false, "Whistling Mine" },
        { 100, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Eastmarch, { 134765.0f, 12937.0f, -12073.0f }, false, "Kynesgrove" },
        { 101, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Rift, { 163299.0f, -60431.0f, 7107.0f }, false, "Shor's Stone" },
        { 102, Kind::Armour, 0, 0, Respawn::Slow, World::Tamriel, Hold::Rift, { 133312.0f, -96128.0f, 10702.0f }, false, "Heartwood Mill" },

        // Hearts in villages, beside their armour.
        { 50, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 22243.0f + kBeside, -44616.0f, -136.0f }, false, "Riverwood" },
        { 51, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Rift, { 77183.0f + kBeside, -65456.0f, 10869.0f }, false, "Ivarstead" },
        { 52, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Haafingar, { -101407.0f + kBeside, 84423.0f, -12003.0f }, false, "Dragon Bridge" },
        { 53, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 109299.0f + kBeside, 102777.0f, -8867.0f }, false, "Winterhold" },
        { 54, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { -83782.0f + kBeside, 7902.0f, -4118.0f }, false, "Rorikstead" },
        { 55, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { -30566.0f + kBeside, -86850.0f, -3124.0f }, false, "Falkreath" },
        { 56, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Hjaalmarch, { -38384.0f + kBeside, 63751.0f, -13910.0f }, false, "Morthal" },
        { 57, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Pale, { 33278.0f + kBeside, 105558.0f, -13633.0f }, false, "Dawnstar" },
        { 58, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -132797.0f + kBeside, 38451.0f, -6200.0f }, false, "Karthwasten" },
        { 59, Kind::Health, 0, 0, Respawn::Street, World::Tamriel, Hold::Eastmarch, { 121354.0f + kBeside, -34306.0f, -9809.0f }, false, "Darkwater Crossing" },

        // Weapons lying about, back like San Andreas's street ones: handguns and SMGs most, a few shotguns and rifles.
        { 60, Kind::Weapon, Weapons::Pistol, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 22243.0f - kBeside, -44616.0f, -136.0f }, false, "Riverwood" },
        { 61, Kind::Weapon, Weapons::Shotgun, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { -13759.0f, -8448.0f, -5552.0f }, false, "Fort Greymoor" },
        { 62, Kind::Weapon, Weapons::Tec9, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { -39379.0f, -18164.0f, -5479.0f }, false, "Sleeping Tree Camp" },
        { 63, Kind::Weapon, Weapons::Ak47, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -72571.0f, -24475.0f, 183.0f }, false, "Fort Sungard" },
        { 64, Kind::Weapon, Weapons::SniperRifle, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { -57305.0f, -62109.0f, 1767.0f }, false, "Bannermist Tower" },
        { 65, Kind::Weapon, Weapons::Molotov, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 16500.0f, 18919.0f, -4177.0f }, false, "Halted Stream Camp" },
        { 66, Kind::Weapon, Weapons::Pistol, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 19737.0f + kBeside, -79337.0f, 8449.0f }, false, "Helgen" },
        { 67, Kind::Weapon, Weapons::MicroUzi, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 2807.0f + kBeside, -59216.0f, 1436.0f }, false, "the Guardian Stones" },
        { 68, Kind::Weapon, Weapons::Sawnoff, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 8249.0f, -57383.0f, 1491.0f }, false, "Embershard Mine" },
        { 69, Kind::Weapon, Weapons::DesertEagle, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 19072.0f + kBeside, -18080.0f, -4360.0f }, false, "Pelagia Farm" },
        { 70, Kind::Weapon, Weapons::Mp5, 0, Respawn::Street, World::Tamriel, Hold::Whiterun, { 17390.0f, -10404.0f, -4520.0f }, false, "the Whiterun stables" },
        { 71, Kind::Weapon, Weapons::PistolSilenced, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { -41600.0f, -83776.0f, -4368.0f }, false, "the Dark Brotherhood's door" },
        { 72, Kind::Weapon, Weapons::Mp5, 0, Respawn::Street, World::Tamriel, Hold::Haafingar, { -101407.0f, 84423.0f + kBeside, -12003.0f }, false, "Dragon Bridge" },
        { 73, Kind::Weapon, Weapons::MicroUzi, 0, Respawn::Street, World::Tamriel, Hold::Haafingar, { -78436.0f, 90134.0f, -13845.0f }, false, "the Solitude sawmill" },
        { 74, Kind::Weapon, Weapons::Pistol, 0, Respawn::Street, World::Tamriel, Hold::Hjaalmarch, { -38384.0f - kBeside, 63751.0f, -13910.0f }, false, "Morthal" },
        { 75, Kind::Weapon, Weapons::Tec9, 0, Respawn::Street, World::Tamriel, Hold::Pale, { 33278.0f - kBeside, 105558.0f, -13633.0f }, false, "Dawnstar" },
        { 76, Kind::Weapon, Weapons::DesertEagle, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 109299.0f - kBeside, 102777.0f, -8867.0f }, false, "Winterhold" },
        { 77, Kind::Weapon, Weapons::Shotgun, 0, Respawn::Street, World::Tamriel, Hold::Eastmarch, { 134765.0f + kBeside, 12937.0f, -12073.0f }, false, "Kynesgrove" },
        { 78, Kind::Weapon, Weapons::Mp5, 0, Respawn::Street, World::Tamriel, Hold::Eastmarch, { 121354.0f - kBeside, -34306.0f, -9809.0f }, false, "Darkwater Crossing" },
        { 79, Kind::Weapon, Weapons::Pistol, 0, Respawn::Street, World::Tamriel, Hold::Rift, { 163299.0f + kBeside, -60431.0f, 7107.0f }, false, "Shor's Stone" },
        { 85, Kind::Weapon, Weapons::MicroUzi, 0, Respawn::Street, World::Tamriel, Hold::Rift, { 77183.0f - kBeside, -65456.0f, 10869.0f }, false, "Ivarstead" },
        { 86, Kind::Weapon, Weapons::Sawnoff, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -132797.0f - kBeside, 38451.0f, -6200.0f }, false, "Karthwasten" },
        { 87, Kind::Weapon, Weapons::Tec9, 0, Respawn::Street, World::Tamriel, Hold::Reach, { -114906.0f + kBeside, -7748.0f, -5452.0f }, false, "Old Hroldan" },
        { 88, Kind::Weapon, Weapons::Ak47, 0, Respawn::Street, World::Tamriel, Hold::Haafingar, { -112292.0f + kBeside, 114083.0f, -5402.0f }, false, "Fort Hraggstad" },
        { 89, Kind::Weapon, Weapons::CountryRifle, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { -56672.0f, -54925.0f, 1559.0f }, false, "Hunter's Rest" },
        { 90, Kind::Weapon, Weapons::M4, 0, Respawn::Street, World::Tamriel, Hold::Winterhold, { 112486.0f + kBeside, 53761.0f, -3032.0f }, false, "Fort Kastav" },
        { 91, Kind::Weapon, Weapons::Spas12, 0, Respawn::Street, World::Tamriel, Hold::Pale, { 28971.0f + kBeside, 61699.0f, -8536.0f }, false, "Fort Dunstad" },
        { 92, Kind::Weapon, Weapons::Grenade, 0, Respawn::Street, World::Tamriel, Hold::Falkreath, { 33279.0f, -91842.0f, 13182.0f }, false, "Fort Neugrad" },
        { 93, Kind::Weapon, Weapons::Pistol, 0, Respawn::Street, World::Tamriel, Hold::Pale, { 73031.0f + kBeside, 45290.0f, -6517.0f }, false, "Heljarchen" },

        // The heavy weapons, back like armour and tucked away: as in San Andreas a weapon comes back once CJ has been away.
        { 80, Kind::Weapon, Weapons::RocketLauncher, 10, Respawn::Slow, World::Tamriel, Hold::Hjaalmarch, { -18641.0f, 42159.0f, -7849.0f }, false, "Labyrinthian" },
        { 81, Kind::Weapon, Weapons::Minigun, 500, Respawn::Slow, World::Tamriel, Hold::Hjaalmarch, { -5431.0f, 48292.0f, 344.0f }, false, "Skyborn Altar" },
        { 94, Kind::Weapon, Weapons::Flamethrower, 0, Respawn::Slow, World::Tamriel, Hold::Rift, { 193225.0f, -84050.0f, 10682.0f }, false, "Giant's Grove" },
        { 95, Kind::Weapon, Weapons::RocketLauncherHs, 0, Respawn::Slow, World::Tamriel, Hold::Haafingar, { -144430.0f, 117428.0f, -13587.0f }, false, "Northwatch Keep" },

        // One-offs, taken for good: stashes of gold.
        { 82, Kind::Money, 0, 500, Respawn::Once, World::Tamriel, Hold::Reach, { -89950.0f, -36677.0f, 1868.0f }, false, "Lost Valley Redoubt" },
        { 83, Kind::Money, 0, 500, Respawn::Once, World::Tamriel, Hold::Hjaalmarch, { -23080.0f, 88177.0f, -13778.0f }, false, "Ustengrav" },
        { 84, Kind::Money, 0, 300, Respawn::Once, World::Tamriel, Hold::Eastmarch, { 100514.0f, 5878.0f, -7900.0f }, false, "Gallows Rock" },
        { 96, Kind::Money, 0, 500, Respawn::Once, World::Tamriel, Hold::Reach, { -180420.0f, -19104.0f, 6589.0f }, false, "Dead Crone Rock" },
        { 97, Kind::Money, 0, 300, Respawn::Once, World::Tamriel, Hold::Pale, { 57393.0f, 115440.0f, -11717.0f }, false, "Yngvild" },
    };
    // clang-format on

    inline const Spot* SpotById(std::uint16_t a_id)
    {
        for (const auto& spot : kSpots) {
            if (spot.id == a_id) {
                return &spot;
            }
        }
        return nullptr;
    }

    // Where a spot's ray down to its ground starts and ends: from 3 m over it (a roof's from 60 m) to 30 m under.
    inline constexpr float kRayAbove = 3.0f * kMeter;
    inline constexpr float kRayAboveTop = 60.0f * kMeter;
    inline constexpr float kRayBelow = 30.0f * kMeter;

    inline float RayFrom(const Spot& a_spot) { return a_spot.at.z + (a_spot.top ? kRayAboveTop : kRayAbove); }
    inline float RayTo(const Spot& a_spot) { return a_spot.at.z - kRayBelow; }

    // ---- What has been taken ----

    // The spots taken and when they come back, by id. One-offs stay taken; the co-save holds them.
    class Board
    {
    public:
        bool Shows(std::uint16_t a_id) const { return _taken.find(a_id) == _taken.end(); }

        void Take(const Spot& a_spot, double a_now) { _taken[a_spot.id] = a_now + RespawnSeconds(a_spot.respawn); }

        // Once a frame for a taken spot a_distance from CJ: back once its time is up and he is far enough off.
        // Whether it is back.
        bool Tick(const Spot& a_spot, double a_now, float a_distance)
        {
            const auto it = _taken.find(a_spot.id);
            if (it == _taken.end()) {
                return true;
            }
            if (a_now < it->second || !(a_distance > kBackAway)) {
                return false;
            }
            _taken.erase(it);
            return true;
        }

        // The one-offs taken, for the co-save, by id.
        std::vector<std::uint16_t> Once() const
        {
            std::vector<std::uint16_t> out;
            for (const auto& [id, back] : _taken) {
                const auto* spot = SpotById(id);
                if (spot && spot->respawn == Respawn::Once) {
                    out.push_back(id);
                }
            }
            return out;
        }

        // A loaded save: the one-offs it took gone, everything else back. Ids no spot has any more are dropped.
        void Restore(const std::vector<std::uint16_t>& a_once)
        {
            _taken.clear();
            for (auto id : a_once) {
                const auto* spot = SpotById(id);
                if (spot && spot->respawn == Respawn::Once) {
                    _taken[id] = INFINITY;
                }
            }
        }

        void Clear() { _taken.clear(); }

    private:
        std::map<std::uint16_t, double> _taken;  // id to when it comes back
    };

    // ---- The heart ----

    // When CJ is down to a quarter of his health, a heart turns up in front of him, at most once every 3 minutes.
    // It stays until he takes it, for 2 minutes, or until he is 60 m away.
    inline constexpr float kHeartBelow = 0.25f;
    inline constexpr float kHeartEvery = 180.0f;
    inline constexpr float kHeartLife = 120.0f;
    inline constexpr float kHeartGone = 60.0f * kMeter;

    class HeartRule
    {
    public:
        // Whether a heart should turn up now: CJ alive but under a quarter of a_max, none out, and the last one
        // long enough ago.
        bool Due(float a_health, float a_max, double a_now, bool a_heartOut) const
        {
            if (a_heartOut || !(a_max > 0.0f) || !(a_health > 0.0f) || !(a_health < kHeartBelow * a_max)) {
                return false;
            }
            return !_spawned || a_now - _last >= kHeartEvery;
        }

        void Spawned(double a_now)
        {
            _spawned = true;
            _last = a_now;
        }

        void Reset() { _spawned = false; }

    private:
        bool   _spawned = false;
        double _last = 0.0;
    };

    // Where to try putting the heart, best first: in front of CJ (a_heading: the way he looks, Skyrim's, forward
    // being (sin, cos)), 6 m off straight ahead, then a little to the sides, nearer and further. The plugin takes
    // the first with ground under it that the camera sees.
    inline std::vector<Vec3> HeartPlaces(const Vec3& a_feet, float a_heading)
    {
        constexpr std::array<std::array<float, 2>, 9> kTries{ { { 6.0f, 0.0f }, { 6.0f, 0.35f }, { 6.0f, -0.35f }, { 4.0f, 0.0f }, { 8.0f, 0.0f },
            { 5.0f, 0.7f }, { 5.0f, -0.7f }, { 3.0f, 0.0f }, { 3.0f, 1.2f } } };
        std::vector<Vec3> out;
        for (const auto& [metres, turn] : kTries) {
            const float h = a_heading + turn;
            out.push_back({ a_feet.x + std::sin(h) * metres * kMeter, a_feet.y + std::cos(h) * metres * kMeter, a_feet.z });
        }
        return out;
    }

    // ---- The pistol when he runs dry ----
    //
    // The weapon spots are kilometres apart, so when CJ has rounds for none of his guns (or no gun at all), a pistol with a
    // clip turns up in front of him, as the heart does: 2 s after he ran dry, one at a time, only while he is CJ (San
    // Andreas has the player and he is his own), and gone after 2 minutes or 60 m. Unlike the heart it has no cooldown:
    // its rounds end the dry spell, and the next comes only when he is empty again. So he can always fight on and has a
    // crosshair to aim at keys, locks and levers.

    inline constexpr float         kDryDelay = 2.0f;
    inline constexpr float         kPistolLife = kHeartLife;
    inline constexpr float         kPistolGone = kHeartGone;
    inline constexpr std::uint32_t kPistolRounds = 17;  // a clip: weapon.dat's for the 9mm

    // A gun: San Andreas's handgun, shotgun, SMG, rifle, sniper and heavy slots (2 to 7), the ones with a crosshair.
    constexpr bool IsGun(std::uint8_t a_type)
    {
        const auto slot = Shop::SlotOf(a_type);
        return Weapons::Carried(a_type) && slot >= 2 && slot <= 7;
    }

    // Whether none of CJ's guns has a round left (no gun counts as none).
    inline bool OutOfRounds(const Weapons::Loadout& a_loadout)
    {
        for (const auto& entry : a_loadout) {
            if (IsGun(entry.type) && entry.ammo > 0) {
                return false;
            }
        }
        return true;
    }

    class DryRule
    {
    public:
        // Every frame: whether a pistol should turn up now. a_cj: San Andreas has the player and CJ is his own (not in a
        // vehicle, a scene or Wasted); a_dry: OutOfRounds; a_out: one is out already.
        bool Due(bool a_cj, bool a_dry, bool a_out, double a_now)
        {
            if (!a_cj || !a_dry || a_out) {
                _dryFor = false;
                return false;
            }
            if (!_dryFor) {
                _dryFor = true;
                _since = a_now;
            }
            return a_now - _since >= kDryDelay;
        }

        // One came: the next waits for its own 2 s (it was taken without rounds in it, or it went).
        void Spawned() { _dryFor = false; }
        void Reset() { _dryFor = false; }

    private:
        bool   _dryFor = false;  // dry since _since
        double _since = 0.0;
    };

    // ---- San Andreas's side ----

    // The name of the model San Andreas draws a kind with, "" for a weapon (its own model, Weapons::ModelName).
    constexpr std::string_view ModelName(Kind a_kind)
    {
        switch (a_kind) {
        case Kind::Health: return "health";
        case Kind::Armour: return "bodyarmour";
        case Kind::Parachute: return "gun_para";
        case Kind::Money: return "money";
        case Kind::Jetpack: return "jetpack";
        default: return "";
        }
    }

    // A pickup San Andreas made itself, by its model's name: what kind it is for Skyrim (None for one it can't be).
    constexpr Kind KindOfModel(std::string_view a_name)
    {
        for (auto kind : { Kind::Health, Kind::Armour, Kind::Parachute, Kind::Money, Kind::Jetpack }) {
            if (a_name == ModelName(kind)) {
                return kind;
            }
        }
        return Kind::None;
    }

    // A model asked for: a kind, and the weapon for a weapon (0 for the rest), as one key.
    constexpr std::uint16_t ModelKey(Kind a_kind, std::uint8_t a_weapon)
    {
        return static_cast<std::uint16_t>(static_cast<std::uint16_t>(a_kind) << 8 | (a_kind == Kind::Weapon ? a_weapon : 0));
    }
}
