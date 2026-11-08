// The Grove Street crew (v39), the game-free half: how many homies CJ may lead, where the followers walk, where the
// homies stand about at a hold's spots, when one opens fire, which one G picks, what CJ's own hits on them do, and which
// way a puppet faces. Header-only and free of game types, so the protocol tests cover it. Skyrim's half is
// skse/src/player/Homies.cpp (the actors, the keys, the following and the fights), San Andreas's
// sa/src/crew_puppets.cpp (the puppets).
//
// Positions here are Skyrim units (70 to the metre) and Skyrim headings (radians, 0 north, clockwise) unless a name says
// metres or San Andreas.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "pose.h"
#include "scene.h"

namespace AhShitRim::Crew
{
    using Vec3 = Pose::Vec3;

    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // ---- Who they are ----

    // San Andreas's Grove Street Families peds (PED_TYPE_GANG2): fam1, fam2 and fam3.
    inline constexpr std::array<std::uint16_t, 3> kModels = { 105, 106, 107 };
    inline constexpr std::uint16_t ModelFor(std::uint32_t a_roll) { return kModels[a_roll % kModels.size()]; }

    // ---- How many follow ----

    // San Andreas's player group holds 7 followers (CPedGroupMembership: 8 members, the leader the last).
    inline constexpr int kFollowerMax = 7;

    // San Andreas's respect table (CStats::FindMaxNumberOfGroupMembers: 0, 2, 3, 4, 5, 6, 7), Skyrim's Thane titles
    // its steps: one title leads 2, six or more lead 7.
    inline constexpr int SizeFor(int a_thanes)
    {
        constexpr int kTable[] = { 0, 2, 3, 4, 5, 6, 7 };
        return kTable[std::clamp(a_thanes, 0, 6)];
    }

    // The help box's line when G finds the crew full.
    inline constexpr std::string_view kFullLine = "Your crew is full: become Thane of another hold for more.";
    inline constexpr std::string_view kNoThaneLine = "Nobody here follows you: become a Thane of this hold first.";

    // ---- The Thane titles ----
    //
    // Vanilla Skyrim keeps no Thane flag of its own: the guards' "I'm a Thane" line checks a variable per hold on the
    // Thane quest's script (FavorJarlsMakeFriends), one for the hold under the Empire and one under the Stormcloaks.
    // A hold counts once either is set.
    struct ThaneHold
    {
        std::string_view name;  // as the variables spell it
        std::string_view imperial;
        std::string_view sons;
    };

    inline constexpr std::string_view kThaneQuest = "FavorJarlsMakeFriends";
    inline constexpr std::string_view kThaneScript = "FavorJarlsMakeFriendsScript";

    // In Wanted::kHolds' order (Whiterun, Eastmarch, the Rift, the Reach, Haafingar, Hjaalmarch, the Pale, Winterhold,
    // Falkreath), so a hold's index is the same in both.
    inline constexpr std::array<ThaneHold, 9> kThaneHolds = { {
        { "Whiterun", "WhiterunImpGetOutofJail", "WhiterunSonsGetOutofJail" },
        { "Eastmarch", "EastmarchImpGetOutofJail", "EastmarchSonsGetOutofJail" },
        { "Rift", "RiftImpGetOutofJail", "RiftSonsGetOutofJail" },
        { "Reach", "ReachImpGetOutofJail", "ReachSonsGetOutofJail" },
        { "Haafingar", "HaafingarImpGetOutofJail", "HaafingarSonsGetOutofJail" },
        { "Hjaalmarch", "HjaalmarchImpGetOutofJail", "HjaalmarchSonsGetOutofJail" },
        { "Pale", "PaleImpGetOutofJail", "PaleSonsGetOutofJail" },
        { "Winterhold", "WinterholdImpGetOutofJail", "WinterholdSonsGetOutofJail" },
        { "Falkreath", "FalkreathImpGetOutofJail", "FalkreathSonsGetOutofJail" },
    } };

