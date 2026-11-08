// The gamepad (docs/DESIGN.md, Controller): Skyrim's own gamepad events turned into CJ's actions. Header-only and
// free of game types, so the protocol tests cover it; the SKSE plugin feeds it from its input sink and asks it once a
// frame what to send.
//
// Skyrim gives the pad's buttons as button events whose id codes are XInput's button masks (the triggers as 0x9 and
// 0xA, with their pull as the value), and the sticks as thumbstick events, -1..1 with up positive. One layout, the
// "modern" one: the triggers aim and fire on foot and drive in a vehicle, San Andreas's console layout wherever that
// doesn't fight it, and Skyrim's own buttons for what stays Skyrim's (its Shout on RB, its tween menu on B, its
// journal on Start, which Skyrim's own handlers keep). The Pad column of the README's keys table is this map.
//
// On foot: the left stick moves (a light push walks), L3 crouches, the right stick looks, R3 insults, A sprints, X
// jumps, Y gets into a vehicle, LB activates (as E), LT aims, RT fires, the d-pad's left and right step through the
// weapons. In a vehicle: RT accelerates, LT brakes, the left stick steers and leans, A fires its weapon, LB its second
// one, RB is the handbrake, X looks behind, Y gets out, L3 is the horn, R3 the sub-mission key (held, the right stick
// bounces hydraulics), the d-pad's up and down tune the radio and its left and right look aside (an aircraft's rudder);
// in the Hydra the right stick's up and down move the nozzles. Back tapped is the camera key (V), held a second it
// hands the player over (F10), both ways.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include "input.h"

namespace AhShitRim::Pad
{
    // Skyrim's gamepad id codes (BSWin32GamepadDevice::Key): XInput's button masks, and the triggers.
    inline constexpr std::uint32_t kUp = 0x0001;
    inline constexpr std::uint32_t kDown = 0x0002;
    inline constexpr std::uint32_t kLeft = 0x0004;
    inline constexpr std::uint32_t kRight = 0x0008;
    inline constexpr std::uint32_t kStart = 0x0010;
    inline constexpr std::uint32_t kBack = 0x0020;
    inline constexpr std::uint32_t kLeftThumb = 0x0040;   // L3
    inline constexpr std::uint32_t kRightThumb = 0x0080;  // R3
    inline constexpr std::uint32_t kLeftShoulder = 0x0100;
    inline constexpr std::uint32_t kRightShoulder = 0x0200;
    inline constexpr std::uint32_t kA = 0x1000;
    inline constexpr std::uint32_t kB = 0x2000;
    inline constexpr std::uint32_t kX = 0x4000;
    inline constexpr std::uint32_t kY = 0x8000;
    inline constexpr std::uint32_t kLeftTrigger = 0x0009;
    inline constexpr std::uint32_t kRightTrigger = 0x000A;

    inline constexpr float kTriggerHeld = 0.3f;   // a trigger pulled this far holds its action
    inline constexpr float kTriggerPress = 0.5f;  // and this far is a press in a cheat sequence
    inline constexpr float kHoldGate = 0.3f;      // B and RB held this long do Skyrim's thing (the tween menu, the Shout) while CJ has the player
    inline constexpr float kTurretHeld = 0.5f;    // the right stick this far is a turret key (hydraulics, the Hydra's nozzles)
    inline constexpr float kBackHold = 1.0f;      // Back held this long hands the player over (F10)
    inline constexpr float kCameraPulse = 0.12f;  // a tap of Back holds the camera key this long, so San Andreas's frame sees it

    // Where CJ is, for the map.
    enum class Context : std::uint8_t
    {
        OnFoot,
        Vehicle,  // a car, bike, bicycle, quad or aircraft
        Hydra,    // the Hydra: as Vehicle, and the right stick's up and down are its nozzles
    };

