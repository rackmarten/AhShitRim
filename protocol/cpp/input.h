// Builds the InputState Skyrim sends to San Andreas from Skyrim's raw input events. Header-only and
// free of game types, so the protocol tests cover it; the SKSE plugin feeds it from its input sink.
//
// Keyboard events carry DirectInput scan codes (DIK_*). San Andreas gets actions, not keys, with
// GTA's own keyboard layout: WASD (or the arrows) move, Left Shift sprints, Space jumps, C crouches
// and Left Alt walks. Everything else stays with Skyrim (see docs/PROTOCOL.md#input).

#pragma once

#include <cstdint>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Input
{
    // Scan codes Skyrim keeps for itself while San Andreas has the player: Escape and Tab open its
    // menus, M its map, ~ the console, F10 hands the player back.
    inline constexpr std::uint32_t kDikEscape = 0x01;
    inline constexpr std::uint32_t kDikTab = 0x0F;
    inline constexpr std::uint32_t kDikGrave = 0x29;
    inline constexpr std::uint32_t kDikM = 0x32;
    inline constexpr std::uint32_t kDikF10 = 0x44;

    inline constexpr std::uint32_t kDikW = 0x11;
    inline constexpr std::uint32_t kDikA = 0x1E;
    inline constexpr std::uint32_t kDikS = 0x1F;
    inline constexpr std::uint32_t kDikD = 0x20;
    inline constexpr std::uint32_t kDikC = 0x2E;
    inline constexpr std::uint32_t kDikLeftShift = 0x2A;
    inline constexpr std::uint32_t kDikLeftAlt = 0x38;
    inline constexpr std::uint32_t kDikSpace = 0x39;
    inline constexpr std::uint32_t kDikUp = 0xC8;
    inline constexpr std::uint32_t kDikLeft = 0xCB;
    inline constexpr std::uint32_t kDikRight = 0xCD;
    inline constexpr std::uint32_t kDikDown = 0xD0;

    enum class Action : std::uint8_t
    {
        None,
        Forward,
        Back,
        Left,
        Right,
        Sprint,
        Jump,
        Crouch,
        Walk,
    };

    // DirectInput scan code -> the action it drives in San Andreas, None for keys San Andreas doesn't get.
    constexpr Action ActionFromScanCode(std::uint32_t a_dik)
    {
        switch (a_dik) {
        case kDikW:
        case kDikUp: return Action::Forward;
        case kDikS:
        case kDikDown: return Action::Back;
        case kDikA:
        case kDikLeft: return Action::Left;
        case kDikD:
        case kDikRight: return Action::Right;
        case kDikLeftShift: return Action::Sprint;
        case kDikSpace: return Action::Jump;
        case kDikC: return Action::Crouch;
        case kDikLeftAlt: return Action::Walk;
        default: return Action::None;  // Escape, Tab, M, ~, F10 and anything unmapped
        }
    }

    // Accumulates held keys and mouse movement and produces the InputState to publish.
    class Builder
    {
    public:
        // Keyboard event: a_dik is the scan code, a_down whether it is held now.
        void SetKey(std::uint32_t a_dik, bool a_down)
        {
            if (a_dik >= 256) {
                return;
            }
            const std::uint64_t bit = 1ull << (a_dik & 63);
            if (a_down) {
                _keys[a_dik >> 6] |= bit;
            } else {
                _keys[a_dik >> 6] &= ~bit;
            }
        }

        bool IsKeyDown(std::uint32_t a_dik) const
        {
            return a_dik < 256 && (_keys[a_dik >> 6] & (1ull << (a_dik & 63))) != 0;
        }

        // Raw counts from a mouse move event, DirectInput's directions (y grows downwards).
        void AddMouse(std::int32_t a_dx, std::int32_t a_dy)
        {
            _mouseX += a_dx;
            _mouseY += a_dy;
        }

        // Everything released, e.g. when ownership ends. Mouse totals keep accumulating.
        void ReleaseAll()
        {
            for (auto& k : _keys) {
                k = 0;
            }
        }

        // a_live false (a Skyrim menu has the input): no movement or buttons, so CJ stands still.
        // a_paused (that menu pauses the game, e.g. Escape or the map): San Andreas freezes its time too.
        Protocol::InputState Build(std::uint32_t a_frame, bool a_ownsPlayer, bool a_live, bool a_paused = false) const
        {
            Protocol::InputState s{};
            s.frame = a_frame;
            s.flags = a_ownsPlayer ? Protocol::kInputOwnsPlayer : 0;
            if (a_ownsPlayer && a_paused) {
                s.flags |= Protocol::kInputPaused;
            }
            s.mouseX = _mouseX;
            s.mouseY = _mouseY;
            if (!a_live) {
                return s;
            }
            s.moveX = Axis(Action::Right, Action::Left);
            s.moveY = Axis(Action::Forward, Action::Back);
            if (Held(Action::Sprint)) s.buttons |= Protocol::kButtonSprint;
            if (Held(Action::Jump)) s.buttons |= Protocol::kButtonJump;
            if (Held(Action::Crouch)) s.buttons |= Protocol::kButtonCrouch;
            if (Held(Action::Walk)) s.buttons |= Protocol::kButtonWalk;
            return s;
        }

    private:
        bool Held(Action a_action) const
        {
            for (std::uint32_t dik = 0; dik < 256; ++dik) {
                if (IsKeyDown(dik) && ActionFromScanCode(dik) == a_action) {
                    return true;
                }
            }
            return false;
        }

        float Axis(Action a_pos, Action a_neg) const
        {
            return (Held(a_pos) ? 1.0f : 0.0f) - (Held(a_neg) ? 1.0f : 0.0f);
        }

        std::uint64_t _keys[4]{};
        double        _mouseX = 0.0;
        double        _mouseY = 0.0;
    };

    // The San Andreas side: an InputState as CControllerState values for CJ on foot (pad mode 0).
    // Sticks run -128..128 with forward negative; buttons are 255 held, 0 released.
    struct PadState
    {
        std::int16_t leftStickX = 0;
        std::int16_t leftStickY = 0;
        std::int16_t buttonCross = 0;   // sprint
        std::int16_t buttonSquare = 0;  // jump
        std::int16_t shockButtonL = 0;  // crouch
        std::int16_t pedWalk = 0;       // walk
    };

    constexpr std::int16_t Stick(float a_value)
    {
        const float v = a_value < -1.0f ? -1.0f : a_value > 1.0f ? 1.0f : a_value;
        return static_cast<std::int16_t>(v * 128.0f);
    }

    constexpr PadState ToPad(const Protocol::InputState& a_input)
    {
        PadState p;
        p.leftStickX = Stick(a_input.moveX);
        p.leftStickY = Stick(-a_input.moveY);
        p.buttonCross = (a_input.buttons & Protocol::kButtonSprint) ? 255 : 0;
        p.buttonSquare = (a_input.buttons & Protocol::kButtonJump) ? 255 : 0;
        p.shockButtonL = (a_input.buttons & Protocol::kButtonCrouch) ? 255 : 0;
        p.pedWalk = (a_input.buttons & Protocol::kButtonWalk) ? 255 : 0;
        return p;
    }
}
