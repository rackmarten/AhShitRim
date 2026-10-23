// San Andreas's parachute on foot (protocol v19): skydiving, opening the chute, floating down and landing.
// Header-only and free of game types, so the protocol tests cover it; the ASI feeds it every frame
// (sa/src/cj_gear.cpp).
//
// San Andreas does all of this in a mission script (its parachute script), which never runs here: the game
// itself only knows the parachute as a weapon and has the animations (parachute.ifp: FALL_skyDive for CJ,
// PARA_open, PARA_float and the rest, each with an _O twin for the canopy). So the rules are AhShitRim's,
// after the script's: CJ holding the parachute who falls fast with enough air under him skydives; the fire
// key opens the chute, which brakes him to a slow glide; W and S dive and flare, A and D turn; touching
// down he lands, and the parachute is used up.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace AhShitRim::Parachute
{
    enum class Phase : std::uint8_t
    {
        Off,
        Freefall,  // skydiving, chute shut
        Opening,   // PARA_open: the canopy comes out and brakes him
        Floating,  // under the canopy
        Landing,   // PARA_Land: on the ground, the canopy collapsing
    };

    // ---- San Andreas metres and seconds ----

    inline constexpr float kStartFall = 5.0f;     // falling this fast with the parachute in his hands, he skydives...
    inline constexpr float kStartHeight = 15.0f;  // ... with at least this much air under him
    inline constexpr float kGravity = 20.0f;      // San Andreas's (0.008 m a step squared at 50 steps a second)
    inline constexpr float kFreefallFall = 40.0f;  // terminal speed, flat
    inline constexpr float kDiveFall = 55.0f;      // W: head down
    inline constexpr float kSlowFall = 32.0f;      // S: spread out
    inline constexpr float kFreefallForward = 4.0f;
    inline constexpr float kDiveForward = 14.0f;
    inline constexpr float kSlowForward = 1.0f;
    inline constexpr float kFreefallTurn = 1.5f;   // radians a second, A and D

    inline constexpr float kOpenSeconds = 1.1f;    // PARA_open
    inline constexpr float kFloatFall = 4.5f;      // under the canopy
    inline constexpr float kFlareFall = 2.5f;      // S: flared
    inline constexpr float kSteepFall = 7.0f;      // W: pulled down
    inline constexpr float kFloatForward = 7.0f;
    inline constexpr float kFlareForward = 2.5f;
    inline constexpr float kSteepForward = 11.0f;
    inline constexpr float kFloatTurn = 0.9f;
    inline constexpr float kBrake = 4.0f;          // per second: how fast speeds go over to what they should be

    inline constexpr float kLandSeconds = 1.3f;    // PARA_Land, then he walks off

    // What CJ is doing this frame, as San Andreas sees it.
    struct Around
    {
        bool  holdsChute = false;  // he carries the parachute (it comes into his hands when the skydive starts, as the script takes it out)
        bool  standing = false;    // San Andreas has him on something (CPed::bIsStanding)
        bool  free = true;         // on foot and his own: not in a vehicle, on the jetpack, in a scene or Wasted
        float fall = 0.0f;         // metres a second downwards (negative going up)
        float height = 1e9f;       // metres from his feet down to the ground, huge when nothing is below
    };

    struct Keys
    {
        float steer = 0.0f;  // -1 left (A) .. 1 right (D)
        float pitch = 0.0f;  // -1 back (S) .. 1 forward (W)
        bool  open = false;  // the fire key went down this frame
    };

    struct State
    {
        Phase phase = Phase::Off;
        float time = 0.0f;     // seconds in this phase
        float forward = 0.0f;  // metres a second along his heading
        float fall = 0.0f;     // metres a second down
        float turn = 0.0f;     // radians a second, positive to the right (clockwise seen from above)
        bool  usedUp = false;  // landed: the parachute is taken from him (as the script's REMOVE_WEAPON_FROM_CHAR)
    };

    inline float Toward(float a_value, float a_target, float a_rate, float a_dt)
    {
        return a_value + (a_target - a_value) * (std::min)(1.0f, a_rate * a_dt);
    }

    inline void Enter(State& a_state, Phase a_phase)
    {
        a_state.phase = a_phase;
        a_state.time = 0.0f;
    }

    // One frame of a_dt seconds.
    inline State Step(State a_state, const Around& a_around, const Keys& a_keys, float a_dt)
    {
        a_state.time += a_dt;
        a_state.usedUp = false;
        const float steer = std::clamp(a_keys.steer, -1.0f, 1.0f);
        const float pitch = std::clamp(a_keys.pitch, -1.0f, 1.0f);
        if (!a_around.free) {
            a_state = {};
            return a_state;
        }
        switch (a_state.phase) {
        case Phase::Off:
            if (a_around.holdsChute && !a_around.standing && a_around.fall >= kStartFall && a_around.height >= kStartHeight) {
                Enter(a_state, Phase::Freefall);
                a_state.fall = a_around.fall;
                a_state.forward = 0.0f;
            }
            break;
        case Phase::Freefall:
            if (a_around.standing) {
                a_state = {};  // hit the ground with it shut: San Andreas's own fall does the rest
                break;
            }
            if (a_keys.open) {
                Enter(a_state, Phase::Opening);
                break;
            }
            {
                const float terminal = pitch > 0.0f ? kFreefallFall + (kDiveFall - kFreefallFall) * pitch : kFreefallFall + (kFreefallFall - kSlowFall) * pitch;
                const float ahead = pitch > 0.0f ? kFreefallForward + (kDiveForward - kFreefallForward) * pitch
                                                 : kFreefallForward + (kFreefallForward - kSlowForward) * pitch;
                // Gravity up to the terminal speed of how he lies; faster than that (out of a dive) the air brakes him.
                a_state.fall = a_state.fall > terminal ? Toward(a_state.fall, terminal, kBrake, a_dt) : (std::min)(a_state.fall + kGravity * a_dt, terminal);
                a_state.forward = Toward(a_state.forward, ahead, kBrake, a_dt);
                a_state.turn = steer * kFreefallTurn;
            }
            break;
        case Phase::Opening:
        case Phase::Floating:
            if (a_around.standing || a_around.height < 0.2f) {
                Enter(a_state, Phase::Landing);
                a_state.forward = 0.0f;
                a_state.fall = 0.0f;
                a_state.turn = 0.0f;
                break;
            }
            if (a_state.phase == Phase::Opening && a_state.time >= kOpenSeconds) {
                Enter(a_state, Phase::Floating);
            }
            {
                // Opening brakes him hard from the freefall over its second, then the canopy flies.
                const bool  opening = a_state.phase == Phase::Opening;
                const float fall = opening ? kFloatFall : pitch > 0.0f ? kFloatFall + (kSteepFall - kFloatFall) * pitch : kFloatFall + (kFloatFall - kFlareFall) * pitch;
                const float ahead = opening ? kFloatForward * 0.5f
                                    : pitch > 0.0f ? kFloatForward + (kSteepForward - kFloatForward) * pitch
                                                   : kFloatForward + (kFloatForward - kFlareForward) * pitch;
                a_state.fall = Toward(a_state.fall, fall, opening ? 2.0f * kBrake : kBrake, a_dt);
                a_state.forward = Toward(a_state.forward, ahead, kBrake, a_dt);
                a_state.turn = opening ? 0.0f : steer * kFloatTurn;
            }
            break;
        case Phase::Landing:
            a_state.forward = a_state.fall = a_state.turn = 0.0f;
            if (a_state.time >= kLandSeconds) {
                a_state = {};
                a_state.usedUp = true;
            }
            break;
        }
        return a_state;
    }

    // Whether AhShitRim moves him (sets his speed every frame) rather than San Andreas's own fall.
    constexpr bool Flying(Phase a_phase) { return a_phase == Phase::Freefall || a_phase == Phase::Opening || a_phase == Phase::Floating; }

    // Whether the canopy is out (the gear slot draws it).
    constexpr bool CanopyOut(Phase a_phase) { return a_phase == Phase::Opening || a_phase == Phase::Floating || a_phase == Phase::Landing; }

    // CJ's animation in parachute.ifp for the phase and keys; the canopy plays the one with "_O" added.
    constexpr std::string_view AnimOf(Phase a_phase, const Keys& a_keys)
    {
        switch (a_phase) {
        case Phase::Freefall:
            if (a_keys.pitch > 0.5f) return "FALL_SkyDive_Accel";
            if (a_keys.steer < -0.5f) return "FALL_SkyDive_L";
            if (a_keys.steer > 0.5f) return "FALL_SkyDive_R";
            return "FALL_skyDive";
        case Phase::Opening: return "PARA_open";
        case Phase::Floating:
            if (a_keys.pitch < -0.5f) return "PARA_decel";
            if (a_keys.steer < -0.5f) return "PARA_steerL";
            if (a_keys.steer > 0.5f) return "PARA_steerR";
            return "PARA_float";
        case Phase::Landing: return "PARA_Land";
        default: return {};
        }
    }

    // Whether CJ's animation loops (the falls and the glides) or plays once and holds (opening, landing).
    constexpr bool Loops(Phase a_phase) { return a_phase == Phase::Freefall || a_phase == Phase::Floating; }

    // ---- Holding CJ's pose ----
    //
    // San Andreas's own in-air reaction keeps blending its partial fall over him (FALL_FALL, FALL_GLIDE: the flailing),
    // so while he skydives the parachute's animation is held at full blend and the rest at none, but for his face's:
    // parachute.ifp keys 26 of his 32 bones, not his jaw, lips and brows. San Andreas sums a bone's rotations weighted
    // by blend and normalizes them, so a bone nothing animates came out as a grin; his own idle, held at kIdleBlend,
    // alone drives those and gives a still face, and next to the parachute's full blend it barely moves the rest.

    inline constexpr float kIdleBlend = 0.02f;

    enum class Hold : std::uint8_t
    {
        Full,  // the parachute's animation
        Idle,  // his idle, at kIdleBlend
        Keep,  // left as San Andreas has it: his face's animations
        None,  // everything else, at no blend
    };

    // What one of CJ's animations is held at, by its group and id (eAnimGroup, eAnimID) and flags.
    constexpr Hold HoldOf(bool a_parachute, int a_group, int a_id, bool a_partial, int a_idleGroup)
    {
        if (a_parachute) {
            return Hold::Full;
        }
        if (a_partial && a_group == 0 && a_id >= 154 && a_id <= 161) {  // ANIM_ID_FACSURP .. ANIM_ID_FACGUM
            return Hold::Keep;
        }
        if (!a_partial && a_group == a_idleGroup && a_id == 3) {  // ANIM_ID_IDLE of his movement group
            return Hold::Idle;
        }
        return Hold::None;
    }
}