    // The holds counted as the player's: bit i for kThaneHolds[i]. a_forced (the ini's [Debug] CrewThanes, -1 for none)
    // makes the first that many holds count instead, Whiterun first.
    inline std::uint16_t ThaneMask(const std::array<int, 9>& a_imperial, const std::array<int, 9>& a_sons, int a_forced)
    {
        std::uint16_t mask = 0;
        if (a_forced >= 0) {
            for (int i = 0; i < (std::min)(a_forced, 9); i++) {
                mask |= static_cast<std::uint16_t>(1u << i);
            }
            return mask;
        }
        for (int i = 0; i < 9; i++) {
            if (a_imperial[i] > 0 || a_sons[i] > 0) {
                mask |= static_cast<std::uint16_t>(1u << i);
            }
        }
        return mask;
    }

    inline int Titles(std::uint16_t a_mask)
    {
        int n = 0;
        for (int i = 0; i < 9; i++) {
            n += (a_mask >> i) & 1;
        }
        return n;
    }

    // ---- Their guns ----

    // San Andreas's eWeaponType of the guns its gangs carry.
    inline constexpr std::uint8_t kPistol = 22;
    inline constexpr std::uint8_t kMicroSmg = 28;
    inline constexpr std::uint8_t kAk47 = 30;
    inline constexpr std::uint8_t kTec9 = 32;

    // As San Andreas arms its gangs better with respect: a 9mm or a micro SMG at first, a Tec-9 from three titles, an
    // AK-47 for one in three from five.
    inline constexpr std::uint8_t WeaponFor(int a_titles, std::uint32_t a_roll)
    {
        if (a_titles >= 5 && a_roll % 3 == 0) {
            return kAk47;
        }
        if (a_titles >= 3 && a_roll % 2 == 0) {
            return kTec9;
        }
        return a_roll % 2 == 0 ? kPistol : kMicroSmg;
    }

    // How far his gun reaches, metres (weapon.dat's ranges).
    inline constexpr float RangeOf(std::uint8_t a_weapon)
    {
        switch (a_weapon) {
        case kAk47: return 70.0f;
        case kMicroSmg: return 45.0f;
        default: return 35.0f;
        }
    }

    // ---- Following ----

    // Where follower i walks, metres from CJ, x to his right and y ahead: San Andreas's formation
    // (CTaskComplexFollowLeaderInFormation), two ranks behind him and a third behind those.
    inline constexpr std::array<std::array<float, 2>, kFollowerMax> kFormation = { {
        { -1.3f, -1.6f },
        { 1.3f, -1.6f },
        { 0.0f, -2.8f },
        { -2.6f, -2.8f },
        { 2.6f, -2.8f },
        { -1.3f, -4.0f },
        { 1.3f, -4.0f },
    } };

    // Follower a_index's offset in Skyrim units, as KeepOffsetFromActor takes it: in the leader's own frame (x right,
    // y ahead).
    inline Vec3 FormationOffset(int a_index)
    {
        const auto& o = kFormation[static_cast<std::size_t>(std::clamp(a_index, 0, kFollowerMax - 1))];
        return { o[0] * kMeter, o[1] * kMeter, 0.0f };
    }

    // That offset turned into the world, around a leader at a_heading.
    inline Vec3 FormationAt(const Vec3& a_leader, float a_heading, int a_index)
    {
        const auto  o = FormationOffset(a_index);
        const float s = std::sin(a_heading), c = std::cos(a_heading);
        // Skyrim's heading turns clockwise from north: ahead is (sin, cos), right is (cos, -sin).
        return { a_leader.x + o.x * c + o.y * s, a_leader.y - o.x * s + o.y * c, a_leader.z };
    }

    // A follower this far from CJ (metres, across) is put back behind him; this far he is lost and leaves.
    inline constexpr float kRegroupAfter = 40.0f;
    inline constexpr float kLostAfter = 300.0f;

    // ---- Spots ----
    //
    // About 15 homies a hold, in groups of two to four round the way into its capital: outside (the stable, the road to the
    // gate) and inside past the gate. A walled capital's inside is a worldspace of its own, so its two halves never load
    // together.

    inline constexpr int kSpotHomies = 15;
    inline constexpr int kGroupMax = 4;

