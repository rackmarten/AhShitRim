// Skyrim's attacks on CJ's vehicle (protocol v24). While CJ is in a vehicle, Skyrim's arrows, blows, spells and
// explosions that reach it hit the vehicle's shape (San Andreas's collision box, published in its slot), not CJ
// inside: Skyrim works out what hit it and how hard (the damage table below), sends it to San Andreas as a
// HitVehicle effect, and San Andreas takes it off the vehicle's health, down to smoke, fire and the blow-up. In an
// open vehicle (a bike, a bicycle, a quad) CJ keeps half of what Skyrim does to him and the vehicle takes half of the
// table (a blast all of it). A
// wanted CJ's stopped car also sends the nearest pursuer to the driver's door to bust him (DoorRun). Header-only and
// free of game types, so the protocol tests cover it.
//
// Skyrim units (70 to the metre) and radians (0 north, clockwise) throughout. X east, Y north, Z up.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "run_over.h"
#include "vehicle_kinds.h"
#include "wanted.h"

namespace AhShitRim::VehicleHits
{
    using Body::Add;
    using Body::Dot;
    using Body::Scale;
    using Body::Sub;
    using Pose::Vec3;
    using RunOver::Box;
    using RunOver::Build;
    using RunOver::Frame;

    // ---- What hit it ----

    // EffectEvent.weaponType of a HitVehicle effect.
    enum class Attack : std::uint8_t
    {
        None = 0,
        Melee = 1,      // a weapon's blow
        Bash = 2,       // a shield's or a weapon's bash
        Unarmed = 3,    // a person's fists
        Claws = 4,      // a creature's own attack: a wolf's bite, a bear's claws, a giant's club, a mammoth's tusks
        Arrow = 5,
        Bolt = 6,       // a crossbow's
        Spell = 7,      // a spell's missile: a firebolt, an ice spike, lightning
        Flames = 8,     // a stream: Flames, Frostbite, Sparks, a dragon's breath
        Explosion = 9,  // a fireball's blast, a rune
    };

    inline constexpr std::uint8_t kAttackCount = 10;

    constexpr const char* NameOf(Attack a_attack)
    {
        constexpr const char* kNames[kAttackCount] = { "nothing", "a blow", "a bash", "a punch", "a creature's attack", "an arrow", "a bolt", "a spell",
            "a stream of magic", "an explosion" };
        const auto i = static_cast<std::uint8_t>(a_attack);
        return i < kAttackCount ? kNames[i] : "something";
    }

    // Skyrim's weapon types (WEAPON_TYPE), as the hit's source weapon has them.
    enum class Weapon : std::uint8_t
    {
        HandToHand = 0,
        Sword = 1,
        Dagger = 2,
        Axe = 3,
        Mace = 4,
        Greatsword = 5,
        Battleaxe = 6,  // and warhammers
        Bow = 7,
        Staff = 8,
        Crossbow = 9,
    };

    // What Skyrim's hit says about itself: its source (a weapon, else a spell), its projectile's type, the blow's flags.
    struct Source
    {
        bool   weapon = false;          // the source is a weapon (else a spell, a shout, an enchantment, or nothing)
        Weapon type = Weapon::HandToHand;
        bool   spell = false;           // the source is magic
        bool   stream = false;          // its projectile is a flamethrower, a beam or a cone (Flames, a dragon's breath)
        bool   explodes = false;        // its projectile has an explosion (a fireball)
        bool   bash = false;            // TESHitEvent's kBashAttack
        bool   person = true;           // the attacker is a humanoid (fists are a punch, not a creature's attack)
    };

    inline Attack AttackOf(const Source& a_source)
    {
        if (a_source.weapon) {
            switch (a_source.type) {
            case Weapon::Bow: return Attack::Arrow;
            case Weapon::Crossbow: return Attack::Bolt;
            case Weapon::Staff: return a_source.stream ? Attack::Flames : a_source.explodes ? Attack::Explosion : Attack::Spell;  // a staff casts
            case Weapon::HandToHand: return a_source.bash ? Attack::Bash : a_source.person ? Attack::Unarmed : Attack::Claws;
            default: return a_source.bash ? Attack::Bash : Attack::Melee;
            }
        }
        if (a_source.spell) {
            return a_source.stream ? Attack::Flames : a_source.explodes ? Attack::Explosion : Attack::Spell;
        }
        return a_source.bash ? Attack::Bash : a_source.person ? Attack::Unarmed : Attack::Claws;
    }

