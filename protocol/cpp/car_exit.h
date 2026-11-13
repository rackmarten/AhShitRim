// Getting out of a vehicle as San Andreas does it. Its leave-car task plays the get-out animation and, every frame,
// its get-out task puts CJ where that animation needs him (CTaskSimpleCarGetOut::SetPedPosition, which lines him up
// with the door through CTaskUtilityLineUpPedWithCar), so his body goes out through the door and ends beside it, where
// CTaskSimpleCarSetPedOut leaves him. Until 0.2 the ASI seated him again after each frame's vehicle work
// (SetPedPositionInCar), which undid that: he played the animation in the seat, stood there once it ended, then was put
// outside. Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

#include "pose.h"

namespace AhShitRim::CarExit
{
    using Pose::Vec3;

    // Whether the ASI seats CJ in his vehicle again after its own work on the vehicle this frame. Not while San
    // Andreas takes him out of it (its leave-car task, or the bust's drag out of the driver's door): its tasks
    // place him themselves then.
    constexpr bool Reseat(bool a_leaving, bool a_draggedOut = false) { return !a_leaving && !a_draggedOut; }

    // One frame of CJ's body while he gets out: its middle (the average of his skinned vertices, what Skyrim draws)
    // in the vehicle's frame (X right, Y forward, Z up from its centre), metres, and whether San Andreas still has
    // him in the vehicle.
    struct Sample
    {
        Vec3 at;
        bool inVehicle = true;
    };

    // What a get-out did, from its samples (one per San Andreas frame, from the key press until he stands).
    struct Trace
    {
        std::size_t samples = 0;
        float       maxStep = 0.0f;    // the farthest his middle moved in one frame (a jump), metres
        float       maxBack = 0.0f;    // the most he came back towards the vehicle's middle from where he had got to, short of where he ends (into the car again)
        float       startOut = 0.0f;   // how far out to the side he was at the start (the seat)
        float       endOut = 0.0f;     // and at the end (beside the door)
        float       outAtRelease = 0.0f;  // and on the first frame San Andreas no longer has him in the vehicle
    };

    // Out to the side: a get-out goes through a side door (a bike's to the left), so the distance from the
    // vehicle's long middle line.
    inline float Out(const Vec3& a_at) { return std::fabs(a_at.x); }

    inline Trace Check(std::span<const Sample> a_samples)
    {
        Trace t;
        t.samples = a_samples.size();
        if (a_samples.empty()) {
            return t;
        }
        t.startOut = Out(a_samples.front().at);
        t.endOut = Out(a_samples.back().at);
        t.outAtRelease = t.endOut;
        float farthest = t.startOut;
        bool  released = false;
        for (std::size_t i = 0; i < a_samples.size(); i++) {
            const auto& s = a_samples[i];
            if (i > 0) {
                const auto& p = a_samples[i - 1].at;
                t.maxStep = (std::max)(t.maxStep, std::sqrt((s.at.x - p.x) * (s.at.x - p.x) + (s.at.y - p.y) * (s.at.y - p.y) + (s.at.z - p.z) * (s.at.z - p.z)));
            }
            // San Andreas's own animation swings him past where he ends (pushing the door shut) and back: only coming
            // back short of where he ends counts.
            farthest = (std::max)(farthest, Out(s.at));
            t.maxBack = (std::max)(t.maxBack, (std::min)(farthest, t.endOut) - Out(s.at));
            if (!released && !s.inVehicle) {
                released = true;
                t.outAtRelease = Out(s.at);
            }
        }
        return t;
    }

    inline constexpr float kMaxStep = 0.3f;   // metres in one frame: the get-out animation moves him about 0.05
    inline constexpr float kMaxBack = 0.2f;   // metres back towards the middle
    inline constexpr float kMinOut = 0.6f;    // metres farther out at the end than in the seat
    inline constexpr float kReleaseShare = 0.7f;  // of that way out he is already when San Andreas lets go of him

    // A get-out as San Andreas plays it: no jump, no way back into the vehicle, out beside it at the end, and most of
    // the way out already when the leave-car task sets him outside (not stood in the seat until then).
    inline bool Smooth(const Trace& a_trace)
    {
        const float way = a_trace.endOut - a_trace.startOut;
        return a_trace.samples >= 2 && a_trace.maxStep <= kMaxStep && a_trace.maxBack <= kMaxBack && way >= kMinOut &&
               a_trace.outAtRelease - a_trace.startOut >= kReleaseShare * way;
    }

    // A drive: one sample per San Andreas frame while CJ drives, in the vehicle's frame. He stays in his seat, so his
    // middle stays where it sat on the first frame: steering, braking and a bike's lean move it a few centimetres. A
    // frame his seating was skipped (Reseat false while he isn't getting out) leaves him behind the moving vehicle.
    struct Ride
    {
        std::size_t samples = 0;
        float       maxOff = 0.0f;   // the farthest his middle got from where it sat on the first frame, metres
        float       lastOff = 0.0f;  // and on the last frame
        float       maxStep = 0.0f;  // the farthest it moved in the vehicle's frame in one frame
        std::size_t worstAt = 0;     // the frame of maxOff
    };

    inline float Distance(const Vec3& a_a, const Vec3& a_b)
    {
        return std::sqrt((a_a.x - a_b.x) * (a_a.x - a_b.x) + (a_a.y - a_b.y) * (a_a.y - a_b.y) + (a_a.z - a_b.z) * (a_a.z - a_b.z));
    }

    inline Ride CheckRide(std::span<const Sample> a_samples)
    {
        Ride r;
        r.samples = a_samples.size();
        for (std::size_t i = 0; i < a_samples.size(); i++) {
            r.lastOff = Distance(a_samples[i].at, a_samples.front().at);
            if (r.lastOff > r.maxOff) {
                r.maxOff = r.lastOff;
                r.worstAt = i;
            }
            if (i > 0) {
                r.maxStep = (std::max)(r.maxStep, Distance(a_samples[i].at, a_samples[i - 1].at));
            }
        }
        return r;
    }

    inline constexpr float kRideMaxOff = 0.15f;   // metres off the seat in a car: his steering and braking, not a frame left behind
    inline constexpr float kRideMaxStep = 0.15f;  // metres in one frame: a frame left behind at 20 m/s is 0.67

    // In his seat all the way. On a bike he leans into the turns, half a metre and more off where he sat, but smoothly:
    // there only a jump counts.
    inline bool Seated(const Ride& a_ride, bool a_bike = false)
    {
        return a_ride.samples >= 2 && a_ride.maxStep <= kRideMaxStep && (a_bike || a_ride.maxOff <= kRideMaxOff);
    }
}