    struct Standing
    {
        Vec3  at;
        float heading = 0.0f;  // turned to his group's middle
    };

    // a_count homies in groups round a_centre: the first group on it, the next ones a_spread apart round it, each group
    // in a ring of 1.2 m facing its middle. a_heading turns the whole layout (a gate's way out). Deterministic.
    inline std::vector<Standing> Groups(const Vec3& a_centre, float a_heading, int a_count, float a_spread = 9.0f * kMeter)
    {
        std::vector<Standing> out;
        int                   group = 0;
        int                   left = (std::max)(a_count, 0);
        while (left > 0) {
            const int size = left > kGroupMax ? (left - kGroupMax >= 2 ? kGroupMax : 3) : left;
            Vec3      middle = a_centre;
            if (group > 0) {
                // round the centre, alternating sides and growing a ring every four groups
                const float ring = a_spread * static_cast<float>(1 + (group - 1) / 4);
                const float angle = a_heading + Pose::kPi * 0.5f * static_cast<float>((group - 1) % 4) + Pose::kPi * 0.25f * static_cast<float>((group - 1) / 4);
                middle.x += std::sin(angle) * ring;
                middle.y += std::cos(angle) * ring;
            }
            for (int i = 0; i < size; i++) {
                const float a = a_heading + 2.0f * Pose::kPi * static_cast<float>(i) / static_cast<float>(size) + 0.4f;
                Standing    s;
                s.at = { middle.x + std::sin(a) * 1.2f * kMeter, middle.y + std::cos(a) * 1.2f * kMeter, middle.z };
                s.heading = Pose::HeadingOf(middle.x - s.at.x, middle.y - s.at.y);
                out.push_back(s);
            }
            left -= size;
            group++;
        }
        return out;
    }

    // How a hold's 15 are shared out: a walled capital puts 8 outside (half at the stable, half at the gate) and 7 inside,
    // an open town all 15 round its way in.
    struct Share
    {
        int stable = 0;
        int gateOutside = 0;
        int gateInside = 0;
        int town = 0;
    };

    inline Share ShareFor(bool a_walled, int a_total = kSpotHomies)
    {
        Share s;
        if (a_walled) {
            s.gateInside = a_total / 2;              // 7
            const int outside = a_total - s.gateInside;  // 8
            s.stable = outside / 2;
            s.gateOutside = outside - s.stable;
        } else {
            s.town = a_total;
        }
        return s;
    }

    // A spot fills while CJ is this near (metres, across) and none of it is in view; its homies go past the farther one.
    inline constexpr float kFillWithin = 110.0f;
    inline constexpr float kEmptyPast = 160.0f;

    inline bool Fills(float a_distance, bool a_inView, bool a_filled) { return !a_filled && !a_inView && a_distance <= kFillWithin; }
    inline bool Empties(float a_distance, bool a_filled) { return a_filled && !(a_distance <= kEmptyPast); }

    // ---- Picking one with G ----

    struct Candidate
    {
        std::uint32_t id = 0;
        Vec3          at;
        bool          targeted = false;  // CJ's lock-on or aim is on him
    };

    inline constexpr float kRecruitReach = 8.0f;    // metres
    inline constexpr float kRecruitCone = 60.0f;    // degrees either side of CJ's facing

    // The homie G recruits (or H dismisses): the one CJ targets, else the nearest within reach in front of him. 0 for none.
    inline std::uint32_t Pick(const Vec3& a_cj, float a_heading, const std::vector<Candidate>& a_candidates)
    {
        std::uint32_t best = 0;
        float         bestDistance = kRecruitReach * kMeter;
        for (const auto& c : a_candidates) {
            const float dx = c.at.x - a_cj.x, dy = c.at.y - a_cj.y;
            const float d = std::hypot(dx, dy);
            if (c.targeted && d <= 30.0f * kMeter) {
                return c.id;
            }
            if (d > bestDistance) {
                continue;
            }
            float off = std::abs(Pose::HeadingOf(dx, dy) - a_heading);
            off = (std::min)(off, 2.0f * Pose::kPi - off);
            if (d > 1.0f * kMeter && off > kRecruitCone * Pose::kPi / 180.0f) {
                continue;
            }
            best = c.id;
            bestDistance = d;
        }
        return best;
    }

