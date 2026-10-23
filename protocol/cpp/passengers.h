// Followers riding with CJ (protocol v23). San Andreas sends the passenger seats of each vehicle in its slot
// (BodyState.seats: San Andreas's passenger order, relative to the vehicle as its parts are); when CJ gets in,
// Skyrim seats the player's followers in them, nearest first, draws each seated where its seat is every frame
// and stands them up beside their doors when he gets out. Header-only and free of game types, so the protocol
// tests cover it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "body.h"
#include "pose.h"

namespace AhShitRim::Passengers
{
    using Pose::Vec3;

    inline constexpr float kRange = 30.0f * Pose::kUnitsPerMeter;  // followers farther from CJ than this stay behind
    inline constexpr float kSeatDrop = 18.0f;                      // Skyrim units from a seat (where the hips go) down to a seated actor's feet
    inline constexpr float kStepOut = 1.0f * Pose::kUnitsPerMeter;  // a passenger gets out this far past its seat, to the side its door is on
    inline constexpr float kStepUp = 0.5f * Pose::kUnitsPerMeter;   // and this high, to land on the ground under it

    struct Follower
    {
        std::uint32_t id = 0;
        float         distance = 0.0f;  // from CJ, Skyrim units
        int           seat = -1;        // the seat it has now, -1 for none
    };

    // The seat for each follower (-1 for none) in a vehicle with a_seatCount passenger seats: a follower in a
    // seat keeps it, the rest take the free ones in San Andreas's order (the front one, then the back left and
    // right), nearest first, as long as they are within a_range.
    inline std::vector<int> Assign(std::span<const Follower> a_followers, std::uint32_t a_seatCount, float a_range = kRange)
    {
        const std::uint32_t seats = (std::min)(a_seatCount, Protocol::kSeatMax);
        std::vector<int>    out(a_followers.size(), -1);
        bool                taken[Protocol::kSeatMax]{};
        for (std::size_t i = 0; i < a_followers.size(); i++) {
            const int seat = a_followers[i].seat;
            if (seat >= 0 && static_cast<std::uint32_t>(seat) < seats && !taken[seat]) {
                out[i] = seat;
                taken[seat] = true;
            }
        }
        std::vector<std::size_t> waiting;
        for (std::size_t i = 0; i < a_followers.size(); i++) {
            if (out[i] < 0 && std::isfinite(a_followers[i].distance) && a_followers[i].distance <= a_range) {
                waiting.push_back(i);
            }
        }
        std::stable_sort(waiting.begin(), waiting.end(), [&](std::size_t a, std::size_t b) { return a_followers[a].distance < a_followers[b].distance; });
        std::uint32_t next = 0;
        for (const std::size_t i : waiting) {
            while (next < seats && taken[next]) {
                next++;
            }
            if (next >= seats) {
                break;
            }
            out[i] = static_cast<int>(next);
            taken[next] = true;
        }
        return out;
    }

    inline Body::Quat QuatOf(const Protocol::BodyBone& a_bone) { return { a_bone.rotation[0], a_bone.rotation[1], a_bone.rotation[2], a_bone.rotation[3] }; }

    // a_v turned by a unit quaternion, and back.
    inline Vec3 Rotate(const Body::Quat& q, const Vec3& v)
    {
        const Vec3 u{ q.x, q.y, q.z };
        const Vec3 t = Body::Scale(Body::Cross(u, v), 2.0f);
        return Body::Add(Body::Add(v, Body::Scale(t, q.w)), Body::Cross(u, t));
    }

    inline Vec3 Unrotate(const Body::Quat& q, const Vec3& v) { return Rotate({ -q.x, -q.y, -q.z, q.w }, v); }

    // The way a seat faces as a Skyrim heading (radians, 0 north, clockwise): its frame's forward, Y.
    inline float HeadingOf(const Protocol::BodyBone& a_seat)
    {
        const Vec3 forward = Rotate(QuatOf(a_seat), { 0.0f, 1.0f, 0.0f });
        return Pose::HeadingOf(forward.x, forward.y);
    }

    // Where a seated passenger's feet go, relative to the vehicle's feet: under the seat by a_drop along the
    // seat's own down, so it leans with a bike.
    inline Vec3 SeatedFeet(const Protocol::BodyBone& a_seat, float a_drop = kSeatDrop)
    {
        const Vec3 down = Rotate(QuatOf(a_seat), { 0.0f, 0.0f, -a_drop });
        return Body::Add({ a_seat.translation[0], a_seat.translation[1], a_seat.translation[2] }, down);
    }

    // Where a passenger stands when it gets out, relative to the vehicle's feet: out of the side its seat is on
    // (a bike's pillion, in the middle, to the left as San Andreas gets off a bike), kStepOut past the seat and
    // kStepUp over it, upright.
    inline Vec3 ExitAt(const Protocol::BodyBone& a_seat)
    {
        const auto q = QuatOf(a_seat);
        const Vec3 seat{ a_seat.translation[0], a_seat.translation[1], a_seat.translation[2] };
        // The seat's offset across the vehicle: the vehicle's centre is the bone's origin, so the seat's side is
        // its translation along the seat's own right.
        const Vec3  right = Rotate(q, { 1.0f, 0.0f, 0.0f });
        const float across = Body::Dot(seat, right);
        const float side = across > 0.05f * Pose::kUnitsPerMeter ? 1.0f : -1.0f;
        const Vec3  flat = Body::Normalize({ right.x, right.y, 0.0f }, { 1.0f, 0.0f, 0.0f });
        return Body::Add(seat, Body::Add(Body::Scale(flat, side * kStepOut), { 0.0f, 0.0f, kStepUp }));
    }
}
