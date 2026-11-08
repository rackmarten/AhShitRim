// The Rhino's tracks on rough ground. Game-free, so the protocol tests cover it; the San Andreas ASI applies it
// once a frame after San Andreas's physics step (sa/src/vehicles.cpp, TrackGrip).
//
// San Andreas drives its tank as a car: it steers the front wheels (35 degrees; the rear ones counter-steer on the
// handbrake, HB_REARWHEEL_STEER) and only the four corner wheels grip (CAutomobile::ProcessCarWheelPair, then
// CVehicle::ProcessWheel for each). The eight track lines between them (SetupSuspensionLines, four a side between its
// front and rear wheel) only hold it up: ProcessSuspension springs them, nothing grips through them. San Andreas's
// ground is smooth, so the corners stay down. On Skyrim's rocks the hull rides on the middle of its tracks with its
// corners in the air, nothing grips, and it slides and spins as on ice: the slide reads as strafing.
//
// So a corner off the ground past San Andreas's own grace (m_fWheelCounts: a wheel keeps gripping for 4 steps after it
// leaves the ground) grips through the track line on its side nearest it that touches, by San Andreas's own wheel
// rule: its adhesion as ProcessCarWheelPair works it out for the player's vehicle, and the sideways slide at that line
// taken off as ProcessWheel takes it off a wheel, shared among the wheels on the ground, at most its adhesion a step.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace AhShitRim::Tracks
{
    inline constexpr int kModelRhino = 432;
    inline constexpr int kWheels = 4;  // San Andreas's order: front left, rear left, front right, rear right
    inline constexpr int kLines = 8;   // the track lines, San Andreas's col lines 4 to 11: left 0..3 then right 4..7, front to back

    // The track lines nearest a corner wheel, nearest first: its side's two at its end.
    constexpr std::array<int, 2> LinesFor(int a_wheel)
    {
        const int side = a_wheel < 2 ? 0 : 4;
        const bool front = a_wheel == 0 || a_wheel == 2;
        return front ? std::array<int, 2>{ side, side + 1 } : std::array<int, 2>{ side + 3, side + 2 };
    }

    // The track line that grips for a_wheel this step (0..7), or -1: the wheel is off the ground (compression 1, San
    // Andreas's "completely relaxed") and its grace is over (a_count, m_fWheelCounts, run out), and one of its two
    // lines touches (compression under 1); the more compressed one wins.
    inline int GripLine(int a_wheel, float a_compression, float a_count, const std::array<float, kLines>& a_lines)
    {
        if (a_wheel < 0 || a_wheel >= kWheels || a_compression < 1.0f || a_count > 0.0f) {
            return -1;
        }
        int best = -1;
        for (const int line : LinesFor(a_wheel)) {
            if (a_lines[line] < 1.0f && (best < 0 || a_lines[line] < a_lines[best])) {
                best = line;
            }
        }
        return best;
    }

    // A corner's adhesion a step, as ProcessCarWheelPair gives the player's vehicle on a surface of a_limit
    // (g_surfaceInfos.GetAdhesiveLimit, wheel against the ground): the handling's traction multiplier / 250, times
    // 0.25 (at San Andreas's usual velocity frequency of 1), the traction bias of its axle (2 x the bias at the front,
    // 2 - 2 x it at the back), and how hard it is pressed down, bias x the suspension's force level x 4 x how far the
    // spring is in (1 - compression), at most 2. a_step: CTimer::ms_fTimeStep.
    inline float Adhesion(float a_limit, float a_tractionMultiplier, float a_tractionBias, float a_suspensionBias, float a_forceLevel, bool a_front,
        float a_compression, float a_step)
    {
        const float traction = a_tractionMultiplier / 250.0f * 0.25f;
        const float axleTraction = a_front ? 2.0f * a_tractionBias : 2.0f - 2.0f * a_tractionBias;
        const float axleSuspension = a_front ? 2.0f * a_suspensionBias : 2.0f - 2.0f * a_suspensionBias;
        const float pressed = (std::min)(axleSuspension * a_forceLevel * 4.0f * (1.0f - std::clamp(a_compression, 0.0f, 1.0f)), 2.0f);
        return (std::max)(0.0f, a_limit * traction * axleTraction * pressed * a_step);
    }

    // The sideways speed change (metres a step, along the wheel's right) that gripping takes off at a contact sliding
    // sideways at a_slide: ProcessWheel's -slide / wheels on the ground, at most a_adhesion either way.
    inline float SideChange(float a_slide, int a_wheelsOnGround, float a_adhesion)
    {
        if (a_slide == 0.0f || a_adhesion <= 0.0f) {
            return 0.0f;
        }
        const float share = -a_slide / static_cast<float>((std::max)(a_wheelsOnGround, 1));
        return std::clamp(share, -a_adhesion, a_adhesion);
    }

    // ---- Turning about its middle ----
    //
    // Steering only its front wheels, San Andreas's Rhino turns about its rear axle like a car, so its middle swings
    // into the turn: 3.7 m/s sideways at 15 m/s (protocol/tools/fake_vehicles, AHSHITRIM_ONLY=handling), which a tank
    // with straight tracks shows as strafing. Its handbrake mode already turns it about its middle (the rear wheels
    // counter-steer as far as the front ones), only tighter. So without the handbrake the Rhino's rear wheels counter-steer
    // as its front ones do, both turned less, to San Andreas's own turning circle: tan(front) + tan(rear) = tan(lock), so
    // each tan(lock) / 2. The handbrake keeps San Andreas's full lock and its tighter turn.

    // The steering lock (degrees) the handling gets for the next step.
    inline float SteeringLock(float a_lock, bool a_handbrake)
    {
        constexpr float kDegrees = 57.29578f;
        return a_handbrake ? a_lock : std::atan(std::tan(a_lock / kDegrees) * 0.5f) * kDegrees;
    }

    // What m_f2ndSteerAngle (the rear wheels' angle, radians) gets after a step whose front wheels were at a_front, so
    // San Andreas's next step steers the rear ones to -a_front: without the handbrake ProcessControlInputs eases it
    // to 0.9 of itself first; with it, San Andreas works it out itself (towards -front, as its handbrake mode does).
    inline float RearSteer(float a_front, bool a_handbrake, float a_current)
    {
        return a_handbrake ? a_current : -a_front / 0.9f;
    }

    // ---- Heavy over rocks ----
    //
    // San Andreas throws a vehicle off what its hull hits by its speed alone, whatever it weighs (CPhysical's collision
    // response scales the push with the mass), and its world is smooth, so a hull rarely hits the ground. On Skyrim's
    // rocks a heavy vehicle's hull does all the time (the Rhino's springs reach down only 10 cm), and the Rhino hopped
    // over them like a car. So a step in which the hull of a vehicle heavier than kHeavyFrom hit the ground (its
    // damage record names a building) keeps only kHeavyFrom / its mass of what that step added to its speed up and
    // to its pitching and rolling: the ground pushes it no harder than a kHeavyFrom vehicle. Its speed along the
    // ground, and anything taking speed off, are San Andreas's.
    inline constexpr float kHeavyFrom = 8000.0f;  // kg: the Rhino (25 t) keeps 0.32, the Dumper (20 t) 0.4, a Packer all

    inline float HeavyKeep(float a_mass)
    {
        return a_mass > kHeavyFrom ? kHeavyFrom / a_mass : 1.0f;
    }

    // a_before: its speed up (or a turn rate) at the end of the last step; a_after: now. A gain away from zero is cut
    // to a_keep of itself; the rest is San Andreas's.
    inline float Settle(float a_before, float a_after, float a_keep)
    {
        if (a_keep >= 1.0f) {
            return a_after;
        }
        if (a_after > 0.0f && a_after > a_before) {
            const float from = (std::max)(a_before, 0.0f);
            return from + (a_after - from) * a_keep;
        }
        if (a_after < 0.0f && a_after < a_before) {
            const float from = (std::min)(a_before, 0.0f);
            return from + (a_after - from) * a_keep;
        }
        return a_after;
    }

    // Only upwards for the speed: a hull hit never slows a fall or a drop less than San Andreas does.
    inline float SettleUp(float a_before, float a_after, float a_keep)
    {
        return a_after > 0.0f ? Settle(a_before, a_after, a_keep) : a_after;
    }
}