    // ---- Shooting ----

    // A homie in a fight opens fire at his target within his gun's range, in a line of sight, in bursts of 1 to 2 s with
    // 1 to 2 s between, as San Andreas's gang peds fire.
    class Burst
    {
    public:
        explicit Burst(std::uint32_t a_seed = 1) : _seed(a_seed ? a_seed : 1) {}

        // a_seconds since the last call; a_may: he has a target he may shoot. Whether he fires now.
        bool Update(float a_seconds, bool a_may)
        {
            if (!a_may) {
                _firing = false;
                _left = 0.0f;
                return false;
            }
            _left -= a_seconds;
            if (_left <= 0.0f) {
                _firing = !_firing;
                _left = 1.0f + Next();  // 1 to 2 s, either way
            }
            return _firing;
        }

    private:
        float Next()
        {
            _seed = _seed * 1664525u + 1013904223u;
            return static_cast<float>((_seed >> 8) & 0xFFFF) / 65535.0f;
        }

        std::uint32_t _seed;
        bool          _firing = false;
        float         _left = 0.0f;
    };

    inline bool MayShoot(float a_distanceMeters, std::uint8_t a_weapon, bool a_lineOfSight)
    {
        return a_lineOfSight && a_distanceMeters <= RangeOf(a_weapon);
    }

    // ShotEvent.shooter: 0 CJ, slot + 1 a homie.
    inline constexpr std::uint8_t ShooterOf(std::uint32_t a_slot) { return static_cast<std::uint8_t>(a_slot + 1); }
    inline constexpr bool         FromCrew(std::uint8_t a_shooter) { return a_shooter >= 1 && a_shooter <= Protocol::kCrewMax; }
    inline constexpr std::uint32_t SlotOf(std::uint8_t a_shooter) { return static_cast<std::uint32_t>(a_shooter - 1); }

    // How much of a CJ's-shot's Skyrim damage a homie's shot does ([Crew] DamageFactor): San Andreas's gang peds are
    // poor shots.
    inline constexpr float kDamageFactor = 0.5f;

    // ---- CJ's hits on them ----
    //
    // CJ's bullets, blasts, fire and car hurt homies as anyone (careless rockets cost him his crew), but they never turn
    // on him and none of it is a crime: the damage has no aggressor (Skyrim's own hit would count a follower's friendly
    // hits and turn him hostile) and no crime is noted.
    struct HitRule
    {
        bool aggressor;  // through Skyrim's hit with the player as the attacker
        bool crime;      // noted for the wanted level
        bool skill;      // counts for CJ's weapon skill
    };

    inline constexpr HitRule RuleFor(bool a_victimIsHomie, bool a_byHomie)
    {
        if (a_byHomie) {
            return { false, false, false };  // his own kill: the homie is the aggressor, not CJ
        }
        if (a_victimIsHomie) {
            return { false, false, false };
        }
        return { true, true, true };
    }

    // ---- Slots ----

    // Which CrewInput member each homie is, kept while he lasts; a freed slot rests a moment before another takes it, so
    // San Andreas's last pose of the one before is never drawn for him.
    class Slots
    {
    public:
        static constexpr float kRest = 0.5f;  // s

