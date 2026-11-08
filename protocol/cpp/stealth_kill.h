// CJ's knife stealth kill (v36), the game-free half. San Andreas's own (CTaskSimplePlayerOnFoot::ProcessPlayerWeapon):
// locked on with the knife behind someone who hasn't noticed him, the right mouse button raises the knife
// (ANIM_ID_KILL_PARTIAL), the left one grabs and stabs (ANIM_ID_KILL_KNIFE_PLAYER) and the victim dies. San Andreas has
// no peds for Skyrim's actors, so its own test never runs: Skyrim decides who can be killed (Check, from
// CPedIntelligence::TestForStealthKill plus Skyrim's own idea of unaware) and flags the lock-on target's proxy
// (kProxyStealthKill); San Andreas plays CJ's half against that proxy (HoldStep, StabSeconds) and sends the grab and
// the stab; at the stab Skyrim checks again (CheckStab, the full test) and kills the victim through its own hit.
// Only people: never creatures, children or essential actors.
//
// Positions are Skyrim units, distances metres. Skyrim's headings are radians from north, clockwise (Pose::HeadingOf).
// Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "damage.h"
#include "pose.h"
#include "weapons.h"

namespace AhShitRim::StealthKill
{
    using Pose::Vec3;

    inline constexpr float kRange = 2.5f;           // metres from CJ to the victim (CPedIntelligence::STEALTH_KILL_RANGE, 0x8D2398)
    inline constexpr float kStabRange = 3.5f;       // metres at the stab: CJ is held a metre off, the victim may have taken a step
    inline constexpr float kHold = 1.0f;            // metres CJ is held from the victim (CTaskSimpleStealthKill::ProcessPed)
    inline constexpr float kHoldSlack = 0.02f;      // metres off kHold that are left alone
    inline constexpr float kHoldSpeed = 2.5f;       // m/s CJ slides toward kHold: 0.05 m a 1/50 s step
    inline constexpr float kSprintBlend = 1.9f;     // CPlayerData::m_fMoveBlendRatio from which CJ is too fast to raise the knife
    inline constexpr float kRaised = 0.5f;          // KILL_PARTIAL's blend over which the left button stabs
    inline constexpr float kRaiseRate = 8.0f;       // its blend a second, up and down (BlendAnimation's 8.0)
    inline constexpr float kStabShare = 0.55f;      // of KILL_KNIFE_PLAYER: the stab when the victim's anim length is unknown
    inline constexpr float kStabDefault = 1.2f;     // s, when neither length is known
    inline constexpr float kGrabMax = 4.0f;         // s Skyrim holds a grabbed victim waiting for the stab
    inline constexpr float kKillMax = 6.0f;         // s CJ's kill runs at most, whatever its anim says
    inline constexpr float kSlumpKnock = 1.0f;      // KnockExplosion's magnitude from behind at the stab (the least there is): the body slumps forward

    // ---- Who can be killed (Skyrim's side) ----

    // People: the races with ActorTypeNPC (the playable ones, dremora, the elves of Solstheim and the Snow Elves)
    // and draugr and falmer, which have no such keyword but stand and fall like people. Never giants, dragons or
    // the beast forms (werewolves, vampire lords).
    inline bool IsPerson(std::string_view a_raceEditorId, bool a_npcKeyword, bool a_dragonKeyword)
    {
        const auto race = Damage::Lower(a_raceEditorId);
        if (race.find("beast") != std::string::npos || race.find("werewolf") != std::string::npos) {
            return false;
        }
        if (Damage::ClassOf(a_raceEditorId, a_dragonKeyword) != Damage::Class::Normal) {
            return false;
        }
        return a_npcKeyword || race.starts_with("draugr") || race.starts_with("falmer");
    }