    // How the right stick turns San Andreas's camera, as mouse counts ([Input] in AhShitRim.ini).
    struct Look
    {
        float lookSpeed = 1400.0f;  // counts a second with the stick all the way over (PadLookSpeed)
        float vertical = 0.6f;      // the up and down share of that
        float aimShare = 0.55f;     // while LT aims over the shoulder (PadAimSpeed)
        float scopeShare = 0.35f;   // through a scope or a first-person sight
        bool  invertY = false;      // PadInvertY
        float deadZone = 0.15f;
    };

    // A stick axis past the dead zone, rescaled to 0..1 and squared, with its sign: fine control near the middle,
    // full speed at the edge.
    inline float Curve(float a_value, float a_deadZone)
    {
        const float v = a_value < -1.0f ? -1.0f : a_value > 1.0f ? 1.0f : a_value;
        const float m = std::fabs(v);
        if (m <= a_deadZone || a_deadZone >= 1.0f) {
            return 0.0f;
        }
        const float t = (m - a_deadZone) / (1.0f - a_deadZone);
        return (v < 0.0f ? -1.0f : 1.0f) * t * t;
    }

    // What the pad does this frame.
    struct Frame
    {
        Input::PadActions actions;  // held: the axes and buttons, merged with the keyboard's
        double            mouseX = 0.0;  // counts to add to the mouse totals, DirectInput's directions (y down)
        double            mouseY = 0.0;
        int               nextSteps = 0;  // d-pad steps for the wheel's counters: weapons on foot, stations in a vehicle
        int               prevSteps = 0;
        bool              activate = false;  // LB pressed on foot: E
        bool              insult = false;    // R3 pressed on foot: X
        bool              handover = false;  // Back held a second: F10
        bool              waitTap = false;   // Back tapped while Skyrim has the player: its wait menu, held back from Skyrim to time the hold
    };

    // Keeps the pad's state from Skyrim's events and turns it into a Frame once a frame.
    class Mapper
    {
    public:
        // A button event: a_id a Skyrim gamepad id code, a_value its value (0 or 1, a trigger's pull). Returns whether it
        // was a press: a button going down, a trigger pulled past half (for the cheat sequences, PadCheats).
        bool SetButton(std::uint32_t a_id, float a_value)
        {
            if (a_id == kLeftTrigger || a_id == kRightTrigger) {
                float&     pull = a_id == kLeftTrigger ? _lt : _rt;
                const bool press = pull < kTriggerPress && a_value >= kTriggerPress;
                pull = a_value;
                return press;
            }
            const bool down = a_value > 0.0f;
            const bool press = down && !(_held & a_id);
            if (down && !(_held & a_id)) {
                _pressed |= a_id;
                if (a_id == kBack) {
                    _backSeconds = 0.0f;
                    _backFired = false;
                }
            } else if (!down && (_held & a_id) && a_id == kBack) {
                _backReleased = true;
            }
            _held = down ? (_held | a_id) : (_held & ~a_id);
            return press;
        }

        // A thumbstick event.
        void SetStick(bool a_right, float a_x, float a_y)
        {
            (a_right ? _rx : _lx) = a_x;
            (a_right ? _ry : _ly) = a_y;
        }

        // Everything let go (the pad unplugged, the game losing focus).
        void ReleaseAll()
        {
            _held = _pressed = 0;
            _lt = _rt = _lx = _ly = _rx = _ry = 0.0f;
            _backReleased = false;
            _backFired = true;
            _cameraLeft = 0.0f;
        }

        bool  Held(std::uint32_t a_id) const { return (_held & a_id) != 0; }
        float LeftTrigger() const { return _lt; }
        float RightX() const { return _rx; }
        float RightY() const { return _ry; }
        float RightTrigger() const { return _rt; }
        bool  Active() const { return _held != 0 || _lt > 0.0f || _rt > 0.0f || _lx != 0.0f || _ly != 0.0f || _rx != 0.0f || _ry != 0.0f; }