        // a_id's slot, given one if it has none; -1 when all are taken.
        int Take(std::uint32_t a_id)
        {
            for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
                if (_ids[i] == a_id) {
                    return static_cast<int>(i);
                }
            }
            for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
                if (_ids[i] == 0 && _rest[i] <= 0.0f) {
                    _ids[i] = a_id;
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        int Find(std::uint32_t a_id) const
        {
            for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
                if (a_id && _ids[i] == a_id) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void Free(std::uint32_t a_id)
        {
            for (std::uint32_t i = 0; i < Protocol::kCrewMax; i++) {
                if (_ids[i] == a_id) {
                    _ids[i] = 0;
                    _rest[i] = kRest;
                }
            }
        }

        void Tick(float a_seconds)
        {
            for (auto& r : _rest) {
                r = (std::max)(0.0f, r - a_seconds);
            }
        }

        std::uint32_t IdAt(std::uint32_t a_slot) const { return a_slot < Protocol::kCrewMax ? _ids[a_slot] : 0; }

        void Clear() { *this = {}; }

    private:
        std::array<std::uint32_t, Protocol::kCrewMax> _ids{};
        std::array<float, Protocol::kCrewMax>         _rest{};
    };

    // ---- The puppet (San Andreas side) ----

    // Which way a homie's puppet faces, a San Andreas heading. While he walks or runs he faces the way he goes, as San
    // Andreas's peds turn into their walk: his velocity smoothed, turned toward at a ped's rate. Standing he turns to
    // a_still (CJ's heading for a follower, his own at a spot), and aiming he faces a_still, his aim, even on the move.
    class Facing
    {
    public:
        static constexpr float kTurnRate = 2.0f * Pose::kPi;  // rad/s
        static constexpr float kMinSpeed = 0.5f;              // m/s: slower, the velocity gives no direction
        static constexpr float kSmoothing = 0.25f;            // s
        static constexpr float kJump = 5.0f;                  // m: a load or a move, not a step

        // a_feet: where his feet are, metres; a_moving: he walks or runs (Scene::MotionTracker).
        float Update(const Pose::Vec3& a_feet, float a_seconds, float a_still, bool a_moving, bool a_aiming)
        {
            const float dx = a_feet.x - _last.x;
            const float dy = a_feet.y - _last.y;
            const bool  jumped = !_have || std::hypot(dx, dy) > kJump;
            _last = a_feet;
            if (jumped) {
                _have = true;
                _vx = _vy = 0.0f;
                _heading = a_still;
                return _heading;
            }
            if (a_seconds > 0.0f) {
                const float k = a_seconds / (a_seconds + kSmoothing);
                _vx += (dx / a_seconds - _vx) * k;
                _vy += (dy / a_seconds - _vy) * k;
            }
            float goal = a_still;
            if (a_moving && !a_aiming && Speed() >= kMinSpeed) {
                goal = Pose::SanAndreasHeading(Pose::HeadingOf(_vx, _vy));
            }
            _heading = Scene::TurnToward(_heading, goal, kTurnRate * a_seconds);
            return _heading;
        }

        float Speed() const { return std::hypot(_vx, _vy); }

    private:
        bool       _have = false;
        Pose::Vec3 _last;
        float      _vx = 0.0f;
        float      _vy = 0.0f;
        float      _heading = 0.0f;
    };

    // One animation on a clump, as San Andreas's CAnimBlendAssociation has it.
    struct Blend
    {
        float amount = 0.0f;        // m_fBlendAmount
        float delta = 0.0f;         // m_fBlendDelta, per second
        bool  deleteFaded = false;  // flag kBlendDeleteFaded: taken out once it has faded to nothing
        bool  sequences = true;     // its hierarchy has its sequences (its anim block is in memory)
    };

    inline constexpr std::uint16_t kBlendDeleteFaded = 0x4;

    // Whether a_blend is still on its clump after an update of a_step seconds and gives that update a node
    // (CAnimBlendAssociation::UpdateBlend takes out one faded to nothing with its delete flag set; one without sequences
    // gives no node).
    constexpr bool Outlives(const Blend& a_blend, float a_step)
    {
        const bool goes = a_blend.deleteFaded && a_blend.delta < 0.0f && a_blend.amount + a_blend.delta * a_step <= 0.0f;
        return !goes && a_blend.sequences;
    }

    // Whether RpAnimBlendClumpUpdateAnimations may run on a clump with these animations. San Andreas reads the first of
    // the nodes it gathers without looking, so a clump whose last animation fades out in that very update crashes it (a
    // homie's walk fading out before anything started his run).
    inline bool LeavesANode(const Blend* a_blends, std::size_t a_count, float a_step)
    {
        for (std::size_t i = 0; i < a_count; i++) {
            if (Outlives(a_blends[i], a_step)) {
                return true;
            }
        }
        return false;
    }
}