    // ---- How hard ----

    struct Blow
    {
        Attack attack = Attack::None;
        Build  build = Build::Person;  // who dealt it (RunOver::BuildOf): a giant's or a mammoth's blow hits hard whatever it holds
        float  weight = 0.0f;          // the weapon's weight (Skyrim's), for a blow
        bool   power = false;          // a power attack
    };

    inline constexpr float kVehicleHealth = 1000.0f;  // San Andreas's new vehicle; it smokes under 650 and burns under 250
    inline constexpr float kPowerScale = 1.75f;       // a power attack hits this much harder
    inline constexpr float kBashScale = 0.5f;
    inline constexpr float kMeleeBase = 12.0f;        // a blow: this, plus kMeleePerWeight for every unit of the weapon's weight
    inline constexpr float kMeleePerWeight = 2.5f;
    inline constexpr float kMeleeMax = 90.0f;
    inline constexpr float kDamageMax = 400.0f;       // no one hit takes more

    // The heavy ones' blows, whatever the attack: about what a wall at 50 km/h takes off a car in San Andreas.
    constexpr float HeavyBlow(Build a_build)
    {
        switch (a_build) {
        case Build::Giant: return 240.0f;    // four club blows and it burns
        case Build::Mammoth: return 300.0f;
        case Build::Dragon: return 160.0f;   // its bite and its tail (its breath is Flames)
        default: return 0.0f;
        }
    }

    // A creature's own attack by its build.
    constexpr float ClawsOf(Build a_build)
    {
        switch (a_build) {
        case Build::Small: return 4.0f;  // a skeever's bite
        case Build::Horse: return 15.0f;
        case Build::Beast: return 25.0f;  // a wolf, a bear, a troll, a sabre cat
        case Build::Person: return 8.0f;
        default: return HeavyBlow(a_build);
        }
    }

    // San Andreas vehicle health one hit takes, as the cops' guns take it in San Andreas (an M4's bullet 30 of a car's
    // 1000): some 35 arrows set a car on fire, a dozen of a swordsman's power attacks, four of a giant's blows.
    inline float DamageOf(const Blow& a_blow)
    {
        float damage = 0.0f;
        const float weight = std::isfinite(a_blow.weight) ? std::clamp(a_blow.weight, 0.0f, 60.0f) : 0.0f;
        switch (a_blow.attack) {
        case Attack::Melee: damage = (std::min)(kMeleeMax, kMeleeBase + kMeleePerWeight * weight); break;
        case Attack::Bash: damage = kBashScale * (std::min)(kMeleeMax, kMeleeBase + kMeleePerWeight * weight); break;
        case Attack::Unarmed: damage = ClawsOf(Build::Person); break;
        case Attack::Claws: damage = ClawsOf(a_blow.build); break;
        case Attack::Arrow: damage = 22.0f; break;
        case Attack::Bolt: damage = 32.0f; break;
        case Attack::Spell: damage = 40.0f; break;
        case Attack::Flames: damage = a_blow.build == Build::Dragon ? 30.0f : 12.0f; break;  // each counted hit of the stream (kGate apart)
        case Attack::Explosion: damage = 150.0f; break;
        default: return 0.0f;
        }
        // Up close the heavy ones hit as hard as their size, whatever they swing.
        const bool close = a_blow.attack == Attack::Melee || a_blow.attack == Attack::Bash || a_blow.attack == Attack::Unarmed || a_blow.attack == Attack::Claws;
        if (close && RunOver::Heavy(a_blow.build)) {
            damage = (std::max)(damage, HeavyBlow(a_blow.build));
        } else if (a_blow.power && close) {
            damage *= kPowerScale;
        }
        return (std::min)(damage, kDamageMax);
    }

    // ---- Who takes it ----

    enum class Cover : std::uint8_t
    {
        None,    // on foot, or gear (a jetpack, a parachute): Skyrim's hits are CJ's as ever
        Closed,  // a car, a monster truck, an aircraft: everything hits the vehicle
        Open,    // a bike, a bicycle, a quad: the rider is in the way too
    };

    constexpr Cover CoverOf(VehicleKinds::Kind a_kind)
    {
        switch (a_kind) {
        case VehicleKinds::Kind::Unknown: return Cover::None;
        case VehicleKinds::Kind::Bike:
        case VehicleKinds::Kind::Bicycle:
        case VehicleKinds::Kind::Quad: return Cover::Open;
        default: return Cover::Closed;
        }
    }

