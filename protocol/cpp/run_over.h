// CJ's vehicle against Skyrim's actors (protocol v23). Skyrim sends the actors near the vehicle as capsules
// (ActorProxy, sized by race); San Andreas sweeps the vehicle's collision box against them every frame, pushes
// the vehicle back by what it hit (a person a little, a giant or a mammoth like a wall) and sends each hard
// hit back as an ImpactEvent, which Skyrim turns into damage by speed and a throw. Header-only and free of
// game types, so the protocol tests cover it.
//
// Skyrim's half works in its units (70 to the metre) relative to the handover's anchor; San Andreas's in
// metres in its own world. Both put X east, Y north and Z up.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "damage.h"
#include "pose.h"

namespace AhShitRim::RunOver
{
    using Body::Add;
    using Body::Cross;
    using Body::Dot;
    using Body::Scale;
    using Body::Sub;
    using Pose::Vec3;

    inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

    // ---- Skyrim side: who is near, and how big ----

    // How an actor is built, for its capsule and its weight.
    enum class Build : std::uint8_t
    {
        Person,   // a humanoid race (ActorTypeNPC): standing, about 1.8 m
        Small,    // chickens, hares, foxes, skeevers, mudcrabs, dogs
        Beast,    // anything else: wolves, bears, trolls, sabre cats, draugr, spiders
        Horse,    // lying along its heading
        Giant,    // heavy: stops a car
        Mammoth,  // heavy, lying along its heading
        Dragon,   // heavy, lying along its heading (a landed one)
    };

    // The build of an actor by its race's editor id and two of the race's keywords.
    inline Build BuildOf(std::string_view a_raceEditorId, bool a_npcKeyword, bool a_dragonKeyword)
    {
        if (Damage::ClassOf(a_raceEditorId, a_dragonKeyword) == Damage::Class::Dragon) {
            return Build::Dragon;
        }
        const auto race = Damage::Lower(a_raceEditorId);
        const auto has = [&](const char* a_part) { return race.find(a_part) != std::string::npos; };
        if (has("mammoth")) {
            return Build::Mammoth;
        }
        if (has("giant") && !has("spider") && !has("frostbite")) {  // GiantRace, not the giant frostbite spiders
            return Build::Giant;
        }
        if (has("horse")) {
            return Build::Horse;
        }
        if (a_npcKeyword) {
            return Build::Person;
        }
        for (const char* animal : { "chicken", "hen", "hare", "rabbit", "fox", "skeever", "mudcrab", "dog", "goat", "slaughterfish" }) {
            if (has(animal)) {
                return Build::Small;
            }
        }
        return Build::Beast;
    }

    constexpr bool Heavy(Build a_build) { return a_build == Build::Giant || a_build == Build::Mammoth || a_build == Build::Dragon; }

    // An actor's capsule in its own frame, Skyrim units: upright from its feet, or lying along its heading.
    struct Shape
    {
        float radius = 0.0f;
        float length = 0.0f;      // the segment between the two end spheres' centres
        float centre = 0.0f;      // a lying one's segment sits this high over the feet (an upright one starts at radius)
        bool  lying = false;
        float mass = 0.0f;        // kilograms
        bool  heavy = false;
    };

    inline constexpr float kMeter = Pose::kUnitsPerMeter;

    // Its capsule at a_scale (the reference's scale) and a_height (Skyrim's height for it, units; 0 when unknown).
    inline Shape ShapeOf(Build a_build, float a_scale, float a_height)
    {
        const float scale = std::isfinite(a_scale) && a_scale > 0.05f ? a_scale : 1.0f;
        const auto  upright = [&](float a_radius, float a_defaultHeight, float a_mass) {
            const float height = std::isfinite(a_height) && a_height > 1.0f ? a_height : a_defaultHeight * scale;
            const float radius = (std::min)(a_radius * scale, 0.5f * height);
            return Shape{ radius, (std::max)(0.0f, height - 2.0f * radius), 0.0f, false, a_mass * scale * scale * scale, false };
        };
        const auto lying = [&](float a_radius, float a_length, float a_centre, float a_mass, bool a_heavy) {
            return Shape{ a_radius * scale, a_length * scale, a_centre * scale, true, a_mass * scale * scale * scale, a_heavy };
        };
        switch (a_build) {
        case Build::Person: return upright(0.3f * kMeter, 1.8f * kMeter, 80.0f);
        case Build::Small: return upright(0.25f * kMeter, 0.5f * kMeter, 15.0f);
        case Build::Beast: return upright(0.45f * kMeter, 1.4f * kMeter, 200.0f);
        case Build::Horse: return lying(0.55f * kMeter, 1.6f * kMeter, 1.15f * kMeter, 500.0f, false);
        case Build::Giant: {
            Shape giant = upright(0.7f * kMeter, 4.5f * kMeter, 2500.0f);
            giant.heavy = true;
            return giant;
        }
        case Build::Mammoth: return lying(1.6f * kMeter, 3.0f * kMeter, 2.2f * kMeter, 6000.0f, true);
        case Build::Dragon: return lying(1.5f * kMeter, 6.0f * kMeter, 1.5f * kMeter, 5000.0f, true);
        default: return {};
        }
    }

