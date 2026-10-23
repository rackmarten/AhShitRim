// How Skyrim's people react to CJ's guns and chainsaw the way San Andreas's peds do, the game-free half:
// who reacts at all (civilians, guards, everyone else keeps doing what they do), how far each weapon is
// heard, the aim cone through San Andreas's crosshair, and how long a reaction lasts before it is started
// again or let go. Header-only and free of game types, so the protocol tests cover it.
//
// San Andreas's own rules (gta-reversed): a gunshot is heard 45 m out (CEventGunShot::AffectsPed); a
// silenced one only by a ped that faces the shooter and sees him; the player's gunshot is a crime
// (IsCriminalEvent), so cops come. A ped CJ aims at puts its hands up and cowers (CEventGunAimedAt).
// Skyrim's people have never seen a gun, though, so to them it is magic: the guards and the people CJ aims
// at say Skyrim's own lines for a player casting spells ("Whoa, whoa, whoa! Watch the magic!"), and casting
// spells in town is no crime in Skyrim, so neither is shooting.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "damage.h"
#include "weapons.h"

namespace AhShitRim::Reactions
{
    using Vec3 = Damage::Vec3;

    inline constexpr float kUnitsPerMeter = 70.0f;

    // ---- Who reacts ----

    // Skyrim's Confidence actor value: 0 cowardly, 1 cautious, 2 average, 3 brave, 4 foolhardy.
    inline constexpr int kBrave = 3;
    inline constexpr int kFoolhardy = 4;

    struct Person
    {
        bool npc = true;           // a person (ActorTypeNPC), not a creature
        bool dead = false;
        bool follower = false;     // the player's teammate, or commanded (a summon, a thrall)
        bool guard = false;        // Skyrim's IsGuard: in a hold's guard faction
        bool hostile = false;      // hostile to the player
        bool fighting = false;     // in combat already, with anyone
        int  confidence = 2;
    };

    enum class Role : std::uint8_t
    {
        None,      // keeps doing what it does: creatures, the dead, followers, anyone fighting or hostile
        Civilian,  // cowers when aimed at, flees from gunfire and a running chainsaw
        Guard,     // hears the gunfire and tells CJ off for the magic
    };

    // Followers keep fighting, bandits and anyone else already fighting carry on as Skyrim has them, and a
    // creature is left to its own AI. Of the rest, guards are guards and everyone else a civilian.
    constexpr Role RoleOf(const Person& a_person)
    {
        if (!a_person.npc || a_person.dead || a_person.follower || a_person.hostile || a_person.fighting) {
            return Role::None;
        }
        return a_person.guard ? Role::Guard : Role::Civilian;
    }

    enum class Cause : std::uint8_t
    {
        Aimed,     // CJ aims a gun at them
        Gunfire,   // a shot or a blast they heard (or, silenced, saw)
        Chainsaw,  // a running chainsaw close by
    };

    // Whether a civilian of a_confidence reacts to a_cause: a foolhardy one stands its ground through
    // anything, a brave one doesn't cower at a gun pointed at it but still runs from shooting and the saw.
    constexpr bool Reacts(int a_confidence, Cause a_cause)
    {
        if (a_confidence >= kFoolhardy) {
            return false;
        }
        return a_cause != Cause::Aimed || a_confidence < kBrave;
    }

    // ---- Noise ----

    // How loud Skyrim's detection event is (SOUND_LEVEL's values, AIFormulas::GetSoundLevelValue turns it into
    // the game's own number).
    enum class Sound : std::uint8_t
    {
        Loud = 0,
        Normal = 1,
        Silent = 2,
        VeryLoud = 3,
    };

    struct Noise
    {
        float heard = 0.0f;   // Skyrim units: civilians this close run and guards come
        float seen = 0.0f;    // further out, those who face CJ and see him still do (the silenced pistol)
        Sound sound = Sound::Silent;
        constexpr bool noisy() const { return heard > 0.0f || seen > 0.0f; }
    };

