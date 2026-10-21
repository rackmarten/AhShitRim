// CJ's weapons, the game-free half (v12): San Andreas's weapon ids and what Skyrim needs to know about
// them (their model and icon names, whether they count ammo, whether they flash), the scopes its camera
// puts up, the mouse wheel's weapon switches as presses on San Andreas's pad, shots counted from the
// ammo, where the shots leave, and the loadout the save and the pose hold. Header-only and free of game
// types, so the protocol tests cover it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "pose.h"

namespace AhShitRim::Weapons
{
    // San Andreas's eWeaponType values CJ can carry (weapon.dat's names in comments where they differ).
    enum Type : std::uint8_t
    {
        Unarmed = 0,
        BrassKnuckle = 1,
        GolfClub = 2,
        Nightstick = 3,
        Knife = 4,
        BaseballBat = 5,
        Shovel = 6,
        PoolCue = 7,
        Katana = 8,
        Chainsaw = 9,
        Dildo1 = 10,
        Dildo2 = 11,
        Vibe1 = 12,
        Vibe2 = 13,
        Flowers = 14,
        Cane = 15,
        Grenade = 16,
        TearGas = 17,
        Molotov = 18,
        Pistol = 22,
        PistolSilenced = 23,
        DesertEagle = 24,
        Shotgun = 25,
        Sawnoff = 26,
        Spas12 = 27,
        MicroUzi = 28,
        Mp5 = 29,
        Ak47 = 30,
        M4 = 31,
        Tec9 = 32,
        CountryRifle = 33,
        SniperRifle = 34,
        RocketLauncher = 35,
        RocketLauncherHs = 36,  // the heat seeker
        Flamethrower = 37,
        Minigun = 38,
        SatchelCharge = 39,
        Detonator = 40,
        Spraycan = 41,
        Extinguisher = 42,
        Camera = 43,
        NightVision = 44,
        Infrared = 45,
        Parachute = 46,
    };

    inline constexpr std::uint8_t  kLastCarried = Parachute;
    inline constexpr std::uint32_t kAmmoMax = 99999;

    // Types that can sit in a weapon slot. 19 to 21 are projectiles, 47 and up San Andreas's own
    // bookkeeping (armour, ways to die).
    constexpr bool Carried(std::uint8_t a_type)
    {
        return (a_type >= BrassKnuckle && a_type <= Molotov) || (a_type >= Pistol && a_type <= Parachute);
    }

    // Fists, the melee weapons and the gifts (flowers, dildos, the cane): attacks, not shots.
    constexpr bool IsMelee(std::uint8_t a_type) { return a_type <= Cane; }

    // Guns San Andreas draws a muzzle flash on (the gunflash frame of their model).
    constexpr bool Flashes(std::uint8_t a_type)
    {
        return (a_type >= Pistol && a_type <= SniperRifle) || a_type == Minigun;
    }

    // Whether San Andreas's HUD shows a round count for the weapon (CHud::DrawAmmo leaves the rest out).
    constexpr bool CountsAmmo(std::uint8_t a_type)
    {
        return Carried(a_type) && !IsMelee(a_type) && a_type != Detonator && a_type != Parachute;
    }

    // The name of the weapon's model and texture dictionary (gta3.img has both as <name>.dff and
    // <name>.txd), "" for fists and types that have none.
    constexpr std::string_view ModelName(std::uint8_t a_type)
    {
        switch (a_type) {
        case BrassKnuckle: return "brassknuckle";
        case GolfClub: return "golfclub";
        case Nightstick: return "nitestick";
        case Knife: return "knifecur";
        case BaseballBat: return "bat";
        case Shovel: return "shovel";
        case PoolCue: return "poolcue";
        case Katana: return "katana";
        case Chainsaw: return "chnsaw";
        case Dildo1: return "gun_dildo1";
        case Dildo2: return "gun_dildo2";
        case Vibe1: return "gun_vibe1";
        case Vibe2: return "gun_vibe2";
        case Flowers: return "flowera";
        case Cane: return "gun_cane";
        case Grenade: return "grenade";
        case TearGas: return "teargas";
        case Molotov: return "molotov";
        case Pistol: return "colt45";
        case PistolSilenced: return "silenced";
        case DesertEagle: return "desert_eagle";
        case Shotgun: return "chromegun";
        case Sawnoff: return "sawnoff";
        case Spas12: return "shotgspa";
        case MicroUzi: return "micro_uzi";
        case Mp5: return "mp5lng";
        case Ak47: return "ak47";
        case M4: return "m4";
        case Tec9: return "tec9";
        case CountryRifle: return "cuntgun";
        case SniperRifle: return "sniper";
        case RocketLauncher: return "rocketla";
        case RocketLauncherHs: return "heatseek";
        case Flamethrower: return "flame";
        case Minigun: return "minigun";
        case SatchelCharge: return "satchel";
        case Detonator: return "bomb";
        case Spraycan: return "spraycan";
        case Extinguisher: return "fire_ex";
        case Camera: return "camera";
        case NightVision: return "nvgoggles";
        case Infrared: return "irgoggles";
        case Parachute: return "gun_para";
        default: return "";
        }
    }

