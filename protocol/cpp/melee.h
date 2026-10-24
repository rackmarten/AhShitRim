// CJ on foot against Skyrim's actors (#895, on the proxy slot of run_over.h). Skyrim picks the actor CJ locks on
// to, as San Andreas's CPlayerPed::EvaluateTarget rates them: with fists or a melee weapon while he holds the right
// mouse button, or the nearest hostile in front when he swings without it, and flags its proxy (kProxyTarget) for
// San Andreas to turn him to face it; with a gun while he aims over the shoulder, and sends his shots at it. And on
// foot Skyrim sends the actors around CJ too, which San Andreas pushes him out of, so he bumps into people and
// creatures instead of walking through them. Header-only and free of game types, so the protocol tests cover it.
//
// Distances here are metres. Skyrim's headings are radians from north, clockwise (Pose::HeadingOf); San
// Andreas's are the negative (Pose::SanAndreasHeading).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "pose.h"
#include "run_over.h"
#include "scene.h"

namespace AhShitRim::Melee
{
    using Body::Add;
    using Body::Dot;
    using Body::Scale;
    using Body::Sub;
    using Pose::Vec3;

    // ---- The lock-on (Skyrim's side) ----

    inline constexpr float kViewAngle = 140.0f;      // degrees off the heading San Andreas still rates (PLAYER_MAX_TARGET_VIEW_ANGLE)
    inline constexpr float kHostilePriority = 0.8f;  // FindTargetPriority: someone out to kill or arrest the player
    inline constexpr float kOtherPriority = 0.1f;    // anyone else

    // How one kind of lock-on picks and keeps its target.
    struct Rules
    {
        float range = 0.0f;         // metres: who is farther isn't picked
        float breakRange = 0.0f;    // metres: a kept target farther than this is let go (a little over range, so it doesn't flicker)
        float pickAngle = 180.0f;   // degrees off the heading a new target may be at most
        float keepAngle = 180.0f;   // degrees off the heading a kept one may drift before it is let go (180: never)
        bool  hostileOnly = false;  // only someone fighting or hostile to the player
        float keepSeconds = 0.0f;   // how long the target outlasts letting go of the button
    };

    // The right mouse button with fists or a melee weapon: as San Andreas's lock-on, anyone in front within 10 m,
    // kept until out of reach whichever way CJ turns (San Andreas breaks a lock by range, not angle).
    inline constexpr Rules kMeleeLock{ 10.0f, 12.0f, 140.0f, 180.0f, false, 0.0f };
    // A punch or a swing with nobody locked on to: the nearest hostile in front within reach, kept through the combo.
    inline constexpr Rules kSwing{ 3.0f, 4.0f, 60.0f, 180.0f, true, 0.75f };
    // The right mouse button with a gun, aimed over the shoulder: San Andreas's pad lock-on, by where the camera looks
    // (the mouse aims); looking well away from the target lets it go for whoever is there, or free aim.
    inline constexpr Rules kGunLock{ 30.0f, 36.0f, 30.0f, 45.0f, false, 0.0f };

    inline constexpr float kLockRange = kMeleeLock.range;
    inline constexpr float kBreakRange = kMeleeLock.breakRange;

    struct Candidate
    {
        std::uint32_t id = 0;
        Vec3          offset;          // its middle from CJ's middle, metres
        bool          hostile = false;  // in combat with the player or hostile to him
    };

    // How far a_offset is off a_heading, degrees, 0..180.
    inline float DegreesOff(const Vec3& a_offset, float a_heading)
    {
        float off = Pose::HeadingOf(a_offset.x, a_offset.y) - a_heading;
        while (off <= -Pose::kPi) off += 2.0f * Pose::kPi;
        while (off > Pose::kPi) off -= 2.0f * Pose::kPi;
        return std::abs(off) * (180.0f / Pose::kPi);
    }

