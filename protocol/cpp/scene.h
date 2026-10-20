// Scenes while CJ has the player (v9), the game-free half: who drives the player this frame (CJ, or
// Skyrim in a scripted scene or a seat), what CJ's body does while he follows Skyrim's player, how
// he turns to the one he talks to, and when the player let out a shout. Header-only and free of game
// types, so the protocol tests cover it; the SKSE plugin (skse/src/player/PlayerSync.cpp) and the
// San Andreas ASI (sa/src/scene.cpp) feed it.
//
// Skyrim headings are radians, 0 = north, clockwise; San Andreas's turn the other way (see
// Pose::SanAndreasHeading).

#pragma once

#include <cmath>
#include <cstdint>

#include "../ahshitrim_protocol.h"
#include "pose.h"

namespace AhShitRim::Scene
{
    // ---- Skyrim side ----

    // Skyrim's SIT_SLEEP_STATE (ActorState): sitting down, seated, standing up; then the same for a bed.
    inline constexpr std::uint8_t kSitNormal = 0;
    inline constexpr std::uint8_t kSitWantToStand = 4;
    inline constexpr std::uint8_t kSitWantToWake = 8;

    enum class Control : std::uint8_t
    {
        Cj,        // CJ moves and Skyrim's player follows him
        Scripted,  // Skyrim moves the player (a scene, controls taken away, a bed): CJ follows it
        Seated,    // the player sits (a chair, a bench, a carriage): CJ follows it, sitting
    };

    struct PlayerFrame
    {
        std::uint8_t sitSleepState = kSitNormal;
        bool         movementEnabled = true;  // ControlMap: the player's movement controls are on
        bool         aiDriven = false;        // a package walks the player (SetPlayerAIDriven)
    };

    constexpr Control Classify(const PlayerFrame& a_frame)
    {
        if (a_frame.sitSleepState > kSitNormal && a_frame.sitSleepState <= kSitWantToStand) {
            return Control::Seated;
        }
        if (a_frame.sitSleepState > kSitWantToStand || !a_frame.movementEnabled || a_frame.aiDriven) {
            return Control::Scripted;
        }
        return Control::Cj;
    }

    constexpr const char* NameOf(Control a_control)
    {
        switch (a_control) {
        case Control::Scripted: return "scripted";
        case Control::Seated: return "seated";
        default: return "CJ";
        }
    }

    // Puts what Skyrim does with its player into the input: in a scene, where CJ follows it (a_offset
    // is the player's feet relative to the anchor, a_heading its heading); otherwise nothing.
    inline void Follow(Protocol::InputState& a_input, Control a_control, const float (&a_offset)[3], float a_heading)
    {
        if (a_control == Control::Cj) {
            return;
        }
        a_input.flags |= Protocol::kInputScripted | (a_control == Control::Seated ? Protocol::kInputSeated : 0);
        for (int i = 0; i < 3; ++i) {
            a_input.sceneOffset[i] = a_offset[i];
        }
        a_input.sceneHeading = a_heading;
    }

    // A menu has the input and someone talks to the player: CJ stands still facing them. Not in a
    // scene, where CJ goes where Skyrim has the player.
    inline void Face(Protocol::InputState& a_input, float a_px, float a_py, float a_tx, float a_ty)
    {
        if ((a_input.flags & Protocol::kInputScripted) != 0 || (std::abs(a_tx - a_px) < 1.0f && std::abs(a_ty - a_py) < 1.0f)) {
            return;
        }
        a_input.flags |= Protocol::kInputFace;
        a_input.sceneHeading = Pose::HeadingOf(a_tx - a_px, a_ty - a_py);
    }

    // The player let out a shout: Skyrim starts its voice cooldown when the shout fires.
    constexpr bool ShoutReleased(float a_recoveryBefore, float a_recoveryNow)
    {
        return a_recoveryBefore <= 0.0f && a_recoveryNow > 0.0f;
    }

    // ---- San Andreas side ----

    // San Andreas heading a_current turned toward a_target by at most a_maxStep (radians), the short
    // way round; in (-pi, pi].
    inline float TurnToward(float a_current, float a_target, float a_maxStep)
    {
        float d = a_target - a_current;
        while (d <= -Pose::kPi) d += 2.0f * Pose::kPi;
        while (d > Pose::kPi) d -= 2.0f * Pose::kPi;
        float h = a_current + (d > a_maxStep ? a_maxStep : d < -a_maxStep ? -a_maxStep : d);
        while (h <= -Pose::kPi) h += 2.0f * Pose::kPi;
        while (h > Pose::kPi) h -= 2.0f * Pose::kPi;
        return h;
    }

    // What CJ's body does while he follows Skyrim's player.
    enum class Motion : std::uint8_t
    {
        Idle,
        Walk,
        Run,
        Seated,
    };

    constexpr const char* NameOf(Motion a_motion)
    {
        switch (a_motion) {
        case Motion::Walk: return "walk";
        case Motion::Run: return "run";
        case Motion::Seated: return "seated";
        default: return "idle";
        }
    }

    // Walks or runs CJ by how fast the player he follows moves. Skyrim's frames and San Andreas's
    // don't line up, so the speed is smoothed and each gait has a margin before it changes back.
    class MotionTracker
    {
    public:
        static constexpr float kWalkFrom = 0.5f;  // m/s
        static constexpr float kIdleBelow = 0.25f;
        static constexpr float kRunFrom = 3.5f;
        static constexpr float kWalkBelow = 2.8f;

        void Reset() { *this = {}; }

        // a_position: where the player he follows is, metres; a_seconds since the last call.
        Motion Update(bool a_seated, const Pose::Vec3& a_position, float a_seconds)
        {
            const float moved = std::hypot(a_position.x - _last.x, a_position.y - _last.y);
            if (_have && a_seconds > 0.0f && moved < kJump) {  // a jump is a load or a move, not a gait
                _speed += (moved / a_seconds - _speed) * (a_seconds / (a_seconds + kSmoothing));
            }
            _last = a_position;
            _have = true;
            if (a_seated) {
                _gait = Motion::Idle;
                return Motion::Seated;
            }
            switch (_gait) {
            case Motion::Run:
                _gait = _speed < kIdleBelow ? Motion::Idle : _speed < kWalkBelow ? Motion::Walk : Motion::Run;
                break;
            case Motion::Walk:
                _gait = _speed < kIdleBelow ? Motion::Idle : _speed >= kRunFrom ? Motion::Run : Motion::Walk;
                break;
            default:
                _gait = _speed >= kRunFrom ? Motion::Run : _speed >= kWalkFrom ? Motion::Walk : Motion::Idle;
                break;
            }
            return _gait;
        }

        float Speed() const { return _speed; }

    private:
        static constexpr float kSmoothing = 0.2f;  // s
        static constexpr float kJump = 5.0f;       // m

        bool      _have = false;
        Pose::Vec3 _last;
        float     _speed = 0.0f;
        Motion    _gait = Motion::Idle;
    };
}