    // The HUD icon's texture in the weapon's dictionary: the model name and "icon" (gun_para's is
    // "gun_paraIcon"; texture names compare without case). Fists and the types without a model use
    // hud.txd's "fist".
    inline std::string IconName(std::uint8_t a_type)
    {
        const auto model = ModelName(a_type);
        return model.empty() ? std::string("fist") : std::string(model) + "icon";
    }

    // The scoped crosshair's texture in the dictionary of the weapon that has one.
    constexpr std::string_view CrosshairName(std::uint8_t a_type)
    {
        switch (a_type) {
        case SniperRifle: return "SNIPERcrosshair";
        case Camera: return "cameraCrosshair";
        default: return "";
        }
    }

    // ---- San Andreas's camera, as the HUD needs it ----

    // eCamMode values (plugin-sdk's eCamMode).
    inline constexpr std::uint8_t kCamSniper = 0x07;
    inline constexpr std::uint8_t kCamRocketLauncher = 0x08;
    inline constexpr std::uint8_t kCamFirstPerson = 0x10;
    inline constexpr std::uint8_t kCamM16FirstPerson = 0x22;
    inline constexpr std::uint8_t kCamSniperRunabout = 0x27;
    inline constexpr std::uint8_t kCamRocketLauncherRunabout = 0x28;
    inline constexpr std::uint8_t kCamCamera = 0x2E;
    inline constexpr std::uint8_t kCamRocketLauncherHs = 0x33;
    inline constexpr std::uint8_t kCamRocketLauncherRunaboutHs = 0x34;
    inline constexpr std::uint8_t kCamAimWeapon = 0x35;

    enum class Scope : std::uint8_t
    {
        None,     // Skyrim's HUD as it is (San Andreas's own crosshair for a lock-on is the HUD's, see Crosshair)
        Sniper,   // the sniper rifle's scope: its crosshair texture with the rest of the screen blacked out
        Rocket,   // a rocket launcher's sight
        Rifle,    // the first-person rifle sight: four corner sprites
        Camera,   // the camera's viewfinder
        Aim,      // free aim in the third person: the crosshair that follows the gun
    };

    constexpr Scope ScopeOf(std::uint8_t a_cameraMode)
    {
        switch (a_cameraMode) {
        case kCamSniper:
        case kCamSniperRunabout: return Scope::Sniper;
        case kCamRocketLauncher:
        case kCamRocketLauncherRunabout:
        case kCamRocketLauncherHs:
        case kCamRocketLauncherRunaboutHs: return Scope::Rocket;
        case kCamM16FirstPerson: return Scope::Rifle;
        case kCamCamera: return Scope::Camera;
        case kCamAimWeapon: return Scope::Aim;
        default: return Scope::None;
        }
    }

    // The look pitch to give San Andreas's aim (CPlayerData::m_fLookPitch) for a camera looking along a unit vector
    // with this Z: the angle it looks up, negated (0 is level, looking up is negative).
    inline float AimLookPitch(float a_frontZ)
    {
        return -std::asin(std::clamp(std::isfinite(a_frontZ) ? a_frontZ : 0.0f, -1.0f, 1.0f));
    }

    // ---- The mouse wheel ----

    // The wheel's steps, as the presses San Andreas's pad wants: its weapon switch is an edge (the
    // shoulder button down this frame, up the one before), so a step is one frame down and one up, and
    // steps that came in together go out one after another.
    class WheelPulse
    {
    public:
        struct Presses
        {
            bool next = false;  // R2: the next weapon
            bool prev = false;  // L2: the previous one
        };

        static constexpr int kPendingMax = 8;