    // How San Andreas rates a target (CPlayerPed::EvaluateTarget): its priority, less the farther it is off the
    // heading (nothing at kViewAngle or more) and the farther away it is (by the fourth root, past a metre). 0 for
    // one out of a_rules' range or cone, or not hostile when they want that.
    inline float Rating(const Candidate& a_candidate, float a_heading, const Rules& a_rules = kMeleeLock)
    {
        const float distance = RunOver::Length(a_candidate.offset);
        if (!std::isfinite(distance) || distance > a_rules.range || (a_rules.hostileOnly && !a_candidate.hostile)) {
            return 0.0f;
        }
        const float off = DegreesOff(a_candidate.offset, a_heading);
        float       view = 1.0f - off / kViewAngle;
        if (!(view > 0.0f) || off > a_rules.pickAngle) {
            return 0.0f;
        }
        if (distance > 1.0f) {
            view /= std::sqrt(std::sqrt(distance));
        }
        return (a_candidate.hostile ? kHostilePriority : kOtherPriority) * view;
    }

    // The candidates worth locking on to, best first: the indices into a_all.
    inline std::vector<std::size_t> Ranked(std::span<const Candidate> a_all, float a_heading, const Rules& a_rules = kMeleeLock)
    {
        std::vector<std::size_t> out;
        std::vector<float>       rating(a_all.size(), 0.0f);
        for (std::size_t i = 0; i < a_all.size(); i++) {
            rating[i] = Rating(a_all[i], a_heading, a_rules);
            if (rating[i] > 0.0f) {
                out.push_back(i);
            }
        }
        std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) { return rating[a] > rating[b]; });
        return out;
    }

    // Who CJ is locked on to. San Andreas keeps a lock until the target is out of reach (DoesTargetHaveToBeBroken),
    // so a better one walking up doesn't take it; a lost one is replaced by the best one in sight.
    class Lock
    {
    public:
        // Once a frame. a_held: the button that locks on, held. a_all: who could be locked on to (alive, loaded, not a
        // follower or a child). a_heading: CJ's facing, or the camera's for a gun. a_sees(i): whether CJ sees
        // a_all[i], asked only of a new target. a_seconds since the last call (for a_rules.keepSeconds). The
        // target's id, 0 for none.
        template <class Sees>
        std::uint32_t Update(bool a_held, std::span<const Candidate> a_all, float a_heading, Sees&& a_sees, const Rules& a_rules = kMeleeLock,
            float a_seconds = 0.0f)
        {
            _unheld = a_held ? 0.0f : _unheld + (std::isfinite(a_seconds) && a_seconds > 0.0f ? a_seconds : 0.0f);
            if (!a_held && !(_target && a_rules.keepSeconds > 0.0f && _unheld <= a_rules.keepSeconds)) {
                _target = 0;
                return 0;
            }
            if (_target) {
                const auto kept = std::find_if(a_all.begin(), a_all.end(), [&](const Candidate& c) { return c.id == _target; });
                if (kept != a_all.end() && RunOver::Length(kept->offset) <= a_rules.breakRange && DegreesOff(kept->offset, a_heading) <= a_rules.keepAngle) {
                    return _target;
                }
                _target = 0;
            }
            if (!a_held) {
                return 0;  // only a held button picks a new one
            }
            for (const auto i : Ranked(a_all, a_heading, a_rules)) {
                if (a_sees(i)) {
                    _target = a_all[i].id;
                    break;
                }
            }
            return _target;
        }

        std::uint32_t Target() const { return _target; }
        void          Clear() { _target = 0; }

    private:
        std::uint32_t _target = 0;
        float         _unheld = 0.0f;
    };

    // ---- Shots at a locked target (Skyrim's side) ----

    // Where a gun's shot goes when CJ is locked on: the head when the shot's own line (San Andreas's, through the
    // crosshair) passes nearer it than the chest, the chest otherwise, as San Andreas's lock-on aims at the body.
    // a_dir is a unit vector.
    inline Vec3 AimPoint(const Vec3& a_origin, const Vec3& a_dir, const Vec3& a_head, const Vec3& a_chest)
    {
        if (!(std::abs(Dot(a_dir, a_dir) - 1.0f) < 0.01f)) {
            return a_chest;
        }
        const auto missBy = [&](const Vec3& a_point) {
            const Vec3  to = Sub(a_point, a_origin);
            const float along = (std::max)(0.0f, Dot(to, a_dir));
            return RunOver::Length(Sub(to, Scale(a_dir, along)));
        };
        return missBy(a_head) < missBy(a_chest) ? a_head : a_chest;
    }

    // San Andreas's lock-on marker's colour by the target's health (0..1): green when unhurt, through yellow, red
    // near death. RGBA packed as the overlay takes it (red in the low byte), opaque.
    inline std::uint32_t MarkerColour(float a_health)
    {
        const float h = std::isfinite(a_health) ? std::clamp(a_health, 0.0f, 1.0f) : 1.0f;
        const auto  red = static_cast<std::uint32_t>(std::lround(255.0f * (std::min)(1.0f, 2.0f * (1.0f - h))));
        const auto  green = static_cast<std::uint32_t>(std::lround(255.0f * (std::min)(1.0f, 2.0f * h)));
        return 0xFF000000u | (green << 8) | red;
    }

    // ---- The turn (San Andreas's side) ----

    inline constexpr float kTurnRate = 3.0f * Pose::kPi;  // rad/s: a half turn to the target in a third of a second

    // The San Andreas heading that faces from a_from to a_to (CGeneral::GetATanOf's convention), a_current when
    // they are on top of each other.
    inline float Facing(const Vec3& a_from, const Vec3& a_to, float a_current)
    {
        const float dx = a_to.x - a_from.x, dy = a_to.y - a_from.y;
        if (!(dx * dx + dy * dy > 0.01f)) {
            return a_current;
        }
        return Pose::SanAndreasHeading(Pose::HeadingOf(dx, dy));
    }

    // CJ's heading a_seconds on, turning from a_current to face a_to at kTurnRate.
    inline float Turn(float a_current, const Vec3& a_from, const Vec3& a_to, float a_seconds)
    {
        const float step = std::isfinite(a_seconds) && a_seconds > 0.0f ? kTurnRate * (std::min)(a_seconds, 0.1f) : 0.0f;
        return Scene::TurnToward(a_current, Facing(a_from, a_to, a_current), step);
    }

    // ---- Who Skyrim sends on foot ----

    inline constexpr float kFootRange = 12.0f * RunOver::kMeter;  // Skyrim units: actors farther from CJ on foot aren't sent

    // The proxies to send on foot: the nearest within kFootRange, at most a_max, and the target among them even in
    // a crowd (in the last place if it wouldn't have made it). a_target is an index into a_all, or a_all.size().
    inline std::vector<std::size_t> OnFoot(std::span<const RunOver::Candidate> a_all, std::size_t a_target, std::size_t a_max = Protocol::kProxyMax)
    {
        auto out = RunOver::Nearest(a_all, a_max, kFootRange);
        if (a_target < a_all.size() && a_max > 0 && std::find(out.begin(), out.end(), a_target) == out.end()) {
            if (out.size() >= a_max) {
                out.back() = a_target;
            } else {
                out.push_back(a_target);
            }
        }
        return out;
    }

    // ---- CJ against the capsules (San Andreas's side) ----

    inline constexpr float kCjRadius = 0.35f;  // metres: CJ's capsule, about San Andreas's ped collision sphere
    inline constexpr float kCjHeight = 1.8f;
    inline constexpr float kPushMax = 0.5f;    // metres CJ is moved in a frame at most (one caught inside a giant comes out over a few)

    // CJ's capsule standing at a_feet.
    inline RunOver::Capsule CjAt(const Vec3& a_feet)
    {
        const Vec3 a = Add(a_feet, { 0.0f, 0.0f, kCjRadius });
        return { a, Add(a_feet, { 0.0f, 0.0f, kCjHeight - kCjRadius }), kCjRadius };
    }

    // The closest points of segments p0-p1 and q0-q1 (the textbook clamped solution).
    inline void Closest(const Vec3& p0, const Vec3& p1, const Vec3& q0, const Vec3& q1, Vec3& a_onP, Vec3& a_onQ)
    {
        const Vec3  d1 = Sub(p1, p0), d2 = Sub(q1, q0), r = Sub(p0, q0);
        const float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
        float       s = 0.0f, t = 0.0f;
        constexpr float kTiny = 1e-8f;
        if (a <= kTiny && e <= kTiny) {
            s = t = 0.0f;
        } else if (a <= kTiny) {
            t = std::clamp(f / e, 0.0f, 1.0f);
        } else {
            const float c = Dot(d1, r);
            if (e <= kTiny) {
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else {
                const float b = Dot(d1, d2), denom = a * e - b * b;
                s = denom > kTiny * a * e ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f) {
                    t = 0.0f;
                    s = std::clamp(-c / a, 0.0f, 1.0f);
                } else if (t > 1.0f) {
                    t = 1.0f;
                    s = std::clamp((b - c) / a, 0.0f, 1.0f);
                }
            }
        }
        a_onP = Add(p0, Scale(d1, s));
        a_onQ = Add(q0, Scale(d2, t));
    }

    struct Push
    {
        Vec3 move;    // metres, level: where CJ goes to stand clear
        Vec3 normal;  // level unit vector from the actor to CJ
    };

    // How far CJ (a_cj) has to step aside, level, to stand clear of an actor's capsule; nothing when they don't
    // touch. Straight on top of each other, he goes back along a_back.
    inline std::optional<Push> PushOut(const RunOver::Capsule& a_cj, const RunOver::Capsule& a_actor, const Vec3& a_back)
    {
        Vec3 onCj, onActor;
        Closest(a_cj.a, a_cj.b, a_actor.a, a_actor.b, onCj, onActor);
        const Vec3  d = Sub(onCj, onActor);
        const float reach = a_cj.radius + a_actor.radius;
        if (!(Dot(d, d) < reach * reach)) {
            return std::nullopt;
        }
        // Level: CJ steps aside, never up onto someone or down through the ground. The level distance that
        // clears them at this height difference.
        const float level = std::sqrt(d.x * d.x + d.y * d.y);
        const float clear = std::sqrt((std::max)(0.0f, reach * reach - d.z * d.z));
        Push        out;
        out.normal = level > 1e-4f ? Vec3{ d.x / level, d.y / level, 0.0f } : Body::Normalize({ a_back.x, a_back.y, 0.0f }, { 0.0f, -1.0f, 0.0f });
        out.move = Scale(out.normal, clear - level);
        return out;
    }

    struct Resolved
    {
        Vec3 move;              // metres, at most kPushMax
        Vec3 velocity;          // CJ's velocity less what took him into them
        int  touching = 0;
    };

    // CJ standing at a_feet moving at a_velocity against the actors' capsules: each pushes him out in turn, and
    // what of his velocity goes into one is taken away, so he stops against a guard and slides along a mammoth.
    inline Resolved Resolve(const Vec3& a_feet, const Vec3& a_velocity, const Vec3& a_back, std::span<const RunOver::Capsule> a_actors)
    {
        Resolved out;
        out.velocity = a_velocity;
        Vec3 feet = a_feet;
        for (const auto& actor : a_actors) {
            const auto push = PushOut(CjAt(feet), actor, a_back);
            if (!push) {
                continue;
            }
            out.touching++;
            feet = Add(feet, push->move);
            const float into = Dot(out.velocity, push->normal);
            if (into < 0.0f) {
                out.velocity = Sub(out.velocity, Scale(push->normal, into));
            }
        }
        out.move = Sub(feet, a_feet);
        const float length = RunOver::Length(out.move);
        if (length > kPushMax) {
            out.move = Scale(out.move, kPushMax / length);
        }
        return out;
    }
}