    struct Target
    {
        bool  person = false;     // IsPerson
        bool  child = false;
        bool  essential = false;  // essential or a quest's protected alias that can't die
        bool  dead = false;
        bool  mounted = false;
        bool  standing = true;    // on its feet: not sitting, sleeping, knocked down, ragdolled or bleeding out (SA: head above its feet)
        bool  sneaking = false;   // SA: not ducking
        bool  running = false;    // running or sprinting (SA: move state under PEDMOVE_RUN)
        bool  fighting = false;   // in combat with the player, or the player its combat target (SA: not attacking CJ)
        bool  detects = false;    // it detects the player (Skyrim's detection; Skyrim's sneak attack needs this off too)
        Vec3  feet;
        float heading = 0.0f;
    };

    enum class Verdict : std::uint8_t
    {
        Yes,
        NotKnife,
        NotPerson,
        Child,
        Essential,
        Dead,
        Mounted,
        NotStanding,
        Sneaking,
        Running,
        Fighting,
        Detects,
        TooFar,
        Facing,
    };

    constexpr const char* Why(Verdict a_verdict)
    {
        switch (a_verdict) {
        case Verdict::Yes: return "can be killed";
        case Verdict::NotKnife: return "not the knife";
        case Verdict::NotPerson: return "not a person";
        case Verdict::Child: return "a child";
        case Verdict::Essential: return "essential";
        case Verdict::Dead: return "dead";
        case Verdict::Mounted: return "riding";
        case Verdict::NotStanding: return "not on its feet";
        case Verdict::Sneaking: return "sneaking";
        case Verdict::Running: return "running";
        case Verdict::Fighting: return "fighting the player";
        case Verdict::Detects: return "sees the player";
        case Verdict::TooFar: return "too far";
        case Verdict::Facing: return "facing CJ";
        }
        return "?";
    }

