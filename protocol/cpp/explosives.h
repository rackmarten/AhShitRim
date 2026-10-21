// CJ's explosives and fire in Skyrim (v15), the game-free half: what each of San Andreas's projectiles
// does (a rocket and a Molotov go off where they touch, a grenade and tear gas bounce until their fuse
// runs out), their flight under San Andreas's gravity, the bounce, how hard a blast hits and throws people
// by how far they stand, which locks it opens, the tear gas cloud and the flamethrower's cone. Header-only
// and free of game types, so the protocol tests cover it.
//
// Units are Skyrim's (70 to the metre) and seconds; damage is in San Andreas's health points, which
// Damage::ToSkyrim turns into Skyrim's as it does a bullet's.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

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
        Rocket,   // the rocket launcher's and (unguided until #863) the heat seeker's
        Grenade,
        TearGas,
        Molotov,
    };

    constexpr Kind KindOf(std::uint8_t a_projectileType)
    {
        switch (a_projectileType) {
        case Weapons::Grenade: return Kind::Grenade;
        case Weapons::TearGas: return Kind::TearGas;
        case Weapons::Molotov: return Kind::Molotov;
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

    // When a San Andreas projectile goes off by itself if it sent no fuse: a grenade's 2 s (gaProjectileInfo's
    // destroy time), tear gas starts its cloud as soon, a rocket gives up out of reach.
    constexpr float FuseSeconds(Kind a_kind, std::uint32_t a_fuseMs)
    {
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
        case Kind::Grenade: return { 6.0f * kMeter, 300.0f, 12.0f, true };
        case Kind::Molotov: return { 2.5f * kMeter, 40.0f, 0.0f, false };
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

    inline constexpr float kFireSpread = 1.0f * kMeter;  // a Molotov's fire: one patch where it burst, more this far round it
    inline constexpr int   kFirePatches = 4;              // round the first

    // Where the a_index-th patch of a Molotov's fire goes (0 the middle, then round it), relative to its burst.
    inline Vec3 FirePatch(int a_index)
    {
        if (a_index <= 0) {
            return {};
        }
        const float angle = 6.28318531f * static_cast<float>(a_index - 1) / static_cast<float>(kFirePatches);
        return { std::cos(angle) * kFireSpread, std::sin(angle) * kFireSpread, 0.0f };
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

    // Whether a body of a_radius at a_point is in the cone from a_origin along a_dir (a unit vector) reaching
    // a_range: within kFlameHalfAngle of its axis, or a_radius of it near the nozzle.
    inline bool InFlame(const Vec3& a_origin, const Vec3& a_dir, float a_range, const Vec3& a_point, float a_radius)
    {
        const Vec3  to = Sub(a_point, a_origin);
        const float along = Dot(to, a_dir);
        if (along < -a_radius || along > a_range + a_radius) {
            return false;
        }
        const float across = Length(Sub(to, Scale(a_dir, along)));
        const float wide = (std::max)(along, 0.0f) * std::tan(kFlameHalfAngle * 3.14159265f / 180.0f) + a_radius;
        return across <= wide;
    }
}