        // a_next and a_prev are InputState's wheel counters (they wrap). a_active says the steps are
        // for the weapons (CJ on foot, San Andreas's player); otherwise they are only followed, so
        // the radio's steps in a vehicle don't switch the weapon later.
        Presses Step(std::uint16_t a_next, std::uint16_t a_prev, bool a_active)
        {
            const std::uint16_t nextSteps = static_cast<std::uint16_t>(a_next - _lastNext);
            const std::uint16_t prevSteps = static_cast<std::uint16_t>(a_prev - _lastPrev);
            const bool          seeded = _seeded;
            _lastNext = a_next;
            _lastPrev = a_prev;
            _seeded = true;
            if (!seeded || !a_active) {
                _pendingNext = _pendingPrev = 0;
                _wasDown = false;
                return {};
            }
            // More steps than a hand makes in a frame is the counters starting over (a restarted Skyrim, the
            // link coming back): followed, not played.
            _pendingNext = (std::min)(_pendingNext + (nextSteps > kPendingMax ? 0 : static_cast<int>(nextSteps)), kPendingMax);
            _pendingPrev = (std::min)(_pendingPrev + (prevSteps > kPendingMax ? 0 : static_cast<int>(prevSteps)), kPendingMax);
            Presses out;
            if (_wasDown) {
                _wasDown = false;  // the release between two presses
                return out;
            }
            if (_pendingNext > 0) {
                _pendingNext--;
                out.next = _wasDown = true;
            } else if (_pendingPrev > 0) {
                _pendingPrev--;
                out.prev = _wasDown = true;
            }
            return out;
        }

    private:
        std::uint16_t _lastNext = 0;
        std::uint16_t _lastPrev = 0;
        bool          _seeded = false;
        bool          _wasDown = false;
        int           _pendingNext = 0;
        int           _pendingPrev = 0;
    };

    // ---- Shots ----

    // Shots fired between two frames of the same weapon, from its ammo: San Andreas takes a round
    // off the total for each one (also for a thrown grenade or a burst of flames).
    constexpr std::uint16_t ShotsBetween(std::uint32_t a_ammoBefore, std::uint32_t a_ammoNow)
    {
        const std::uint32_t gone = a_ammoBefore > a_ammoNow ? a_ammoBefore - a_ammoNow : 0;
        return static_cast<std::uint16_t>(gone > 32 ? 32 : gone);  // a reload or a swap isn't a volley
    }

    // Where a shot leaves: the weapon's frame (San Andreas metres, its model's origin at the hand) applied
    // to the weapon's fire offset, relative to CJ's feet, in Skyrim units.
    inline void Muzzle(const Body::RwRows& a_weapon, const float (&a_fireOffset)[3], const Pose::Vec3& a_feet, float (&a_out)[3])
    {
        const auto at = Body::Apply(a_weapon, { a_fireOffset[0], a_fireOffset[1], a_fireOffset[2] });
        a_out[0] = (at.x - a_feet.x) * Pose::kUnitsPerMeter;
        a_out[1] = (at.y - a_feet.y) * Pose::kUnitsPerMeter;
        a_out[2] = (at.z - a_feet.z) * Pose::kUnitsPerMeter;
    }

    // ---- The debug key ----

    // What the debug key gives, one step per press, round and round: San Andreas's three weapon-set cheats, the
    // gear they leave out and each melee weapon and gift (a slot holds one, so they come one at a time).
    struct DebugStep
    {
        Protocol::WeaponCommandKind kind;
        std::uint32_t               argument;  // the set, or the weapon's type
        std::uint32_t               ammo;      // for a weapon
        const char*                 what;
    };