    inline constexpr float kGunshotHeard = 45.0f * kUnitsPerMeter;  // San Andreas's range, for the pistols and SMGs
    inline constexpr float kBlastHeard = 90.0f * kUnitsPerMeter;    // a rocket, grenade or satchel going off, a car blowing up

    // One shot of a_type. San Andreas hears every gun 45 m out; here the heavier ones carry further, the
    // silenced pistol is only heard a few metres away and seen as far as any other gun is heard, and what
    // makes no bang (melee, a throw, the camera, the spray can) is silent. A thrown grenade's noise is its
    // blast.
    constexpr Noise NoiseOf(std::uint8_t a_type)
    {
        using namespace Weapons;
        switch (a_type) {
        case PistolSilenced: return { 4.0f * kUnitsPerMeter, kGunshotHeard, Sound::Normal };
        case Pistol:
        case MicroUzi:
        case Mp5:
        case Tec9: return { kGunshotHeard, 0.0f, Sound::Loud };
        case DesertEagle:
        case Shotgun:
        case Sawnoff:
        case Spas12:
        case Ak47:
        case M4:
        case CountryRifle: return { 60.0f * kUnitsPerMeter, 0.0f, Sound::VeryLoud };
        case SniperRifle:
        case Minigun:
        case RocketLauncher:
        case RocketLauncherHs: return { 80.0f * kUnitsPerMeter, 0.0f, Sound::VeryLoud };
        case Flamethrower: return { 20.0f * kUnitsPerMeter, 0.0f, Sound::Loud };
        default: return {};
        }
    }

    // To Skyrim, a gun is magic: every weapon that makes a bang (the guns, the flamethrower, the launchers) is
    // a spell cast when it fires, and Skyrim is told so (its spell cast event, from the player), so whatever in
    // Skyrim watches the player's magic sees it (a brawl ends on magic). Melee, throws and the rest aren't.
    constexpr bool IsMagic(std::uint8_t a_type) { return NoiseOf(a_type).noisy(); }
    inline constexpr double kCastEvery = 0.5;  // a burst is one spell cast every so often, not one per bullet

    // The chainsaw, held in CJ's hands on foot, runs: people this close run from it, further while it cuts.
    inline constexpr float kChainsawRunning = 6.0f * kUnitsPerMeter;
    inline constexpr float kChainsawCutting = 15.0f * kUnitsPerMeter;
    inline constexpr double kCutLasts = 0.5;  // a cut counts this long after its last blow

    // Whether someone a_distance from the noise and, for the seen range, facing CJ with a line of sight to
    // him (a_sees) takes it in.
    constexpr bool Hears(const Noise& a_noise, float a_distance, bool a_sees)
    {
        return a_distance <= a_noise.heard || (a_sees && a_distance <= a_noise.seen);
    }

    // ---- Aiming ----

    inline constexpr float kAimRange = 45.0f * kUnitsPerMeter;     // aimed at from this far, as heard
    inline constexpr float kScopeRange = 100.0f * kUnitsPerMeter;  // through a scope
    inline constexpr float kAimSlack = 25.0f;                      // units around the chest a near miss still counts
    inline constexpr float kAimConeDegrees = 3.0f;                 // and the cone around the aim ray, for the far ones

    // How far a gun aimed at someone makes them cower, 0 for what isn't a gun (melee, throws, the camera).
    constexpr float AimRange(std::uint8_t a_type)
    {
        using namespace Weapons;
        switch (a_type) {
        case SniperRifle:
        case CountryRifle:
        case RocketLauncher:
        case RocketLauncherHs: return kScopeRange;
        case Pistol:
        case PistolSilenced:
        case DesertEagle:
        case Shotgun:
        case Sawnoff:
        case Spas12:
        case MicroUzi:
        case Mp5:
        case Ak47:
        case M4:
        case Tec9:
        case Flamethrower:
        case Minigun: return kAimRange;
        default: return 0.0f;
        }
    }

