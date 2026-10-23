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
#include <iterator>
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

    // ---- Lines about the magic ----

    // Skyrim.esm's own lines for a player casting spells, shouting or walking about with a weapon drawn, said in
    // the speaker's own voice (INFO form ids). Each line carries the voice types it has a voice file for in
    // Skyrim - Voices_en0.bsa, so nobody says a line silently: a voice type outside kVoiceTypes says nothing.
    // Found with a scan of Skyrim.esm's DIAL/INFO records and the voice archive's file names (ids only here).
    struct Line
    {
        std::uint32_t info = 0;
        std::uint64_t voices = 0;  // Voice:: bits
    };

    // Skyrim.esm's voice types (VTYP) these lines are recorded for, one bit each.
    inline constexpr std::uint32_t kVoiceTypes[] = {
        0x00013AD1,  //  0 MaleYoungEager
        0x00013AD2,  //  1 MaleEvenToned
        0x00013AD3,  //  2 MaleCommoner
        0x00013AD4,  //  3 MaleDrunk
        0x00013AD5,  //  4 MaleSlyCynical
        0x00013AD6,  //  5 MaleOldKindly
        0x00013AD7,  //  6 MaleOldGrumpy
        0x00013AD8,  //  7 MaleCommander
        0x00013AD9,  //  8 MaleCondescending
        0x00013ADA,  //  9 MaleBrute
        0x00013ADB,  // 10 MaleCoward
        0x00013ADC,  // 11 FemaleYoungEager
        0x00013ADD,  // 12 FemaleEvenToned
        0x00013ADE,  // 13 FemaleCommoner
        0x00013AE0,  // 14 FemaleSultry
        0x00013AE1,  // 15 FemaleOldKindly
        0x00013AE2,  // 16 FemaleOldGrumpy
        0x00013AE3,  // 17 FemaleCommander
        0x00013AE4,  // 18 FemaleCondescending
        0x00013AE5,  // 19 FemaleCoward
        0x00013AE6,  // 20 MaleNord
        0x00013AE7,  // 21 FemaleNord
        0x00013AE8,  // 22 MaleChild
        0x00013AE9,  // 23 FemaleChild
        0x00013AEA,  // 24 MaleOrc
        0x00013AEB,  // 25 FemaleOrc
        0x00013AEC,  // 26 MaleKhajiit
        0x00013AED,  // 27 FemaleKhajiit
        0x00013AEE,  // 28 MaleArgonian
        0x00013AEF,  // 29 FemaleArgonian
        0x00013AF0,  // 30 MaleElfHaughty
        0x00013AF1,  // 31 FemaleElfHaughty
        0x00013AF2,  // 32 MaleDarkElf
        0x00013AF3,  // 33 FemaleDarkElf
        0x00013BC3,  // 34 FemaleShrill
        0x0001B55F,  // 35 MaleSoldier
        0x0009843B,  // 36 MaleBandit
        0x0009844F,  // 37 MaleForsworn
        0x000AA8D3,  // 38 MaleGuard
        0x000E5003,  // 39 MaleNordCommander
        0x000EA266,  // 40 MaleCommonerAccented
        0x000EA267,  // 41 MaleEvenTonedAccented
    };
    namespace Voice
    {
        inline constexpr std::uint64_t MaleYoungEager = 1ull << 0;
        inline constexpr std::uint64_t MaleEvenToned = 1ull << 1;
        inline constexpr std::uint64_t MaleCommoner = 1ull << 2;
        inline constexpr std::uint64_t MaleDrunk = 1ull << 3;
        inline constexpr std::uint64_t MaleSlyCynical = 1ull << 4;
        inline constexpr std::uint64_t MaleOldKindly = 1ull << 5;
        inline constexpr std::uint64_t MaleOldGrumpy = 1ull << 6;
        inline constexpr std::uint64_t MaleCommander = 1ull << 7;
        inline constexpr std::uint64_t MaleCondescending = 1ull << 8;
        inline constexpr std::uint64_t MaleBrute = 1ull << 9;
        inline constexpr std::uint64_t MaleCoward = 1ull << 10;
        inline constexpr std::uint64_t FemaleYoungEager = 1ull << 11;
        inline constexpr std::uint64_t FemaleEvenToned = 1ull << 12;
        inline constexpr std::uint64_t FemaleCommoner = 1ull << 13;
        inline constexpr std::uint64_t FemaleSultry = 1ull << 14;
        inline constexpr std::uint64_t FemaleOldKindly = 1ull << 15;
        inline constexpr std::uint64_t FemaleOldGrumpy = 1ull << 16;
        inline constexpr std::uint64_t FemaleCommander = 1ull << 17;
        inline constexpr std::uint64_t FemaleCondescending = 1ull << 18;
        inline constexpr std::uint64_t FemaleCoward = 1ull << 19;
        inline constexpr std::uint64_t MaleNord = 1ull << 20;
        inline constexpr std::uint64_t FemaleNord = 1ull << 21;
        inline constexpr std::uint64_t MaleChild = 1ull << 22;
        inline constexpr std::uint64_t FemaleChild = 1ull << 23;
        inline constexpr std::uint64_t MaleOrc = 1ull << 24;
        inline constexpr std::uint64_t FemaleOrc = 1ull << 25;
        inline constexpr std::uint64_t MaleKhajiit = 1ull << 26;
        inline constexpr std::uint64_t FemaleKhajiit = 1ull << 27;
        inline constexpr std::uint64_t MaleArgonian = 1ull << 28;
        inline constexpr std::uint64_t FemaleArgonian = 1ull << 29;
        inline constexpr std::uint64_t MaleElfHaughty = 1ull << 30;
        inline constexpr std::uint64_t FemaleElfHaughty = 1ull << 31;
        inline constexpr std::uint64_t MaleDarkElf = 1ull << 32;
        inline constexpr std::uint64_t FemaleDarkElf = 1ull << 33;
        inline constexpr std::uint64_t FemaleShrill = 1ull << 34;
        inline constexpr std::uint64_t MaleSoldier = 1ull << 35;
        inline constexpr std::uint64_t MaleBandit = 1ull << 36;
        inline constexpr std::uint64_t MaleForsworn = 1ull << 37;
        inline constexpr std::uint64_t MaleGuard = 1ull << 38;
        inline constexpr std::uint64_t MaleNordCommander = 1ull << 39;
        inline constexpr std::uint64_t MaleCommonerAccented = 1ull << 40;
        inline constexpr std::uint64_t MaleEvenTonedAccented = 1ull << 41;
        inline constexpr std::uint64_t kAny = (1ull << 42) - 1;
    }
    // A guard who hears the shots (DialogueGuardsGeneral's hellos, WICastMagic03's guard): watch the magic,
    // destruction magic is fine but no burning buildings, no law against whatever that is, an eye on you, a foul
    // and unnatural weapon.
    inline constexpr Line kGuardHears[] = {
        { 0x000D27E1, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000D5EC5, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000167CA, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000167C9, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000C7F61, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
    };

    // A guard CJ aims at: fancy magic someplace else, a few spells don't impress, nervous at a drawn weapon (the
    // man's line: CJ is one), the foul weapon, an eye on you.
    inline constexpr Line kGuardAimedAt[] = {
        { 0x000D27E2, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000D5EB6, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000D27E3, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000C7F61, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x000167C9, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
    };

    // The minigun, the rockets and every blast are a shout: WICastMagic03's guard asking to stop the shouting,
    // and the reactions to the player's Thu'um (topic 000330A6) but the three that call CJ Dragonborn or a
    // Greybeard. For guards and civilians alike.
    inline constexpr Line kLoud[] = {
        { 0x000167BC, Voice::FemaleNord | Voice::MaleGuard | Voice::MaleNordCommander },
        { 0x00013A6F, Voice::FemaleNord | Voice::MaleNord },
        { 0x00013A70, Voice::FemaleEvenToned | Voice::MaleEvenToned | Voice::MaleEvenTonedAccented },
        { 0x00013A74, Voice::FemaleYoungEager | Voice::MaleYoungEager },
        { 0x00013A75, Voice::FemaleYoungEager | Voice::MaleYoungEager },
        { 0x0006CE4F, Voice::FemaleCommander | Voice::MaleCommander | Voice::MaleNordCommander },
        { 0x00013A8B, Voice::FemaleCommander | Voice::MaleCommander | Voice::MaleNordCommander },
        { 0x00013A8D, Voice::MaleSoldier },
        { 0x00013A8E, Voice::MaleSoldier },
        { 0x0006CE50, Voice::FemaleElfHaughty | Voice::MaleElfHaughty },
        { 0x00013A8F, Voice::FemaleElfHaughty | Voice::MaleElfHaughty },
        { 0x00013A90, Voice::FemaleDarkElf | Voice::MaleDarkElf },
        { 0x00013A91, Voice::FemaleDarkElf | Voice::MaleDarkElf },
        { 0x0006CE52, Voice::FemaleOrc | Voice::MaleOrc },
        { 0x00013A92, Voice::FemaleOrc | Voice::MaleOrc },
        { 0x00013A93, Voice::FemaleKhajiit | Voice::MaleKhajiit },
        { 0x00013A94, Voice::FemaleKhajiit | Voice::MaleKhajiit },
        { 0x00013A95, Voice::MaleDrunk },
        { 0x00013A96, Voice::MaleDrunk },
        { 0x0006CE54, Voice::FemaleSultry },
        { 0x00013A97, Voice::FemaleSultry },
        { 0x00013A98, Voice::MaleCoward },
        { 0x0006CE55, Voice::FemaleOldGrumpy | Voice::FemaleShrill | Voice::MaleOldGrumpy | Voice::MaleSlyCynical },
        { 0x0006CE56, Voice::FemaleOldKindly | Voice::MaleOldKindly },
        { 0x00013A99, Voice::FemaleOldKindly | Voice::MaleOldKindly },
        { 0x0006CE57, Voice::kAny },
        { 0x00013A9A, Voice::kAny },
    };

    // A civilian CJ aims at: the WICommentMagic greetings (colourful 000A9B1B, dangerous 000AB3AF), one or two
    // per voice, and two everyone has: careful with that, hey, watch it.
    inline constexpr Line kCivilianAimedAt[] = {
        { 0x000AB970, Voice::FemaleChild },
        { 0x000AB971, Voice::FemaleCommoner },
        { 0x000AB972, Voice::FemaleEvenToned },
        { 0x000AB978, Voice::FemaleNord },
        { 0x000AB979, Voice::FemaleYoungEager },
        { 0x000AB983, Voice::MaleChild },
        { 0x000AB986, Voice::MaleCommoner | Voice::MaleCommonerAccented },
        { 0x000AB988, Voice::MaleDrunk },
        { 0x000AB98D, Voice::MaleEvenToned | Voice::MaleEvenTonedAccented },
        { 0x000AB990, Voice::MaleNord },
        { 0x000AB991, Voice::MaleYoungEager },
        { 0x000AD209, Voice::FemaleArgonian },
        { 0x000AD20A, Voice::FemaleChild },
        { 0x000AD20B, Voice::FemaleCommander },
        { 0x000AD212, Voice::FemaleCommoner },
        { 0x000AD213, Voice::FemaleCondescending },
        { 0x000AD217, Voice::FemaleCoward },
        { 0x000AD218, Voice::FemaleDarkElf },
        { 0x000AD219, Voice::FemaleElfHaughty },
        { 0x000AD22C, Voice::FemaleEvenToned },
        { 0x000AD22D, Voice::FemaleKhajiit },
        { 0x000AD232, Voice::FemaleNord },
        { 0x000AD23B, Voice::FemaleOldGrumpy },
        { 0x000AD276, Voice::FemaleOldKindly },
        { 0x000AD27D, Voice::FemaleOrc },
        { 0x000AD27F, Voice::FemaleShrill },
        { 0x000AD296, Voice::FemaleSultry },
        { 0x000AD2EE, Voice::FemaleYoungEager },
        { 0x000AD2EF, Voice::MaleArgonian },
        { 0x000AD2F0, Voice::MaleBrute },
        { 0x000AD2F6, Voice::MaleChild },
        { 0x000AD2F7, Voice::MaleCommander },
        { 0x000AD2FF, Voice::MaleCommoner | Voice::MaleCommonerAccented },
        { 0x000AD300, Voice::MaleCondescending },
        { 0x000AD33C, Voice::MaleCoward },
        { 0x000AD34B, Voice::MaleDarkElf },
        { 0x000AD34C, Voice::MaleDrunk },
        { 0x000AD377, Voice::MaleElfHaughty },
        { 0x000AD37C, Voice::MaleEvenToned | Voice::MaleEvenTonedAccented },
        { 0x000AD37D, Voice::MaleKhajiit },
        { 0x000AD380, Voice::MaleNord },
        { 0x000AD382, Voice::MaleOldGrumpy },
        { 0x000AD383, Voice::MaleOldKindly },
        { 0x000AD387, Voice::MaleOrc },
        { 0x000AD388, Voice::MaleSoldier },
        { 0x000AD389, Voice::MaleSlyCynical },
        { 0x000AD38A, Voice::MaleYoungEager },
        { 0x000B823F, Voice::kAny },
        { 0x0006CB2D, Voice::kAny },
    };

    // A civilian who hears the shots: the shared reactions to a spell cast close by (WISharedInfos, someone
    // will get hurt, foul magic, a fool sorcerer), one or two per voice, and what was that.
    inline constexpr Line kCivilianHears[] = {
        { 0x000AB3D6, Voice::FemaleCoward },
        { 0x000AB3D7, Voice::FemaleDarkElf },
        { 0x000AB3D8, Voice::FemaleElfHaughty },
        { 0x000AB3D9, Voice::FemaleEvenToned },
        { 0x000AB3DA, Voice::FemaleKhajiit },
        { 0x000AB3DB, Voice::FemaleNord },
        { 0x000AB3DC, Voice::FemaleOldGrumpy },
        { 0x000AB3DD, Voice::FemaleOldKindly },
        { 0x000AB3DE, Voice::FemaleOrc },
        { 0x000AB3DF, Voice::FemaleShrill },
        { 0x000AB3E1, Voice::FemaleSultry },
        { 0x000AB3E2, Voice::FemaleYoungEager },
        { 0x000AB3E3, Voice::MaleArgonian },
        { 0x000AB3E4, Voice::MaleBrute },
        { 0x000AB3E5, Voice::MaleChild },
        { 0x000AB3E6, Voice::MaleCommander },
        { 0x000AB3E7, Voice::MaleCommoner | Voice::MaleCommonerAccented },
        { 0x000AB3E8, Voice::MaleCondescending },
        { 0x000AB3E9, Voice::MaleCoward },
        { 0x000AB3EA, Voice::MaleDarkElf },
        { 0x000AB3EB, Voice::MaleDrunk },
        { 0x000AB3EC, Voice::MaleElfHaughty },
        { 0x000AB3ED, Voice::MaleEvenToned | Voice::MaleEvenTonedAccented },
        { 0x000AB3EE, Voice::MaleKhajiit },
        { 0x000AB3EF, Voice::MaleNord },
        { 0x000AB3F0, Voice::MaleOldGrumpy },
        { 0x000AB3F1, Voice::MaleOldKindly },
        { 0x000AB3F2, Voice::MaleOrc },
        { 0x00013A9A, Voice::kAny },
    };

    // The flamethrower, aimed or fired, adds the shared remarks on playing with fire, one or two per voice.
    inline constexpr Line kFlames[] = {
        { 0x0006FEB1, Voice::FemaleArgonian },
        { 0x0006FEB2, Voice::FemaleChild },
        { 0x0006FEB3, Voice::FemaleCommander },
        { 0x0006FEB4, Voice::FemaleCommoner },
        { 0x0006FEB5, Voice::FemaleCondescending },
        { 0x0006FEB6, Voice::FemaleCoward },
        { 0x0006FEB7, Voice::FemaleDarkElf },
        { 0x0006FEB8, Voice::FemaleElfHaughty },
        { 0x0006FEB9, Voice::FemaleEvenToned },
        { 0x0006FEBA, Voice::FemaleKhajiit },
        { 0x0006FEBB, Voice::FemaleNord },
        { 0x0006FEBC, Voice::FemaleOldGrumpy },
        { 0x0006FEBD, Voice::FemaleOldKindly },
        { 0x0006FEBE, Voice::FemaleOrc },
        { 0x0006FEBF, Voice::FemaleShrill },
        { 0x0006FEC1, Voice::FemaleSultry },
        { 0x0006FEC2, Voice::FemaleYoungEager },
        { 0x0006FEC3, Voice::MaleArgonian },
        { 0x0006FEC4, Voice::MaleBrute },
        { 0x0006FEC5, Voice::MaleChild },
        { 0x0006FEC6, Voice::MaleCommander | Voice::MaleNordCommander },
        { 0x0006FEC7, Voice::MaleCommoner | Voice::MaleCommonerAccented },
        { 0x0006FEC8, Voice::MaleCondescending },
        { 0x0006FEC9, Voice::MaleCoward },
        { 0x0006FECA, Voice::MaleDarkElf },
        { 0x0006FECB, Voice::MaleDrunk },
        { 0x0006FECC, Voice::MaleElfHaughty },
        { 0x0006FECD, Voice::MaleEvenToned | Voice::MaleEvenTonedAccented },
        { 0x0006FECE, Voice::MaleKhajiit },
        { 0x0006FECF, Voice::MaleNord },
        { 0x0006FED0, Voice::MaleOldGrumpy },
        { 0x0006FED1, Voice::MaleOldKindly },
        { 0x0006FED2, Voice::MaleOrc },
        { 0x0006FED3, Voice::MaleSlyCynical },
        { 0x0006FED4, Voice::MaleSoldier },
        { 0x0006FED5, Voice::MaleYoungEager },
    };

    // The voice bit of a voice type (a VTYP form id), 0 for one no line here is recorded for.
    constexpr std::uint64_t VoiceBit(std::uint32_t a_voiceType)
    {
        for (std::size_t i = 0; i < std::size(kVoiceTypes); ++i) {
            if (kVoiceTypes[i] == a_voiceType) {
                return 1ull << i;
            }
        }
        return 0;
    }

    enum class Moment : std::uint8_t
    {
        GuardHears,       // the nearest guard who hears the shots
        GuardAimedAt,     // a guard CJ aims at
        Loud,             // anyone who hears the minigun, a rocket or a blast
        CivilianAimedAt,  // a civilian CJ aims at, as it cowers
        CivilianHears,    // the nearest civilian who hears the shots, with no guard about
    };

    // The weapons loud enough to be a shout: the minigun and the rocket launchers (their blasts are, too).
    constexpr bool IsLoud(std::uint8_t a_type)
    {
        using namespace Weapons;
        return a_type == Minigun || a_type == RocketLauncher || a_type == RocketLauncherHs;
    }

    // The lines that fit a_moment with a_type in CJ's hands: the flamethrower adds the fire remarks to the
    // civilians' own.
    inline std::vector<Line> LinesFor(Moment a_moment, std::uint8_t a_type)
    {
        std::vector<Line> lines;
        const auto add = [&](const auto& a_pool) { lines.insert(lines.end(), std::begin(a_pool), std::end(a_pool)); };
        switch (a_moment) {
        case Moment::GuardHears: add(kGuardHears); break;
        case Moment::GuardAimedAt: add(kGuardAimedAt); break;
        case Moment::Loud: add(kLoud); break;
        case Moment::CivilianAimedAt: add(kCivilianAimedAt); break;
        case Moment::CivilianHears: add(kCivilianHears); break;
        }
        if (a_type == Weapons::Flamethrower && (a_moment == Moment::CivilianAimedAt || a_moment == Moment::CivilianHears)) {
            add(kFlames);
        }
        return lines;
    }

    // Who may say a line about the magic now, and which: nobody within kCommentGap of the last line, nobody twice
    // within kCommentRepeat, and nobody the same line again before the others that fit them have been said. Said
    // records it.
    class Comments
    {
    public:
        bool Due(std::uint32_t a_speaker, double a_now) const
        {
            if (a_now - lastAt < kCommentGap) {
                return false;
            }
            const auto found = said.find(a_speaker);
            return found == said.end() || a_now - found->second.at >= kCommentRepeat;
        }

        // A line of a_lines in a_voice (a Voice:: bit) that a_speaker hasn't said in this round of them, a_random
        // choosing; once it has said them all, a new round starts. The line said last by anyone comes up again only
        // when nothing else fits. 0 when no line has a_speaker's voice.
        std::uint32_t Pick(std::uint32_t a_speaker, std::uint64_t a_voice, const std::vector<Line>& a_lines, std::uint32_t a_random)
        {
            std::vector<std::uint32_t> fits;
            for (const auto& line : a_lines) {
                if ((line.voices & a_voice) && std::find(fits.begin(), fits.end(), line.info) == fits.end()) {
                    fits.push_back(line.info);
                }
            }
            if (!a_voice || fits.empty()) {
                return 0;
            }
            auto&      spoken = said[a_speaker].lines;
            const auto unsaid = [&] {
                std::vector<std::uint32_t> out;
                for (const auto id : fits) {
                    if (std::find(spoken.begin(), spoken.end(), id) == spoken.end()) {
                        out.push_back(id);
                    }
                }
                return out;
            };
            auto fresh = unsaid();
            if (fresh.empty()) {  // said them all: a new round
                std::erase_if(spoken, [&](std::uint32_t a_id) { return std::find(fits.begin(), fits.end(), a_id) != fits.end(); });
                fresh = fits;
            }
            if (fresh.size() > 1) {
                std::erase(fresh, lastLine);
            }
            return fresh[a_random % fresh.size()];
        }

        void Said(std::uint32_t a_speaker, double a_now, std::uint32_t a_line = 0)
        {
            lastAt = a_now;
            auto& speaker = said[a_speaker];
            speaker.at = a_now;
            if (a_line) {
                lastLine = a_line;
                if (std::find(speaker.lines.begin(), speaker.lines.end(), a_line) == speaker.lines.end()) {
                    speaker.lines.push_back(a_line);
                }
            }
        }

    private:
        struct Speaker
        {
            double                     at = -1e9;  // when it last said a line
            std::vector<std::uint32_t> lines;      // what it said this round
        };

        double                                     lastAt = -1e9;
        std::uint32_t                              lastLine = 0;
        std::unordered_map<std::uint32_t, Speaker> said;
    };

    // Shots fired between two reads of the pose's wrapping 16-bit counter.
    constexpr std::uint16_t ShotsSince(std::uint16_t a_before, std::uint16_t a_now) { return static_cast<std::uint16_t>(a_now - a_before); }
}