    inline constexpr DebugStep kDebugSteps[] = {
        { Protocol::WeaponCommandKind::GiveSet, 1, 0, "weapon set 1 (thug tools)" },
        { Protocol::WeaponCommandKind::GiveSet, 2, 0, "weapon set 2 (professional tools)" },
        { Protocol::WeaponCommandKind::GiveSet, 3, 0, "weapon set 3 (nutter tools)" },
        { Protocol::WeaponCommandKind::GiveSet, 4, 0, "the rest: tear gas, satchel charges, minigun, camera, parachute" },
        { Protocol::WeaponCommandKind::GiveWeapon, GolfClub, 1, "golf club" },
        { Protocol::WeaponCommandKind::GiveWeapon, Nightstick, 1, "nightstick" },
        { Protocol::WeaponCommandKind::GiveWeapon, Shovel, 1, "shovel" },
        { Protocol::WeaponCommandKind::GiveWeapon, PoolCue, 1, "pool cue" },
        { Protocol::WeaponCommandKind::GiveWeapon, Katana, 1, "katana" },
        { Protocol::WeaponCommandKind::GiveWeapon, Dildo1, 1, "dildo" },
        { Protocol::WeaponCommandKind::GiveWeapon, Dildo2, 1, "dildo, the other" },
        { Protocol::WeaponCommandKind::GiveWeapon, Vibe1, 1, "vibrator" },
        { Protocol::WeaponCommandKind::GiveWeapon, Vibe2, 1, "vibrator, the other" },
        { Protocol::WeaponCommandKind::GiveWeapon, Flowers, 1, "flowers" },
        { Protocol::WeaponCommandKind::GiveWeapon, Cane, 1, "cane" },
        { Protocol::WeaponCommandKind::GiveWeapon, Chainsaw, 1, "chainsaw" },
    };
    inline constexpr std::size_t kDebugStepCount = sizeof(kDebugSteps) / sizeof(kDebugSteps[0]);

    // The command for the a_step-th press (counting from 0, wrapping).
    inline Protocol::WeaponCommand DebugCommand(std::size_t a_step)
    {
        const auto&             step = kDebugSteps[a_step % kDebugStepCount];
        Protocol::WeaponCommand command{};
        command.kind = static_cast<std::uint32_t>(step.kind);
        command.argument = step.argument;
        command.entries[0].ammo = step.ammo;
        return command;
    }

    // ---- The loadout ----

    using Loadout = Protocol::WeaponEntry[Protocol::kWeaponSlotCount];

    // Makes the entries ones San Andreas can hold: empty what isn't a carried weapon, ammo at most
    // kAmmoMax, the clip no more than the ammo. Returns how many entries were changed.
    inline int Sanitise(Loadout& a_loadout)
    {
        int changed = 0;
        for (auto& entry : a_loadout) {
            Protocol::WeaponEntry fixed = entry;
            fixed.reserved = 0;
            if (!Carried(fixed.type)) {
                fixed = {};
            }
            fixed.ammo = (std::min)(fixed.ammo, kAmmoMax);
            fixed.clip = static_cast<std::uint16_t>((std::min)(static_cast<std::uint32_t>(fixed.clip), fixed.ammo));
            if (fixed.type != entry.type || fixed.clip != entry.clip || fixed.ammo != entry.ammo || entry.reserved != 0) {
                changed++;
            }
            entry = fixed;
        }
        return changed;
    }

    inline bool Empty(const Loadout& a_loadout)
    {
        for (const auto& entry : a_loadout) {
            if (entry.type != Unarmed) {
                return false;
            }
        }
        return true;
    }

    // Whether CJ holds a weapon in the slot (a loaded save's current slot must be one he has, else he
    // holds his fists).
    inline bool Holds(const Loadout& a_loadout, std::uint32_t a_slot)
    {
        return a_slot < Protocol::kWeaponSlotCount && a_loadout[a_slot].type != Unarmed;
    }

    // The ammo style of a weapon with this clip (WeaponInfo::m_nAmmoClip), as CHud::DrawAmmo decides it: the
    // total for a clip of one or of a thousand and more, "rest-clip" for the rest, the flamethrower's in tenths.
    constexpr std::uint8_t AmmoStyleOf(std::uint8_t a_type, std::uint32_t a_clipSize)
    {
        if (a_clipSize <= 1 || a_clipSize >= 1000) {
            return Protocol::kAmmoTotal;
        }
        return a_type == Flamethrower ? Protocol::kAmmoFlame : Protocol::kAmmoClip;
    }

    // The ammo line San Andreas's HUD shows (CHud::DrawAmmo).
    inline std::string AmmoText(std::uint32_t a_ammo, std::uint32_t a_clip, std::uint8_t a_style)
    {
        constexpr std::uint32_t kMaxShown = 9999;
        if (a_style == Protocol::kAmmoTotal) {
            return std::to_string(a_ammo);
        }
        const bool          flame = a_style == Protocol::kAmmoFlame;
        const std::uint32_t reserve = a_ammo > a_clip ? a_ammo - a_clip : 0;
        const std::uint32_t total = (std::min)(flame ? reserve / 10 : reserve, kMaxShown);
        const std::uint32_t current = flame ? a_clip / 10 : a_clip;
        return std::to_string(total) + "-" + std::to_string(current);
    }
}
