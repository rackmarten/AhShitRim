// CJ's explosives and fire in Skyrim (v15), the game-free half: what each of San Andreas's projectiles
// does (a rocket and a Molotov go off where they touch, a grenade and tear gas bounce until their fuse
// runs out), their flight under San Andreas's gravity, the bounce, how hard a blast hits and throws people
// by how far they stand, which locks it opens, the tear gas cloud and the flamethrower's cone; and (v17) a
// satchel charge sticking to what it lands on, and the heat seeker's lock and steering. Header-only and free
// of game types, so the protocol tests cover it.
//
// Units are Skyrim's (70 to the metre) and seconds; damage is in San Andreas's health points, which
// Damage::ToSkyrim turns into Skyrim's as it does a bullet's.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "body.h"
#include "damage.h"
#include "pose.h"
#include "weapons.h"

namespace AhShitRim::Explosives
{
    using Vec3 = Damage::Vec3;

    inline Vec3  Add(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline Vec3  Sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline Vec3  Scale(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
    inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // ---- What it is ----

    // The types San Andreas gives CProjectileInfo::AddProjectile (its eWeaponType): the thrown weapons are
    // themselves, the rocket launchers fire 19 and 20.
    inline constexpr std::uint8_t kRocket = 19;
    inline constexpr std::uint8_t kHeatSeeker = 20;

    enum class Kind : std::uint8_t
    {
        None,
        Rocket,   // the rocket launcher's and the heat seeker's
        Grenade,
        TearGas,
        Molotov,
        Satchel,  // a satchel charge: sticks where it lands, goes off when the detonator says
        Vehicle,  // (v18) a vehicle blowing up: no projectile, its blast only
    };

    constexpr Kind KindOf(std::uint8_t a_projectileType)
    {
        switch (a_projectileType) {
        case Weapons::Grenade: return Kind::Grenade;
        case Weapons::TearGas: return Kind::TearGas;
        case Weapons::Molotov: return Kind::Molotov;
        case Weapons::SatchelCharge: return Kind::Satchel;
        case kRocket:
        case kHeatSeeker: return Kind::Rocket;
        default: return Kind::None;
        }
    }

    // The weapon a projectile counts as (the damage table, the skill hits): the launcher for a rocket.
    constexpr std::uint8_t WeaponOf(std::uint8_t a_projectileType)
    {
        return a_projectileType == kRocket ? Weapons::RocketLauncher : a_projectileType == kHeatSeeker ? Weapons::RocketLauncherHs : a_projectileType;
    }

    // ---- Flight ----

    // San Andreas's gravity (GAME_GRAVITY, 0.008 m a step squared at 50 steps a second): 20 m/s², stronger than
    // Earth's, and what its throws are aimed for. A rocket flies straight.
    inline constexpr float kGravity = 0.008f * 50.0f * 50.0f * kMeter;
    inline constexpr float kRestitution = 0.35f;  // of the speed into a surface a bounce keeps
    inline constexpr float kFriction = 0.7f;      // of the speed along it
    inline constexpr float kRestSpeed = 0.5f * kMeter;  // slower than this after a bounce, it lies still
    inline constexpr float kLifeMax = 20.0f;      // seconds: one that never went off is dropped then
    inline constexpr float kNever = 1e9f;         // a satchel's fuse: it waits for the detonator

    // When a San Andreas projectile goes off by itself if it sent no fuse: a grenade's 2 s (gaProjectileInfo's
    // destroy time), tear gas starts its cloud as soon, a rocket gives up out of reach.
    constexpr float FuseSeconds(Kind a_kind, std::uint32_t a_fuseMs)
    {
        if (a_kind == Kind::Satchel) {
            return kNever;
        }
        if (a_fuseMs > 0) {
            return static_cast<float>(a_fuseMs) / 1000.0f;
        }
        switch (a_kind) {
        case Kind::Grenade:
        case Kind::TearGas: return 2.0f;
        case Kind::Rocket: return 10.0f;
        default: return kLifeMax;  // a Molotov goes off when it lands
        }
    }

    constexpr bool Falls(Kind a_kind) { return a_kind != Kind::Rocket; }

    // A rocket and a Molotov go off where they touch anything, a grenade and tear gas bounce.
    constexpr bool BurstsOnContact(Kind a_kind) { return a_kind == Kind::Rocket || a_kind == Kind::Molotov; }

    // A satchel sticks to the first thing it touches, as San Andreas's does.
    constexpr bool SticksOnContact(Kind a_kind) { return a_kind == Kind::Satchel; }

    struct Flight
    {
        Vec3 position;
        Vec3 velocity;
    };

    inline constexpr float kRocketSpeed = 40.0f * kMeter;  // a rocket San Andreas sent slower (it speeds them up as they go) flies this fast

    // How it leaves: as San Andreas threw it; a rocket at least kRocketSpeed, along its velocity or, without one,
    // the way it faces (a_forward, its matrix's Y).
    inline Vec3 LaunchVelocity(Kind a_kind, const Vec3& a_velocity, const Vec3& a_forward)
    {
        if (a_kind != Kind::Rocket) {
            return a_velocity;
        }
        const float speed = Length(a_velocity);
        if (speed >= kRocketSpeed) {
            return a_velocity;
        }
        const Vec3  along = speed > 1.0f ? a_velocity : a_forward;
        const float length = Length(along);
        return length > 1e-4f ? Scale(along, kRocketSpeed / length) : Vec3{};
    }

    // a_body after a_dt seconds of free flight.
    inline Flight Step(const Flight& a_body, Kind a_kind, float a_dt)
    {
        Flight next = a_body;
        if (Falls(a_kind)) {
            next.velocity.z -= kGravity * a_dt;
        }
        next.position = Add(a_body.position, Scale(Add(a_body.velocity, next.velocity), 0.5f * a_dt));
        return next;
    }

    // The velocity after bouncing off a surface with a_normal (a unit vector): what went into it comes back
    // at kRestitution, what ran along it keeps kFriction. Moving away from it already, it is kept.
    inline Vec3 Bounce(const Vec3& a_velocity, const Vec3& a_normal)
    {
        const float into = Dot(a_velocity, a_normal);
        if (into >= 0.0f) {
            return a_velocity;
        }
        const Vec3 normal = Scale(a_normal, into);
        const Vec3 along = Sub(a_velocity, normal);
        return Sub(Scale(along, kFriction), Scale(normal, kRestitution));
    }

    inline bool Rests(const Vec3& a_velocity) { return Length(a_velocity) < kRestSpeed; }

    // ---- Drawing it ----

    using Quat = Body::Quat;

    inline Quat Mul(const Quat& a, const Quat& b)
    {
        return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    }

    inline Vec3 Rotate(const Quat& q, const Vec3& v)
    {
        const Quat r = Mul(Mul(q, { v.x, v.y, v.z, 0.0f }), { -q.x, -q.y, -q.z, q.w });
        return { r.x, r.y, r.z };
    }

    // a_angle radians round a_axis (a unit vector).
    inline Quat AxisAngle(const Vec3& a_axis, float a_angle)
    {
        const float s = std::sin(0.5f * a_angle);
        return { a_axis.x * s, a_axis.y * s, a_axis.z * s, std::cos(0.5f * a_angle) };
    }

    // The shortest turn taking direction a_from to direction a_to (none for opposite ones).
    inline Quat FromTo(const Vec3& a_from, const Vec3& a_to)
    {
        const float lf = Length(a_from), lt = Length(a_to);
        if (!(lf > 1e-4f) || !(lt > 1e-4f)) {
            return {};
        }
        const Vec3  u = Scale(a_from, 1.0f / lf), v = Scale(a_to, 1.0f / lt);
        const Vec3  axis{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
        const float s = Length(axis);
        return s > 1e-6f ? AxisAngle(Scale(axis, 1.0f / s), std::atan2(s, Dot(u, v))) : Quat{};
    }

    inline constexpr float kSpin = 2.0f * 6.28318531f;  // radians a second a thrown one tumbles head over heels

    // A thrown one in flight turns a_dt's worth of kSpin round the level axis across its path; a rocket and one
    // lying still keep their turn.
    inline Quat Tumble(const Quat& a_turn, Kind a_kind, const Vec3& a_velocity, float a_dt)
    {
        const Vec3  across{ a_velocity.y, -a_velocity.x, 0.0f };
        const float length = Length(across);
        if (!Falls(a_kind) || Rests(a_velocity) || length < 1e-3f) {
            return a_turn;
        }
        return Mul(AxisAngle(Scale(across, 1.0f / length), kSpin * a_dt), a_turn);
    }

    // The bone that draws a part of its model: the part's own place on it (a_part, relative to the projectile)
    // turned by the projectile's a_turn, relative to where the projectile is.
    inline Protocol::BodyBone PartBone(const Quat& a_turn, const Protocol::BodyBone& a_part)
    {
        const Quat q = Mul(a_turn, { a_part.rotation[0], a_part.rotation[1], a_part.rotation[2], a_part.rotation[3] });
        const Vec3 t = Rotate(a_turn, { a_part.translation[0], a_part.translation[1], a_part.translation[2] });
        return { { q.x, q.y, q.z, q.w }, { t.x, t.y, t.z } };
    }

    // ---- Sticking ----

    // What a satchel stuck to, where it is now: a node's world transform as Skyrim keeps it (a point p on the
    // node is at x * p.x + y * p.y + z * p.z, times scale, plus translate; x, y and z are its axes, its rotation's
    // columns). A static's is where it touched, unturned.
    struct Node
    {
        Vec3  x{ 1.0f, 0.0f, 0.0f };
        Vec3  y{ 0.0f, 1.0f, 0.0f };
        Vec3  z{ 0.0f, 0.0f, 1.0f };
        Vec3  translate;
        float scale = 1.0f;
    };

    // Where it sits on that node, and how it is turned there.
    struct Stuck
    {
        Vec3 offset;
        Quat turn;
    };

    inline Quat Inverse(const Quat& q) { return { -q.x, -q.y, -q.z, q.w }; }

    inline Quat TurnOf(const Node& a_node)
    {
        return Body::FromAxes({ a_node.x.x, a_node.x.y, a_node.x.z }, { a_node.y.x, a_node.y.y, a_node.y.z });
    }

    // A satchel at a_at turned a_turn (both in the world) sticking to a_node.
    inline Stuck StickTo(const Node& a_node, const Vec3& a_at, const Quat& a_turn)
    {
        const Vec3  d = Sub(a_at, a_node.translate);
        const float s = a_node.scale > 1e-4f ? a_node.scale : 1.0f;
        return { { Dot(d, a_node.x) / s, Dot(d, a_node.y) / s, Dot(d, a_node.z) / s }, Mul(Inverse(TurnOf(a_node)), a_turn) };
    }

    // Where a satchel stuck on a_node is in the world, and how it is turned, now that the node has moved.
    inline Vec3 StuckAt(const Node& a_node, const Stuck& a_stuck)
    {
        const Vec3 o = a_stuck.offset;
        return Add(a_node.translate, Scale(Add(Add(Scale(a_node.x, o.x), Scale(a_node.y, o.y)), Scale(a_node.z, o.z)), a_node.scale));
    }

    inline Quat StuckTurn(const Node& a_node, const Stuck& a_stuck) { return Mul(TurnOf(a_node), a_stuck.turn); }

    // ---- Blasts ----

    struct Blast
    {
        float radius = 0.0f;  // Skyrim units: it reaches no one farther
        float damage = 0.0f;  // San Andreas health points in its middle
        float knock = 0.0f;   // Skyrim's knock magnitude (AIProcess::KnockExplosion) in its middle
        bool  opensLocks = false;
    };

    // A rocket and a grenade kill a San Andreas ped (100 health) three times over near the middle and still
    // throw people a few metres out; a Molotov's burst is small, its fire does the rest.
    constexpr Blast BlastOf(Kind a_kind)
    {
        switch (a_kind) {
        case Kind::Rocket: return { 7.0f * kMeter, 300.0f, 14.0f, true };
        case Kind::Grenade:
        case Kind::Satchel: return { 6.0f * kMeter, 300.0f, 12.0f, true };
        case Kind::Molotov: return { 2.5f * kMeter, 40.0f, 0.0f, false };
        case Kind::Vehicle: return { 9.0f * kMeter, 300.0f, 14.0f, false };  // EXPLOSION_CAR reaches further than a rocket
        default: return {};
        }
    }

    inline constexpr float kCore = 0.25f;  // within this share of the radius a blast hits in full
    inline constexpr float kEdge = 0.1f;   // and at its edge with this share

    // San Andreas health points a blast does to someone a_distance from its middle.
    inline float BlastDamage(Kind a_kind, float a_distance)
    {
        const Blast blast = BlastOf(a_kind);
        if (!(blast.radius > 0.0f) || !(a_distance <= blast.radius)) {
            return 0.0f;
        }
        const float t = std::clamp((a_distance / blast.radius - kCore) / (1.0f - kCore), 0.0f, 1.0f);
        return blast.damage * (1.0f - t * (1.0f - kEdge));
    }

    // How hard a blast throws someone a_distance from its middle: 0 for giants, mammoths and dragons, which
    // San Andreas's blasts couldn't move either, and for the farthest third, where it only hurts.
    inline float KnockMagnitude(Kind a_kind, float a_distance, Damage::Class a_class)
    {
        const Blast blast = BlastOf(a_kind);
        if (a_class != Damage::Class::Normal || !(blast.knock > 0.0f) || !(a_distance <= blast.radius * (2.0f / 3.0f))) {
            return 0.0f;
        }
        return blast.knock * (1.0f - 0.75f * a_distance / (blast.radius * (2.0f / 3.0f)));
    }

    // ---- Locks ----

    inline constexpr float kLockRadius = 3.0f * kMeter;

    // Whether a blast a_distance from a lock opens it: a rocket's or a grenade's, close by, and not a lock
    // only a key opens (those keep quests in order).
    inline bool OpensLock(Kind a_kind, float a_distance, bool a_locked, bool a_needsKey)
    {
        return a_locked && !a_needsKey && BlastOf(a_kind).opensLocks && a_distance <= kLockRadius;
    }

    // ---- Fire and gas ----

    // A Molotov's fire catches where the bottle burst and spreads as San Andreas's does: about a metre round at
    // first, three after kFireGrow seconds, then it burns on until kFireSeconds after the burst. Patches: the
    // middle, 4 round it at 1.5 m, then 8 at 2.5 m.
    inline constexpr float kFireSeconds = 30.0f;
    inline constexpr float kFireGrow = 10.0f;
    inline constexpr int   kFirePatchCount = 13;
    inline constexpr float kFireInner = 1.5f * kMeter;
    inline constexpr float kFireOuter = 2.5f * kMeter;

    struct Patch
    {
        Vec3  offset;         // from the burst
        float start = 0.0f;   // seconds after the burst it catches
    };

    inline Patch FirePatch(int a_index)
    {
        if (a_index <= 0 || a_index >= kFirePatchCount) {
            return {};
        }
        const bool  inner = a_index <= 4;
        const int   k = inner ? a_index - 1 : a_index - 5;
        const int   n = inner ? 4 : 8;
        const float angle = 6.28318531f * (static_cast<float>(k) + (inner ? 0.0f : 0.5f)) / static_cast<float>(n);
        const float radius = inner ? kFireInner : kFireOuter;
        const float start = inner ? 3.0f + 0.5f * static_cast<float>(k) : 6.0f + 0.5f * static_cast<float>(k);
        return { { std::cos(angle) * radius, std::sin(angle) * radius, 0.0f }, start };
    }

    inline constexpr float kFirePatchRadius = 0.8f * kMeter;  // who stands this close to a patch burns
    inline constexpr float kFirePerSecond = 20.0f;         // San Andreas health points a second standing in it
    inline constexpr float kBurnSeconds = 6.0f;            // someone it caught burns on this long, as San Andreas's peds do
    inline constexpr float kBurnPerSecond = 15.0f;         // San Andreas health points a second while burning

    inline bool InFire(const Vec3& a_patch, const Vec3& a_feet, float a_radius)
    {
        const float dx = a_feet.x - a_patch.x, dy = a_feet.y - a_patch.y;
        return dx * dx + dy * dy <= (kFirePatchRadius + a_radius) * (kFirePatchRadius + a_radius) && std::fabs(a_feet.z - a_patch.z) <= 1.5f * kMeter;
    }

    inline constexpr float kGasRadius = 4.0f * kMeter;  // tear gas chokes everyone this close to the canister
    inline constexpr float kGasSeconds = 15.0f;         // for this long after it starts
    inline constexpr float kGasPerSecond = 10.0f;       // San Andreas health points a second
    inline constexpr float kGasCough = 2.0f;            // seconds between coughs (a stagger)

    inline bool InGas(const Vec3& a_canister, const Vec3& a_point) { return Length(Sub(a_point, a_canister)) <= kGasRadius; }

    // The flamethrower: a cone from the nozzle, everyone in it burns.
    inline constexpr float kFlameHalfAngle = 12.0f;      // degrees
    inline constexpr float kFlamePerSecond = 80.0f;      // San Andreas health points a second
    inline constexpr float kFlameRangeMin = 2.0f * kMeter;  // a puff whose range San Andreas didn't say reaches this far
    inline constexpr float kFlameRangeMax = 8.0f * kMeter;

    // How far from its axis a cone of a_halfAngle degrees reaches a_along from its nozzle.
    inline float ConeWidth(float a_along, float a_halfAngle)
    {
        return (std::max)(a_along, 0.0f) * std::tan(a_halfAngle * 3.14159265f / 180.0f);
    }

    // Whether a body of a_radius at a_point is in the cone of a_halfAngle degrees from a_origin along a_dir (a
    // unit vector) reaching a_range: within a_halfAngle of its axis, or a_radius of it near the nozzle.
    inline bool InCone(const Vec3& a_origin, const Vec3& a_dir, float a_range, const Vec3& a_point, float a_radius, float a_halfAngle)
    {
        const Vec3  to = Sub(a_point, a_origin);
        const float along = Dot(to, a_dir);
        if (along < -a_radius || along > a_range + a_radius) {
            return false;
        }
        const float across = Length(Sub(to, Scale(a_dir, along)));
        return across <= ConeWidth(along, a_halfAngle) + a_radius;
    }

    inline bool InFlame(const Vec3& a_origin, const Vec3& a_dir, float a_range, const Vec3& a_point, float a_radius)
    {
        return InCone(a_origin, a_dir, a_range, a_point, a_radius, kFlameHalfAngle);
    }

    // ---- The spray can and the fire extinguisher ----

    // San Andreas sends both as flame shots too (kShotFlame, told apart by the shot's weapon type), each a
    // cone from the nozzle like the flamethrower's: the spray can's short and wide, choking and staggering
    // whoever it catches as tear gas does; the extinguisher's long, putting out whoever and whatever burns in it.
    enum class Cone
    {
        None,
        Flame,
        Spray,
        Hose,
    };

    constexpr Cone ConeOf(std::uint8_t a_weaponType)
    {
        switch (a_weaponType) {
        case Weapons::Flamethrower: return Cone::Flame;
        case Weapons::Spraycan: return Cone::Spray;
        case Weapons::Extinguisher: return Cone::Hose;
        default: return Cone::None;
        }
    }

    inline constexpr float kSprayHalfAngle = 20.0f;         // degrees
    inline constexpr float kSprayRangeMin = 1.0f * kMeter;
    inline constexpr float kSprayRangeMax = 3.0f * kMeter;
    inline constexpr float kSprayPerSecond = 30.0f;         // San Andreas health points a second, three times the tear gas's
    inline constexpr float kSprayCough = kGasCough;         // seconds between one actor's coughs (a stagger)
    inline constexpr float kHoseHalfAngle = 15.0f;          // degrees
    inline constexpr float kHoseRangeMin = 2.0f * kMeter;
    inline constexpr float kHoseRangeMax = 8.0f * kMeter;

    constexpr float HalfAngle(Cone a_cone)
    {
        return a_cone == Cone::Spray ? kSprayHalfAngle : a_cone == Cone::Hose ? kHoseHalfAngle : kFlameHalfAngle;
    }

    // How far a puff reaches: San Andreas's weapon range, kept to the cone's own (its minimum when San Andreas
    // didn't say).
    inline float ConeRange(Cone a_cone, float a_range)
    {
        const float range = std::isfinite(a_range) ? a_range : 0.0f;
        switch (a_cone) {
        case Cone::Spray: return std::clamp(range, kSprayRangeMin, kSprayRangeMax);
        case Cone::Hose: return std::clamp(range, kHoseRangeMin, kHoseRangeMax);
        default: return std::clamp(range, kFlameRangeMin, kFlameRangeMax);
        }
    }

    // Whether the extinguisher from a_origin along a_dir reaching a_range puts out a fire on the ground at
    // a_fire (a Molotov's patch): its patch, burning up to knee height, touches the cone.
    inline bool PutsOut(const Vec3& a_origin, const Vec3& a_dir, float a_range, const Vec3& a_fire)
    {
        const Vec3 flames{ a_fire.x, a_fire.y, a_fire.z + 0.3f * kMeter };
        return InCone(a_origin, a_dir, a_range, flames, kFirePatchRadius, kHoseHalfAngle);
    }

    // San Andreas puts its own fires out round points along the extinguisher's axis (CFireManager::
    // ExtinguishPoint): how far apart, and how wide round the point a_along from the nozzle (Skyrim units).
    inline constexpr float kHoseStep = 1.0f * kMeter;
    inline float HoseRadius(float a_along) { return ConeWidth(a_along, kHoseHalfAngle) + kFirePatchRadius; }
    // ---- The heat seeker ----

    // San Andreas's heat seeker locks on to vehicles only. Skyrim's big ones stand in for them: dragons,
    // horses and whoever rides one, giants, mammoths and dwarven centurions (not giant spiders, trolls,
    // sabre cats or bears, which are no bigger than a car).
    inline bool HeatSeekerLocks(std::string_view a_raceEditorId, bool a_dragonKeyword, bool a_mounted)
    {
        if (a_mounted || Damage::ClassOf(a_raceEditorId, a_dragonKeyword) == Damage::Class::Dragon) {
            return true;
        }
        const auto race = Damage::Lower(a_raceEditorId);
        const auto has = [&](const char* a_part) { return race.find(a_part) != std::string::npos; };
        return has("horse") || has("mammoth") || has("centurion") || (has("giant") && !has("spider"));
    }

    inline constexpr std::uint16_t kModelHydra = 520;
    inline constexpr std::uint16_t kModelHunter = 425;

    // Whether Skyrim's lock steers the rockets CJ launches (v23): the heat seeker in his hands on foot, the Hydra's or
    // the Hunter's in a vehicle. The Hydra's own lock (R1 held, CVehicle::GetPlaneWeaponFiringStatus) looks for San
    // Andreas's vehicles, so it never locks and fires plain rockets: Skyrim's lock, aimed while the handbrake key is
    // held, steers them instead. San Andreas's Hunter has no lock at all (CVehicle::SelectPlaneWeapon gives it double
    // rockets, never lock-on ones); ours gets the Hydra's on purpose, so its rockets home once locked.
    constexpr bool SeeksHeat(std::uint8_t a_cjWeapon, std::uint16_t a_vehicleModel)
    {
        return a_vehicleModel ? a_vehicleModel == kModelHydra || a_vehicleModel == kModelHunter : a_cjWeapon == Weapons::RocketLauncherHs;
    }

    inline constexpr float kHsReach = 250.0f;         // metres: San Andreas's aim line is this long
    inline constexpr float kHsLockSeconds = 1.5f;     // a target held this long in sight is locked
    inline constexpr float kHsLosEvery = 1.0f;        // seconds between line of sight tests, as San Andreas's
    inline constexpr float kHsMarkerSize = 1.3f;      // metres: San Andreas's marker round the target
    inline constexpr float kHsTurn = 2.5f;            // radians a second a locked rocket turns at most
    inline constexpr float kHsProximity = 1.0f * kMeter;  // a locked rocket goes off this close to its target's body

    // How good a target at a_at is for a heat seeker aimed from a_origin along a_dir (a unit vector), in
    // metres, as CWeapon::EvaluateTargetForHeatSeekingMissile rates it: lower is better, and -1 when it is
    // out of the cone (its distance off the aim line at least 1/1.2 of its distance) or out of reach. The
    // target it already had rates 4 times better, so the lock doesn't flick between two.
    inline float HsRating(const Vec3& a_origin, const Vec3& a_dir, const Vec3& a_at, bool a_current)
    {
        const Vec3  to = Sub(a_at, a_origin);
        const float distance = Length(to);
        if (!(distance > 1e-3f) || distance > kHsReach) {
            return -1.0f;
        }
        const float along = std::clamp(Dot(to, a_dir), 0.0f, kHsReach);
        const float off = Length(Sub(to, Scale(a_dir, along)));
        if (off * 1.2f >= distance) {
            return -1.0f;
        }
        const float rating = std::sqrt(distance) / 10.0f + off / distance;
        return a_current ? rating * 0.25f : rating;
    }

    // The lock: the same target in sight kHsLockSeconds running; a new target or losing sight of it starts again.
    struct HsLock
    {
        std::uint32_t target = 0;  // the actor's form id, 0 for none
        float         held = 0.0f;

        bool Locked() const { return target != 0 && held >= kHsLockSeconds; }
    };

    inline void Track(HsLock& a_lock, std::uint32_t a_target, bool a_inSight, float a_dt)
    {
        if (a_target == 0 || a_target != a_lock.target || !a_inSight) {
            a_lock.target = a_inSight ? a_target : 0;
            a_lock.held = 0.0f;
            return;
        }
        a_lock.held += a_dt;
    }

    // A locked rocket's velocity after a_dt seconds steering from a_at towards a_target: it turns towards it at
    // kHsTurn at most and keeps its speed.
    inline Vec3 Steer(const Vec3& a_velocity, const Vec3& a_at, const Vec3& a_target, float a_dt)
    {
        const float speed = Length(a_velocity);
        const Vec3  to = Sub(a_target, a_at);
        const float distance = Length(to);
        if (!(speed > 1e-3f) || !(distance > 1e-3f)) {
            return a_velocity;
        }
        const Vec3  from = Scale(a_velocity, 1.0f / speed);
        const Vec3  want = Scale(to, 1.0f / distance);
        const float angle = std::acos(std::clamp(Dot(from, want), -1.0f, 1.0f));
        const float turn = kHsTurn * a_dt;
        if (angle <= turn) {
            return Scale(want, speed);
        }
        // Turn by `turn` in the plane of from and want (straight behind: round the vertical).
        Vec3 side = Sub(want, Scale(from, Dot(from, want)));
        if (Length(side) < 1e-4f) {
            side = Length(Vec3{ -from.y, from.x, 0.0f }) > 1e-4f ? Vec3{ -from.y, from.x, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        }
        side = Scale(side, 1.0f / Length(side));
        return Scale(Add(Scale(from, std::cos(turn)), Scale(side, std::sin(turn))), speed);
    }

    // Whether a locked rocket at a_at is close enough to go off at a body of a_radius whose middle is a_target.
    inline bool NearTarget(const Vec3& a_at, const Vec3& a_target, float a_radius) { return Length(Sub(a_at, a_target)) <= a_radius + kHsProximity; }

    // ---- The Rhino's cannon (v19) ----

    // CAutomobile::TankControl's shot, in the tank's own space (metres, +y ahead, +z up): it fires along its turret's
    // turn (m_fDoomVerticalRotation, radians from straight ahead, counter-clockwise) and lift (m_fDoomHorizontalRotation,
    // radians up), from the CAR_MISC_C dummy at the barrel's tip, or without one from where that dummy stands on
    // San Andreas's own Rhino (TANK_SHOT_DOOM_POS swung round by the turn towards TANK_SHOT_DOOM_DEFAULT_TARGET).
    inline constexpr std::uint8_t kTankCannonNode = 22;  // CAR_MISC_C in San Andreas's car node table (20 is CAR_MISC_A,
                                                         // the turret's base down on the hull, where the shells came out)
    inline constexpr Vec3  kTankShotPivot{ 0.0f, -1.394f, 2.296f };
    inline constexpr Vec3  kTankShotTip{ 0.0f, 2.95f, 2.97f };
    inline constexpr float kTankShotRange = 60.0f;  // metres: TankControl's line test, and so the shell's reach

    // The way the shot goes, a unit vector in the tank's space (TankControl's (sin(-turn), cos(turn), sin(lift)), which
    // it doesn't normalize: its line test only needs the point 60 of them out).
    inline Vec3 TankShotDirection(float a_turn, float a_lift)
    {
        const Vec3  d{ std::sin(-a_turn), std::cos(a_turn), std::sin(a_lift) };
        const float length = Length(d);
        return Scale(d, 1.0f / length);
    }

    // The barrel's tip in the tank's space for a turn, without the dummy.
    inline Vec3 TankShotMuzzle(float a_turn)
    {
        const Vec3  reach = Sub(kTankShotTip, kTankShotPivot);
        const float c = std::cos(a_turn), s = std::sin(a_turn);
        return { kTankShotPivot.x + c * reach.x - s * reach.y, kTankShotPivot.y + c * reach.y + s * reach.x, kTankShotPivot.z + reach.z - 1.0f };
    }
}