    struct Split
    {
        float vehicle = 0.0f;  // of the table's damage, to the vehicle
        float cj = 1.0f;       // of Skyrim's own damage to the player, kept (the rest given back)
    };

    inline constexpr float kOpenShare = 0.5f;  // an open vehicle: half the hits are the rider's, half the vehicle's

    // As San Andreas has it for bullets: a closed vehicle shields CJ, an open one only half (a blast takes the whole
    // bike as well).
    constexpr Split SplitOf(Cover a_cover, Attack a_attack)
    {
        switch (a_cover) {
        case Cover::Closed: return { 1.0f, 0.0f };
        case Cover::Open: return a_attack == Attack::Explosion ? Split{ 1.0f, kOpenShare } : Split{ kOpenShare, kOpenShare };
        default: return { 0.0f, 1.0f };
        }
    }

    // ---- The vehicle's shape ----

    // The vehicle's frame from its slot's vehicleFrame (a rotation taking X, Y, Z to its right, forward and up) at a_feet.
    inline Frame FrameOf(const Protocol::BodyBone& a_frame, const Vec3& a_feet)
    {
        const auto m = Body::ToMatrix(a_frame);
        Frame      out;
        out.pos = a_feet;
        out.right = { m.m[0][0], m.m[1][0], m.m[2][0] };
        out.forward = { m.m[0][1], m.m[1][1], m.m[2][1] };
        out.up = { m.m[0][2], m.m[1][2], m.m[2][2] };
        return out;
    }

    // Whether a slot's box is there to hit (San Andreas sent one: v24, and a model with a col model).
    inline bool HasBox(const Box& a_box) { return a_box.max.x > a_box.min.x && a_box.max.y > a_box.min.y && a_box.max.z > a_box.min.z; }

    inline Vec3 ToLocal(const Frame& a_frame, const Vec3& a_point)
    {
        const Vec3 d = Sub(a_point, a_frame.pos);
        return { Dot(d, a_frame.right), Dot(d, a_frame.forward), Dot(d, a_frame.up) };
    }

    inline Vec3 ToWorld(const Frame& a_frame, const Vec3& a_local)
    {
        return Add(a_frame.pos, Add(Add(Scale(a_frame.right, a_local.x), Scale(a_frame.forward, a_local.y)), Scale(a_frame.up, a_local.z)));
    }

    // The box's point nearest a_point (a_point itself inside it), world.
    inline Vec3 Nearest(const Box& a_box, const Frame& a_frame, const Vec3& a_point)
    {
        const Vec3 local = ToLocal(a_frame, a_point);
        return ToWorld(a_frame, { std::clamp(local.x, a_box.min.x, a_box.max.x), std::clamp(local.y, a_box.min.y, a_box.max.y),
                                    std::clamp(local.z, a_box.min.z, a_box.max.z) });
    }

    // How far a_point is from the box, 0 inside it.
    inline float GapTo(const Box& a_box, const Frame& a_frame, const Vec3& a_point) { return RunOver::Length(Sub(a_point, Nearest(a_box, a_frame, a_point))); }

    // Where along a_from to a_to (0 to 1) a projectile first enters the box grown by a_pad on every side: 0 if it
    // starts inside, nothing if it misses (the slab test, in the box's own frame).
    inline std::optional<float> Enters(const Box& a_box, const Frame& a_frame, const Vec3& a_from, const Vec3& a_to, float a_pad = 0.0f)
    {
        const Vec3  a = ToLocal(a_frame, a_from);
        const Vec3  b = ToLocal(a_frame, a_to);
        const float from[3] = { a.x, a.y, a.z };
        const float d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
        const float lo[3] = { a_box.min.x - a_pad, a_box.min.y - a_pad, a_box.min.z - a_pad };
        const float hi[3] = { a_box.max.x + a_pad, a_box.max.y + a_pad, a_box.max.z + a_pad };
        float       enter = 0.0f, leave = 1.0f;
        for (int i = 0; i < 3; i++) {
            if (!std::isfinite(from[i]) || !std::isfinite(d[i])) {
                return std::nullopt;
            }
            if (std::abs(d[i]) < 1e-6f) {
                if (from[i] < lo[i] || from[i] > hi[i]) {
                    return std::nullopt;
                }
                continue;
            }
            float t0 = (lo[i] - from[i]) / d[i];
            float t1 = (hi[i] - from[i]) / d[i];
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            enter = (std::max)(enter, t0);
            leave = (std::min)(leave, t1);
            if (enter > leave) {
                return std::nullopt;
            }
        }
        return enter;
    }