    // Metres from a_from to a_to (Skyrim units).
    inline float Distance(const Vec3& a_from, const Vec3& a_to)
    {
        const float dx = a_to.x - a_from.x, dy = a_to.y - a_from.y, dz = a_to.z - a_from.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz) * Pose::kMetersPerUnit;
    }

    // Whether a_target, facing a_heading, has its back to someone at a_cj: SA's DotProduct(target - CJ, target's
    // forward) > 0, on the ground plane.
    inline bool Behind(const Vec3& a_cj, const Vec3& a_target, float a_heading)
    {
        const float dx = a_target.x - a_cj.x, dy = a_target.y - a_cj.y;
        return dx * std::sin(a_heading) + dy * std::cos(a_heading) > 0.0f;
    }

    // Who can't be killed at all, whatever CJ does: the full test, as San Andreas makes it at the stab
    // (TestForStealthKill(target, true): on foot, alive, on its feet), with Skyrim's exclusions.
    inline Verdict Excluded(std::uint8_t a_weapon, const Target& a_target)
    {
        if (a_weapon != Weapons::Knife) {
            return Verdict::NotKnife;
        }
        if (!a_target.person) {
            return Verdict::NotPerson;
        }
        if (a_target.child) {
            return Verdict::Child;
        }
        if (a_target.essential) {
            return Verdict::Essential;
        }
        if (a_target.dead) {
            return Verdict::Dead;
        }
        if (a_target.mounted) {
            return Verdict::Mounted;
        }
        if (!a_target.standing) {
            return Verdict::NotStanding;
        }
        return Verdict::Yes;
    }

    // Whether CJ, at a_cj holding a_weapon, can raise the knife on his lock-on target: TestForStealthKill(target,
    // false) and unaware in Skyrim's terms (not fighting the player, not detecting it). CJ himself may crouch.
    inline Verdict Check(const Vec3& a_cj, std::uint8_t a_weapon, const Target& a_target)
    {
        if (const auto excluded = Excluded(a_weapon, a_target); excluded != Verdict::Yes) {
            return excluded;
        }
        if (a_target.sneaking) {
            return Verdict::Sneaking;
        }
        if (a_target.running) {
            return Verdict::Running;
        }
        if (a_target.fighting) {
            return Verdict::Fighting;
        }
        if (a_target.detects) {
            return Verdict::Detects;
        }
        if (!(Distance(a_cj, a_target.feet) <= kRange)) {
            return Verdict::TooFar;
        }
        if (!Behind(a_cj, a_target.feet, a_target.heading)) {
            return Verdict::Facing;
        }
        return Verdict::Yes;
    }

    // At the stab: the full test, and still within kStabRange of CJ (not run off or carried away).
    inline Verdict CheckStab(const Vec3& a_cj, std::uint8_t a_weapon, const Target& a_target)
    {
        if (const auto excluded = Excluded(a_weapon, a_target); excluded != Verdict::Yes) {
            return excluded;
        }
        return Distance(a_cj, a_target.feet) <= kStabRange ? Verdict::Yes : Verdict::TooFar;
    }

    // ---- CJ's half (San Andreas's side) ----

    // Whether CJ raises the knife this frame: Skyrim flagged his lock-on target, he holds the knife and the right
    // mouse button, and isn't sprinting (move blend under kSprintBlend).
    constexpr bool Raises(bool a_flagged, std::uint8_t a_weapon, bool a_aiming, float a_moveBlend, bool a_sprinting)
    {
        return a_flagged && a_weapon == Weapons::Knife && a_aiming && a_moveBlend < kSprintBlend && !a_sprinting;
    }

    // KILL_PARTIAL's blend a_seconds on, toward 1 while raised and 0 when not, at kRaiseRate.
    inline float Raise(float a_blend, bool a_raised, float a_seconds)
    {
        const float step = std::isfinite(a_seconds) && a_seconds > 0.0f ? kRaiseRate * a_seconds : 0.0f;
        const float blend = std::isfinite(a_blend) ? a_blend : 0.0f;
        return std::clamp(a_raised ? blend + step : blend - step, 0.0f, 1.0f);
    }

    // Whether the left button's press now grabs: the knife is up past kRaised.
    constexpr bool Stabs(float a_blend, bool a_fireJustDown) { return a_fireJustDown && a_blend > kRaised; }

    // How far CJ (at a_cj, metres) moves this step on the ground plane toward or away from a_victim, so that he
    // stands kHold from it: at most kHoldSpeed * a_seconds, nothing within kHoldSlack.
    inline Vec3 HoldStep(const Vec3& a_cj, const Vec3& a_victim, float a_seconds)
    {
        const float dx = a_victim.x - a_cj.x, dy = a_victim.y - a_cj.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const float shift = distance - kHold;
        if (!(distance > 1e-3f) || !(std::fabs(shift) > kHoldSlack) || !std::isfinite(a_seconds) || !(a_seconds > 0.0f)) {
            return {};
        }
        const float most = kHoldSpeed * (std::min)(a_seconds, 0.1f);
        const float move = std::clamp(shift, -most, most);
        return { dx / distance * move, dy / distance * move, 0.0f };
    }

    // When, a_seconds into KILL_KNIFE_PLAYER, the knife goes in: when San Andreas's victim would go from being
    // grabbed (KILL_KNIFE_PED_DAMAGE) to dying (KILL_KNIFE_PED_DIE), so a_victimAnim's length, within CJ's anim; a share
    // of CJ's anim when the victim's isn't known, kStabDefault when neither is. Lengths of 0 or less are unknown.
    inline float StabSeconds(float a_cjAnim, float a_victimAnim)
    {
        const bool cj = std::isfinite(a_cjAnim) && a_cjAnim > 0.0f;
        const bool victim = std::isfinite(a_victimAnim) && a_victimAnim > 0.0f;
        if (victim) {
            return cj ? (std::min)(a_victimAnim, a_cjAnim) : a_victimAnim;
        }
        return cj ? a_cjAnim * kStabShare : kStabDefault;
    }

    // How long CJ's kill runs: his anim, at most kKillMax; kStabDefault's anim when it isn't known.
    inline float KillSeconds(float a_cjAnim)
    {
        return std::isfinite(a_cjAnim) && a_cjAnim > 0.0f ? (std::min)(a_cjAnim, kKillMax) : kStabDefault / kStabShare;
    }
}