    inline Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 Normalized(const Vec3& a)
    {
        const float length = std::sqrt(Dot(a, a));
        return length > 1e-6f ? Vec3{ a.x / length, a.y / length, a.z / length } : Vec3{};
    }

    // San Andreas's aim ray (CCamera::Find3rdPersonCamTargetVector): from its camera looking along a_forward
    // (a_up its up, zero for the world's), through its crosshair at a_fractionX, a_fractionY of a 4:3 picture
    // a_fovDegrees across. The scopes' crosshair is the middle (0.5, 0.5).
    inline Vec3 AimRay(const Vec3& a_forward, const Vec3& a_up, float a_fovDegrees, float a_fractionX, float a_fractionY)
    {
        const Vec3 forward = Normalized(a_forward);
        Vec3       up = Dot(a_up, a_up) > 1e-6f ? a_up : Vec3{ 0.0f, 0.0f, 1.0f };
        Vec3       right = Normalized(Cross(forward, up));
        if (Dot(right, right) < 0.5f) {  // looking straight up or down
            right = { 1.0f, 0.0f, 0.0f };
        }
        up = Cross(right, forward);
        const float half = std::tan(std::clamp(a_fovDegrees, 1.0f, 170.0f) * 3.14159265f / 360.0f);
        const float across = half * (a_fractionX - 0.5f) * 2.0f;
        const float above = half * (0.5f - a_fractionY) * 2.0f / (4.0f / 3.0f);
        return Normalized({ forward.x + right.x * across + up.x * above, forward.y + right.y * across + up.y * above,
            forward.z + right.z * across + up.z * above });
    }

    // How far along a_ray (a unit vector from a_origin) someone whose chest is at a_chest is aimed at, or a
    // negative number when the ray misses them: it passes within kAimSlack of the chest plus the cone, ahead of
    // a_nearest (CJ himself, so no one behind him) and no further than a_range.
    inline float AimedAlong(const Vec3& a_origin, const Vec3& a_ray, const Vec3& a_chest, float a_nearest, float a_range, float a_bodyRadius)
    {
        const Vec3  to{ a_chest.x - a_origin.x, a_chest.y - a_origin.y, a_chest.z - a_origin.z };
        const float along = Dot(to, a_ray);
        if (along < a_nearest || along > a_range) {
            return -1.0f;
        }
        const float dx = to.x - a_ray.x * along, dy = to.y - a_ray.y * along, dz = to.z - a_ray.z * along;
        const float slack = a_bodyRadius + kAimSlack + along * std::tan(kAimConeDegrees * 3.14159265f / 180.0f);
        return dx * dx + dy * dy + dz * dz <= slack * slack ? along : -1.0f;
    }

    // ---- How long it lasts ----

    inline constexpr float kFleeDistance = 60.0f * kUnitsPerMeter;  // how far a flee runs from the player

    inline constexpr double kCowerHold = 3.0;      // a cower goes on this long after CJ stops aiming
    inline constexpr double kFleeLasts = 20.0;     // a flee is let go this long after the last scare
    inline constexpr double kFleeRenew = 5.0;      // and started over at most this often while scares go on
    inline constexpr double kAlertEvery = 1.0;     // the detection event, at most this often
    inline constexpr double kCommentGap = 4.0;     // one line about the magic at a time, this far apart at least
    inline constexpr double kCommentRepeat = 30.0; // and the same person says one at most this often
    inline constexpr float  kCommentRange = 25.0f * kUnitsPerMeter;  // said by someone this close to CJ

    // Skyrim.esm's lines about the player's magic (INFO form ids). The guards' (DialogueGuardsGeneral's
    // greetings) are for every guard voice: one when the player casts, one when it walks up with a spell
    // readied. The civilians' (the WICommentMagic greetings) are one per voice type, picked by the line's own
    // conditions.
    inline constexpr std::uint32_t kGuardWatchTheMagic = 0x000D27E1;  // "Whoa, whoa, whoa! Watch the magic!"
    inline constexpr std::uint32_t kGuardFancyMagic = 0x000D27E2;     // "Go cast your fancy magic someplace else."
    inline constexpr std::uint32_t kCommentMagicColorful = 0x000A9B1B;   // WICommentMagicColorfulHello (a topic): "Is that magic? Careful, please..."
    inline constexpr std::uint32_t kCommentMagicDangerous = 0x000AB3AF;  // WICommentMagicDangerousHello (a topic): "Would you mind dousing that magic?"