    struct Capsule
    {
        Vec3  a;
        Vec3  b;
        float radius = 0.0f;
    };

    // The shape stood at a_feet facing a_heading (Skyrim radians, 0 north, clockwise).
    inline Capsule Place(const Shape& a_shape, const Vec3& a_feet, float a_heading)
    {
        if (!a_shape.lying) {
            const Vec3 a = Add(a_feet, { 0.0f, 0.0f, a_shape.radius });
            return { a, Add(a, { 0.0f, 0.0f, a_shape.length }), a_shape.radius };
        }
        const Vec3 forward{ std::sin(a_heading), std::cos(a_heading), 0.0f };
        const Vec3 middle = Add(a_feet, { 0.0f, 0.0f, a_shape.centre });
        const Vec3 half = Scale(forward, 0.5f * a_shape.length);
        return { Sub(middle, half), Add(middle, half), a_shape.radius };
    }

    inline constexpr float kProxyRange = 40.0f * kMeter;  // actors farther from CJ's vehicle aren't sent
    inline constexpr float kFollowerRange = 30.0f * kMeter;

    struct Candidate
    {
        std::uint32_t id = 0;
        float         distance = 0.0f;  // from CJ's vehicle, Skyrim units
    };

    // The nearest candidates within a_range, nearest first, at most a_max of them: the indices into a_all.
    inline std::vector<std::size_t> Nearest(std::span<const Candidate> a_all, std::size_t a_max = Protocol::kProxyMax, float a_range = kProxyRange)
    {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < a_all.size(); i++) {
            if (std::isfinite(a_all[i].distance) && a_all[i].distance <= a_range) {
                out.push_back(i);
            }
        }
        std::stable_sort(out.begin(), out.end(), [&](std::size_t a, std::size_t b) { return a_all[a].distance < a_all[b].distance; });
        if (out.size() > a_max) {
            out.resize(a_max);
        }
        return out;
    }

    // ---- San Andreas side: the vehicle against a capsule ----

    // The vehicle's collision bounds in its own frame (its col model's box), metres.
    struct Box
    {
        Vec3 min;
        Vec3 max;
    };

    // Where the vehicle is and how it is turned (its matrix's rows), metres.
    struct Frame
    {
        Vec3 pos;
        Vec3 right{ 1.0f, 0.0f, 0.0f };
        Vec3 forward{ 0.0f, 1.0f, 0.0f };
        Vec3 up{ 0.0f, 0.0f, 1.0f };
    };

    struct Touch
    {
        Vec3  point;   // on the vehicle's box, world
        Vec3  normal;  // from the vehicle towards the actor, a unit vector
        float depth = 0.0f;
        float along = 0.0f;  // how far into the frame's sweep it touched first, 0..1
    };

    inline constexpr float kSweepStep = 0.25f;  // metres the vehicle moves between two tests of a sweep
    inline constexpr int   kSweepMax = 48;      // tests a sweep makes at most (a Hydra's 87 m/s in a frame)

    // Whether a sphere touches the box of a vehicle at a_frame.
    inline std::optional<Touch> SphereTouch(const Box& a_box, const Frame& a_frame, const Vec3& a_centre, float a_radius)
    {
        const Vec3 d = Sub(a_centre, a_frame.pos);
        const Vec3 local{ Dot(d, a_frame.right), Dot(d, a_frame.forward), Dot(d, a_frame.up) };
        const Vec3 closest{ std::clamp(local.x, a_box.min.x, a_box.max.x), std::clamp(local.y, a_box.min.y, a_box.max.y),
            std::clamp(local.z, a_box.min.z, a_box.max.z) };
        const Vec3  off = Sub(local, closest);
        const float distance = Length(off);
        if (!(distance < a_radius)) {
            return std::nullopt;
        }
        Vec3 normalLocal;
        if (distance > 1e-4f) {
            normalLocal = Scale(off, 1.0f / distance);
        } else {
            // Its centre is inside the box: out through the nearest side (front, back, left or right).
            const float toRight = a_box.max.x - local.x, toLeft = local.x - a_box.min.x;
            const float toFront = a_box.max.y - local.y, toBack = local.y - a_box.min.y;
            const float least = (std::min)((std::min)(toRight, toLeft), (std::min)(toFront, toBack));
            normalLocal = least == toFront ? Vec3{ 0.0f, 1.0f, 0.0f } : least == toBack ? Vec3{ 0.0f, -1.0f, 0.0f } : least == toRight ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ -1.0f, 0.0f, 0.0f };
        }
        const auto toWorld = [&](const Vec3& v) { return Add(Add(Scale(a_frame.right, v.x), Scale(a_frame.forward, v.y)), Scale(a_frame.up, v.z)); };
        Touch touch;
        touch.point = Add(a_frame.pos, toWorld(closest));
        touch.normal = Body::Normalize(toWorld(normalLocal), a_frame.forward);
        touch.depth = a_radius - distance;
        return touch;
    }

    // The spheres a capsule is tested as: along its segment, at most a radius apart.
    inline std::vector<Vec3> SpheresOf(const Capsule& a_capsule)
    {
        const Vec3  span = Sub(a_capsule.b, a_capsule.a);
        const float length = Length(span);
        const int   steps = a_capsule.radius > 0.0f ? (std::clamp)(static_cast<int>(std::ceil(length / a_capsule.radius)), 1, 16) : 1;
        std::vector<Vec3> out;
        for (int i = 0; i <= steps; i++) {
            out.push_back(Add(a_capsule.a, Scale(span, static_cast<float>(i) / static_cast<float>(steps))));
        }
        return out;
    }

    // Where the vehicle, moving from a_from to a_to this frame (its turn as a_to's), first touches the capsule:
    // the frame's motion is swept in steps, so a fast car can't pass through a person between two frames.
    inline std::optional<Touch> Sweep(const Box& a_box, const Frame& a_from, const Frame& a_to, const Capsule& a_capsule)
    {
        const auto  spheres = SpheresOf(a_capsule);
        const float moved = Length(Sub(a_to.pos, a_from.pos));
        const int   steps = std::isfinite(moved) ? (std::clamp)(static_cast<int>(std::ceil(moved / kSweepStep)), 1, kSweepMax) : 1;
        for (int k = 0; k <= steps; k++) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            Frame       at = a_to;
            at.pos = Add(a_from.pos, Scale(Sub(a_to.pos, a_from.pos), t));
            std::optional<Touch> deepest;
            for (const auto& centre : spheres) {
                if (auto touch = SphereTouch(a_box, at, centre, a_capsule.radius); touch && (!deepest || touch->depth > deepest->depth)) {
                    deepest = touch;
                }
            }
            if (deepest) {
                deepest->along = t;
                return deepest;
            }
        }
        return std::nullopt;
    }

    // ---- What a hit does ----

    inline constexpr std::uint8_t kRammedByCar = 49;  // San Andreas's WEAPON_RAMMEDBYCAR: what Skyrim counts a hit as in the damage table

    inline constexpr float kMinImpact = 2.5f;          // m/s: closing slower than this is a nudge, nobody is hurt
    inline constexpr float kDamageFrom = 3.0f;         // m/s: damage grows with the square of the speed over this
    inline constexpr float kDamagePerSpeed2 = 0.9f;    // San Andreas health points per (m/s)^2 for a 1.5 t car
    inline constexpr float kDamageMax = 1000.0f;
    inline constexpr float kCarMass = 1500.0f;          // kilograms: the car the damage is set for
    inline constexpr float kThrowBounce = 0.3f;         // a person goes off this much faster than the car pushes them
    inline constexpr float kThrowLift = 0.25f;          // and up by this share of the closing speed
    inline constexpr float kHeavyBounce = 0.2f;         // a car comes off a mammoth with this share of its speed into it
    inline constexpr float kNormalLift = 0.25f;         // a hit's normal keeps this share of its up and down: cars don't climb people

    // San Andreas health points a hit at a_closing m/s does (a ped has 100), by the vehicle's weight: a bandit
    // dies to a car at 15 to 20 m/s, a bike or a bicycle hurts less, a tank more.
    inline float ImpactDamage(float a_closing, float a_vehicleMass)
    {
        if (!std::isfinite(a_closing) || a_closing < kMinImpact) {
            return 0.0f;
        }
        const float over = (std::max)(0.0f, a_closing - kDamageFrom);
        const float mass = std::isfinite(a_vehicleMass) && a_vehicleMass > 0.0f ? a_vehicleMass : kCarMass;
        const float weight = std::clamp(std::sqrt(mass / kCarMass), 0.4f, 2.0f);
        return (std::min)(kDamageMax, kDamagePerSpeed2 * over * over * weight);
    }

    inline constexpr float kHeavyCrashPerSpeed = 8.0f;  // vehicle health (1000 new) a heavy hit takes per m/s over kDamageFrom

    // What hitting a giant, a mammoth or a dragon does to the vehicle's own health: about what a wall does in San
    // Andreas, 136 at 20 m/s.
    inline float HeavyCrashDamage(float a_closing)
    {
        if (!std::isfinite(a_closing) || a_closing < kMinImpact) {
            return 0.0f;
        }
        return kHeavyCrashPerSpeed * (std::max)(0.0f, a_closing - kDamageFrom);
    }

    struct Response
    {
        float closing = 0.0f;  // m/s along the normal, before the hit
        Vec3  vehicleChange;   // m/s, the vehicle's velocity change
        Vec3  actorVelocity;   // m/s, how fast the actor goes off (unchanged for a heavy one)
    };

    // A hit between the vehicle and an actor along a_normal (from the vehicle to the actor): an actor that
    // isn't heavy takes the vehicle's momentum along the normal and flies off, slowing the vehicle by its share
    // (a person barely slows a car); a heavy one doesn't move, and the vehicle stops and comes back off it a little.
    inline Response Respond(float a_vehicleMass, const Vec3& a_vehicleVelocity, const Vec3& a_actorVelocity, float a_actorMass, bool a_heavy, const Vec3& a_normal)
    {
        Response   out;
        const Vec3 n = Body::Normalize({ a_normal.x, a_normal.y, a_normal.z * kNormalLift }, { 0.0f, 1.0f, 0.0f });
        out.actorVelocity = a_actorVelocity;
        out.closing = Dot(Sub(a_vehicleVelocity, a_actorVelocity), n);
        if (!std::isfinite(out.closing) || out.closing <= 0.0f) {
            out.closing = 0.0f;
            return out;
        }
        if (a_heavy) {
            out.vehicleChange = Scale(n, -(1.0f + kHeavyBounce) * out.closing);
            return out;
        }
        const float vehicle = std::isfinite(a_vehicleMass) && a_vehicleMass > 0.0f ? a_vehicleMass : kCarMass;
        const float actor = std::isfinite(a_actorMass) && a_actorMass > 0.0f ? a_actorMass : 80.0f;
        out.vehicleChange = Scale(n, -out.closing * actor / (vehicle + actor));
        const float off = out.closing * (1.0f + kThrowBounce) * vehicle / (vehicle + actor);
        out.actorVelocity = Add(Add(a_actorVelocity, Scale(n, off)), { 0.0f, 0.0f, kThrowLift * out.closing });
        return out;
    }

    // Skyrim's knock for a throw at a_speed m/s (AIProcess::KnockExplosion's magnitude: a rocket's middle is 14).
    inline float KnockOf(float a_speed)
    {
        if (!std::isfinite(a_speed) || a_speed < kMinImpact) {
            return 0.0f;
        }
        return std::clamp(0.8f * a_speed, 2.0f, 20.0f);
    }

    // One actor isn't hit again within kHitGap of its last hit, while the vehicle is still in it.
    inline constexpr double kHitGap = 1.0;

    class HitMemory
    {
    public:
        // Whether a hit on a_id at a_now (seconds) counts, and if so remembers it.
        bool Lands(std::uint32_t a_id, double a_now)
        {
            for (auto& entry : _entries) {
                if (entry.id == a_id && entry.used) {
                    if (a_now - entry.at < kHitGap && a_now >= entry.at) {
                        return false;
                    }
                    entry.at = a_now;
                    return true;
                }
            }
            auto& slot = *std::min_element(_entries.begin(), _entries.end(), [](const Entry& a, const Entry& b) { return a.used != b.used ? !a.used : a.at < b.at; });
            slot = { a_id, a_now, true };
            return true;
        }

        void Clear() { _entries = {}; }

    private:
        struct Entry
        {
            std::uint32_t id = 0;
            double        at = 0.0;
            bool          used = false;
        };
        std::array<Entry, Protocol::kProxyMax * 2> _entries{};
    };

    // CJ says a line when his vehicle hits someone, at most one every kSpeechGap seconds.
    inline constexpr double kSpeechGap = 4.0;

    class SpeechGap
    {
    public:
        bool Ready(double a_now)
        {
            if (_said && a_now - _at < kSpeechGap && a_now >= _at) {
                return false;
            }
            _said = true;
            _at = a_now;
            return true;
        }

    private:
        bool   _said = false;
        double _at = 0.0;
    };
}