    inline constexpr float kReachPad = 40.0f;     // Skyrim units a blow reaches past the weapon's own reach (the swing's arc)
    inline constexpr float kFacingCos = 0.34f;    // an attacker within about 70 degrees of facing the box swings at it

    // Whether a blow by an attacker at a_at (its chest), facing a_heading, with a weapon reaching a_reach, lands on the box.
    inline bool Reaches(const Box& a_box, const Frame& a_frame, const Vec3& a_at, float a_heading, float a_reach)
    {
        const Vec3  nearest = Nearest(a_box, a_frame, a_at);
        const Vec3  to = Sub(nearest, a_at);
        const float gap = RunOver::Length(to);
        if (!std::isfinite(gap) || !std::isfinite(a_reach) || gap > a_reach + kReachPad) {
            return false;
        }
        const float flat = std::hypot(to.x, to.y);
        if (flat < 1.0f) {
            return true;  // in it, or right over it
        }
        return (std::sin(a_heading) * to.x + std::cos(a_heading) * to.y) / flat >= kFacingCos;
    }

    // ---- Counting hits ----

    inline constexpr double kGate = 0.3;  // seconds: one attacker's hits closer than this are one (a stream's ticks, an arrow seen twice)

    // Each attacker's last counted hit, so Skyrim's own hit on the player and the box's sight of the same arrow or blow
    // count once.
    class Gate
    {
    public:
        bool Lands(std::uint32_t a_id, double a_now)
        {
            for (auto& entry : _entries) {
                if (entry.id == a_id) {
                    if (a_now >= entry.at && a_now - entry.at < kGate) {
                        return false;
                    }
                    entry.at = a_now;
                    return true;
                }
            }
            if (_entries.size() >= kMax) {
                _entries.erase(std::min_element(_entries.begin(), _entries.end(), [](const Entry& a, const Entry& b) { return a.at < b.at; }));
            }
            _entries.push_back({ a_id, a_now });
            return true;
        }

        void Clear() { _entries.clear(); }

    private:
        static constexpr std::size_t kMax = 32;
        struct Entry
        {
            std::uint32_t id = 0;
            double        at = 0.0;
        };
        std::vector<Entry> _entries;
    };

    // ---- Busted at the door ----

    inline constexpr float kSendAfter = 0.3f;    // seconds nearly stopped before a pursuer goes for the door
    inline constexpr float kSendRange = 2800.0f; // 40 m: a pursuer farther from the door isn't sent
    inline constexpr float kMovingFor = 0.8f;    // seconds moving again that call him back
    inline constexpr float kStuckFor = 4.0f;     // seconds without getting kProgress closer, and he gives up
    inline constexpr float kProgress = 40.0f;
    inline constexpr float kRunMax = 15.0f;      // seconds a run takes at most
    inline constexpr float kBlockFor = 15.0f;    // seconds one who gave up isn't sent again

    // What the vehicle CJ is in allows a pursuer to do now: nothing while he's getting in or out, as San Andreas's cops
    // never bust him in the middle of it (a_gettingInOut, kPlayerGettingInOut).
    constexpr Wanted::Bust BustNow(Wanted::Bust a_bust, bool a_gettingInOut) { return a_gettingInOut ? Wanted::Bust::None : a_bust; }

    // Who may run for the door: a person (no giant, mammoth, dragon or creature busts anyone), on foot.
    constexpr bool CanRun(Build a_build, bool a_mounted) { return a_build == Build::Person && !a_mounted; }

    inline constexpr float kRecallHold = 1.5f;  // seconds a recalled runner stays (isn't sent away) so his offset is cleared first

    struct Pursuer
    {
        std::uint32_t id = 0;
        float         toDoor = 0.0f;  // from the driver's door (CJ himself on a bike), Skyrim units
    };

    struct Orders
    {
        std::uint32_t send = 0;    // run this one to the door, weapon away
        std::uint32_t recall = 0;  // this one's run is over: back to fighting
        bool          gaveUp = false;  // the recalled one was stuck or too slow
    };