    enum class Doing : std::uint8_t
    {
        Nothing,
        Cowering,
        Fleeing,
    };

    // What each scared person is doing and until when, by their form id. Start* say whether Skyrim has to
    // start (or restart) the reaction now; Expired hands back the ones to let go.
    class Scares
    {
    public:
        struct Entry
        {
            Doing  doing = Doing::Nothing;
            double until = 0.0;    // let go after this
            double started = 0.0;  // last time Skyrim started it
        };

        // Aimed at: cower, unless it already cowers (then it lasts longer) or flees (a flee isn't undone by aiming).
        bool StartCower(std::uint32_t a_id, double a_now)
        {
            auto& e = entries[a_id];
            if (e.doing == Doing::Fleeing) {
                return false;
            }
            const bool start = e.doing != Doing::Cowering;
            e.doing = Doing::Cowering;
            e.until = a_now + kCowerHold;
            if (start) {
                e.started = a_now;
            }
            return start;
        }

        // Shots, a blast or the saw: flee, over a cower; a flee already going is started over now and then so it
        // runs from where CJ is now.
        bool StartFlee(std::uint32_t a_id, double a_now)
        {
            auto&      e = entries[a_id];
            const bool start = e.doing != Doing::Fleeing || a_now - e.started >= kFleeRenew;
            e.doing = Doing::Fleeing;
            e.until = a_now + kFleeLasts;
            if (start) {
                e.started = a_now;
            }
            return start;
        }

        Doing DoingOf(std::uint32_t a_id) const
        {
            const auto found = entries.find(a_id);
            return found != entries.end() ? found->second.doing : Doing::Nothing;
        }

        // The ones whose time ran out, with what they were doing, forgotten.
        std::vector<std::pair<std::uint32_t, Doing>> Expired(double a_now)
        {
            std::vector<std::pair<std::uint32_t, Doing>> out;
            for (auto it = entries.begin(); it != entries.end();) {
                if (a_now >= it->second.until) {
                    out.emplace_back(it->first, it->second.doing);
                    it = entries.erase(it);
                } else {
                    ++it;
                }
            }
            return out;
        }

        // Everyone, forgotten (the handover ends).
        std::vector<std::pair<std::uint32_t, Doing>> All()
        {
            std::vector<std::pair<std::uint32_t, Doing>> out;
            for (const auto& [id, e] : entries) {
                out.emplace_back(id, e.doing);
            }
            entries.clear();
            return out;
        }

        std::size_t Size() const { return entries.size(); }

    private:
        std::unordered_map<std::uint32_t, Entry> entries;
    };

    // Who may say a line about the magic now: nobody within kCommentGap of the last line, nobody twice within
    // kCommentRepeat. Say records it.
    class Comments
    {
    public:
        bool Due(std::uint32_t a_speaker, double a_now) const
        {
            if (a_now - lastAt < kCommentGap) {
                return false;
            }
            const auto found = said.find(a_speaker);
            return found == said.end() || a_now - found->second >= kCommentRepeat;
        }

        void Said(std::uint32_t a_speaker, double a_now)
        {
            lastAt = a_now;
            said[a_speaker] = a_now;
        }

    private:
        double                                    lastAt = -1e9;
        std::unordered_map<std::uint32_t, double> said;
    };

    // Shots fired between two reads of the pose's wrapping 16-bit counter.
    constexpr std::uint16_t ShotsSince(std::uint16_t a_before, std::uint16_t a_now) { return static_cast<std::uint16_t>(a_now - a_before); }
}