        // Once a frame. a_seconds: real time since the last call (Back's hold counts while a menu pauses the game too).
        // a_owned: CJ has the player. a_live: CJ takes input (no Skyrim menu, shop or scene has it); presses made
        // while not live are dropped. a_scoped: CJ looks through a sight (a scope, a first-person sight), for the look
        // speed; LT aiming over the shoulder slows it too.
        Frame Update(float a_seconds, Context a_context, bool a_owned, bool a_live, bool a_scoped, const Look& a_look = {})
        {
            Frame f;
            const std::uint32_t pressed = std::exchange(_pressed, 0u);
            // Back: a tap is the camera key (or Skyrim's wait menu), a second's hold the handover, in either game.
            if (Held(kBack) && !_backFired) {
                _backSeconds += a_seconds;
                if (_backSeconds >= kBackHold) {
                    _backFired = true;
                    f.handover = true;
                }
            }
            if (std::exchange(_backReleased, false) && !_backFired) {
                _backFired = true;
                if (a_owned) {
                    _cameraLeft = kCameraPulse;
                } else {
                    f.waitTap = true;
                }
            }
            if (_cameraLeft > 0.0f) {
                f.actions.buttons |= Protocol::kButtonCameraView;
                _cameraLeft -= a_seconds;
            }
            if (!a_owned || !a_live) {
                return f;
            }
            auto& a = f.actions;
            const bool lt = _lt >= kTriggerHeld;
            const bool rt = _rt >= kTriggerHeld;
            const auto bit = [&](bool a_on, std::uint32_t a_button) {
                if (a_on) {
                    a.buttons |= a_button;
                }
            };
            const bool vehicle = a_context != Context::OnFoot;
            bool       lookX = true, lookY = true;  // whether the right stick turns the camera
            if (!vehicle) {
                a.moveX = Clamp(_lx);
                a.moveY = Clamp(_ly);
                bit(Held(kA), Protocol::kButtonSprint);
                bit(Held(kX), Protocol::kButtonJump);
                bit(Held(kLeftThumb), Protocol::kButtonCrouch);
                bit(Held(kY), Protocol::kButtonEnterExit);
                bit(lt, Protocol::kButtonAim);
                bit(rt, Protocol::kButtonFire);
                f.nextSteps = (pressed & kRight) ? 1 : 0;
                f.prevSteps = (pressed & kLeft) ? 1 : 0;
                f.activate = (pressed & kLeftShoulder) != 0;
                f.insult = (pressed & kRightThumb) != 0;
            } else {
                a.moveX = Clamp(_lx);  // steering, analog
                // v40: the triggers and the stick's lean go as they are (InputState.throttle, brake, lean); the buttons stay
                // for Skyrim's side (the burnout, the shop).
                a.throttle = _rt;
                a.brake = _lt;
                a.lean = Clamp(_ly);
                bit(rt, Protocol::kButtonAccelerate);
                bit(lt, Protocol::kButtonBrake);
                bit(Held(kA), Protocol::kButtonVehicleFire);
                bit(Held(kLeftShoulder), Protocol::kButtonVehicleFireAlt);
                bit(Held(kRightShoulder), Protocol::kButtonHandbrake);
                bit(Held(kX), Protocol::kButtonLookBehind);
                bit(Held(kY), Protocol::kButtonEnterExit);
                bit(Held(kLeftThumb), Protocol::kButtonHorn);
                bit(Held(kRightThumb), Protocol::kButtonHydraulics);
                bit(Held(kLeft), Protocol::kButtonLookLeft);
                bit(Held(kRight), Protocol::kButtonLookRight);
                f.nextSteps = (pressed & kUp) ? 1 : 0;
                f.prevSteps = (pressed & kDown) ? 1 : 0;
                if (Held(kRightThumb)) {
                    // R3 held: the right stick is the turret keys, which bounce hydraulics (San Andreas's R3 and right stick).
                    bit(_rx <= -kTurretHeld, Protocol::kButtonTurretLeft);
                    bit(_rx >= kTurretHeld, Protocol::kButtonTurretRight);
                    bit(_ry >= kTurretHeld, Protocol::kButtonTurretUp);
                    bit(_ry <= -kTurretHeld, Protocol::kButtonTurretDown);
                    lookX = lookY = false;
                } else if (a_context == Context::Hydra) {
                    // The nozzles: up is the numpad's 8, which tilts them toward forward flight, as San Andreas's right stick.
                    bit(_ry >= kTurretHeld, Protocol::kButtonTurretUp);
                    bit(_ry <= -kTurretHeld, Protocol::kButtonTurretDown);
                    lookY = false;
                }
            }
            const float share = a_scoped ? a_look.scopeShare : (!vehicle && lt) ? a_look.aimShare : 1.0f;
            const double perSecond = static_cast<double>(a_look.lookSpeed) * share * a_seconds;
            if (lookX) {
                f.mouseX = Curve(_rx, a_look.deadZone) * perSecond;
            }
            if (lookY) {
                // The stick up looks up, which is the mouse moving up: negative counts.
                f.mouseY = -Curve(_ry, a_look.deadZone) * perSecond * a_look.vertical * (a_look.invertY ? -1.0 : 1.0);
            }
            return f;
        }