    // Which pursuer runs to the door of CJ's stopped vehicle (Wanted::DoorSpot, or CJ on a bike), and when he gives up.
    // Wanted::StopWatch busts CJ once he's there.
    class DoorRun
    {
    public:
        // A frame of a_delta seconds: a_wanted while stars show, a_bust what the vehicle allows, a_speed the vehicle's
        // (Skyrim units a second), a_pursuers the pursuit near CJ with their distance to the door.
        Orders Tick(float a_delta, bool a_wanted, Wanted::Bust a_bust, float a_speed, std::span<const Pursuer> a_pursuers)
        {
            Orders out;
            const float dt = std::isfinite(a_delta) ? std::clamp(a_delta, 0.0f, 1.0f) : 0.0f;
            _clock += dt;
            std::erase_if(_blocked, [&](const Blocked& a_block) { return _clock >= a_block.until; });
            if (!a_wanted || a_bust == Wanted::Bust::None) {
                out.recall = Reset();
                return out;
            }
            if (!std::isfinite(a_speed) || a_speed > Wanted::kStoppedSpeed) {
                _moving += dt;
                _stopped = 0.0f;
            } else {
                _stopped += dt;
                _moving = 0.0f;
            }
            if (_runner) {
                const Pursuer* runner = nullptr;
                for (const auto& p : a_pursuers) {
                    if (p.id == _runner) {
                        runner = &p;
                    }
                }
                _elapsed += dt;
                if (!runner || _moving >= kMovingFor) {
                    out.recall = std::exchange(_runner, 0u);  // gone, or CJ drove off
                } else {
                    if (std::isfinite(runner->toDoor) && runner->toDoor < _best - kProgress) {
                        _best = runner->toDoor;
                        _sinceProgress = 0.0f;
                    } else {
                        _sinceProgress += dt;
                    }
                    if (_sinceProgress >= kStuckFor || _elapsed >= kRunMax) {
                        _blocked.push_back({ _runner, _clock + kBlockFor });
                        out.recall = std::exchange(_runner, 0u);
                        out.gaveUp = true;
                    }
                }
            }
            if (!_runner && _stopped >= kSendAfter) {
                const Pursuer* best = nullptr;
                for (const auto& p : a_pursuers) {
                    const bool blocked = std::ranges::any_of(_blocked, [&](const Blocked& a_block) { return a_block.id == p.id; });
                    if (p.id && !blocked && p.id != out.recall && std::isfinite(p.toDoor) && p.toDoor <= kSendRange && (!best || p.toDoor < best->toDoor)) {
                        best = &p;
                    }
                }
                if (best) {
                    _runner = best->id;
                    _best = best->toDoor;
                    _elapsed = _sinceProgress = 0.0f;
                    out.send = _runner;
                }
            }
            return out;
        }

        std::uint32_t Runner() const { return _runner; }

        // Everything forgotten; returns the runner to call back, 0 for none.
        std::uint32_t Reset()
        {
            _stopped = _moving = _elapsed = _sinceProgress = 0.0f;
            _blocked.clear();
            return std::exchange(_runner, 0u);
        }

    private:
        struct Blocked
        {
            std::uint32_t id = 0;
            float         until = 0.0f;
        };
        std::uint32_t        _runner = 0;
        float                _best = 0.0f;
        float                _elapsed = 0.0f;
        float                _sinceProgress = 0.0f;
        float                _stopped = 0.0f;
        float                _moving = 0.0f;
        float                _clock = 0.0f;
        std::vector<Blocked> _blocked;
    };

    // Where the runner stands relative to the player for Skyrim's KeepOffsetFromActor, which takes the offset in the
    // target's own frame (X right, Y forward): a_world from the player, the player facing a_heading.
    inline Wanted::Point LocalOffset(Wanted::Point a_world, float a_heading)
    {
        const float c = std::cos(a_heading), s = std::sin(a_heading);
        return { a_world.x * c - a_world.y * s, a_world.x * s + a_world.y * c };
    }

    inline constexpr float kBikeSide = 70.0f;  // on a bike the runner goes to CJ's left, this far

    // Where the runner goes: the driver's door of a car (its middle a_middle, heading a_heading), else beside CJ (a_cj).
    inline Wanted::Point RunTo(Wanted::Bust a_bust, Wanted::Point a_middle, float a_heading, Wanted::Point a_cj)
    {
        if (a_bust == Wanted::Bust::Door) {
            return Wanted::DoorSpot(a_middle, a_heading);
        }
        return { a_cj.x - std::cos(a_heading) * kBikeSide, a_cj.y + std::sin(a_heading) * kBikeSide };
    }
}
