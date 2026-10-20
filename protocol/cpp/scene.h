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

    // Where a furniture marker is in the world: its offset in the furniture's model space on the
    // furniture's 3D (world = rotate * (scale * offset) + translate, rotate as NiMatrix3's
    // entry[row][column]). Seated, Skyrim's player has its root there; its own position can stay
    // where it got on (a carriage).
    inline Pose::Vec3 MarkerInWorld(const float (&a_rotate)[3][3], const Pose::Vec3& a_translate, float a_scale, const Pose::Vec3& a_offset)
    {
        const float o[3] = { a_offset.x * a_scale, a_offset.y * a_scale, a_offset.z * a_scale };
        return { a_translate.x + a_rotate[0][0] * o[0] + a_rotate[0][1] * o[1] + a_rotate[0][2] * o[2],
            a_translate.y + a_rotate[1][0] * o[0] + a_rotate[1][1] * o[1] + a_rotate[1][2] * o[2],
            a_translate.z + a_rotate[2][0] * o[0] + a_rotate[2][1] * o[1] + a_rotate[2][2] * o[2] };
    }

    // ---- San Andreas side ----

    // San Andreas's sitting animation lowers CJ backwards from where he stands, onto a chair behind
    // him. Seated, his position is moved so his pelvis is on the seat (a furniture marker is on the
    // seat's surface) instead: by his pelvis's offset from his position, smoothed (the idle sways),
    // metres.
    class SeatMatch
    {
    public:
        static constexpr float kMaxOffset = 1.5f;      // m: further is a bad read, kept out
        static constexpr float kPelvisOverSeat = 0.12f;  // m: CJ's pelvis joint over what he sits on

        void Reset() { *this = {}; }

        void Update(bool a_valid, float a_dx, float a_dy, float a_dz, float a_seconds)
        {
            if (!a_valid || !std::isfinite(a_dx) || !std::isfinite(a_dy) || !std::isfinite(a_dz) ||
                std::hypot(a_dx, a_dy, a_dz) > kMaxOffset) {
                return;
            }
            const float k = _have ? a_seconds / (a_seconds + kSmoothing) : 1.0f;
            _x += (a_dx - _x) * k;
            _y += (a_dy - _y) * k;
            _z += (a_dz - _z) * k;
            _have = true;
        }

        bool  Have() const { return _have; }
        float X() const { return _x; }
        float Y() const { return _y; }
        float Z() const { return _z; }

        // CJ's position that puts his pelvis on a seat at a_seat (metres), once his pelvis is known;
        // a_seat itself (his feet's height there plus a_feetToPosition) before.
        Pose::Vec3 Place(const Pose::Vec3& a_seat, float a_feetToPosition) const
        {
            if (!_have) {
                return { a_seat.x, a_seat.y, a_seat.z + a_feetToPosition };
            }
            return { a_seat.x - _x, a_seat.y - _y, a_seat.z + kPelvisOverSeat - _z };
        }

    private:
        static constexpr float kSmoothing = 0.15f;  // s

        bool  _have = false;
        float _x = 0.0f;
        float _y = 0.0f;
        float _z = 0.0f;
    };

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