    private:
        static float Clamp(float a_value) { return a_value < -1.0f ? -1.0f : a_value > 1.0f ? 1.0f : a_value; }

        std::uint32_t _held = 0;
        std::uint32_t _pressed = 0;  // presses since the last Update
        float         _lt = 0.0f, _rt = 0.0f;
        float         _lx = 0.0f, _ly = 0.0f, _rx = 0.0f, _ry = 0.0f;
        float         _backSeconds = 0.0f;
        bool          _backFired = true;  // the press's tap or hold is spent
        bool          _backReleased = false;
        float         _cameraLeft = 0.0f;
    };

    inline constexpr std::uint16_t kModelHydra = 520;  // its right stick's up and down are the nozzles

    // B's and RB's hold while CJ has the player (DESIGN.md, Controller, Cheats on the pad): a tap is only Circle or R1 in a
    // cheat sequence, held kHoldGate it opens Skyrim's tween menu or starts the Shout. Update returns true once per hold,
    // when it opens; Open says whether this hold is past it.
    class HoldGate
    {
    public:
        bool Update(bool a_held, float a_seconds)
        {
            if (!a_held) {
                _seconds = 0.0f;
                _open = false;
                return false;
            }
            _seconds += a_seconds;
            if (!_open && _seconds >= kHoldGate) {
                _open = true;
                return true;
            }
            return false;
        }
        bool Open() const { return _open; }

    private:
        float _seconds = 0.0f;
        bool  _open = false;
    };

    // The pad's name for the activate key the help box names ("E: Open Breezehome" -> "LB: Open Breezehome").
    inline constexpr const char* kActivateName = "LB";

    // An AhShitRim.ini number: a_text as a decimal number within a_min..a_max, else a_default (unset, junk, out of range).
    inline float ParseNumber(std::string_view a_text, float a_default, float a_min, float a_max)
    {
        float value = 0.0f, scale = 0.0f;
        bool  digits = false, negative = false;
        std::size_t i = 0;
        while (i < a_text.size() && a_text[i] == ' ') {
            ++i;
        }
        if (i < a_text.size() && (a_text[i] == '-' || a_text[i] == '+')) {
            negative = a_text[i++] == '-';
        }
        for (; i < a_text.size(); ++i) {
            const char c = a_text[i];
            if (c >= '0' && c <= '9') {
                digits = true;
                if (scale > 0.0f) {
                    value += static_cast<float>(c - '0') * scale;
                    scale *= 0.1f;
                } else {
                    value = value * 10.0f + static_cast<float>(c - '0');
                }
            } else if (c == '.' && scale == 0.0f) {
                scale = 0.1f;
            } else if (c == ' ' || c == '\r') {
                break;
            } else {
                return a_default;
            }
        }
        if (negative) {
            value = -value;
        }
        return digits && value >= a_min && value <= a_max ? value : a_default;
    }
}
