// Builds the InputState Skyrim sends to San Andreas from Skyrim's raw input events. Header-only and
// free of game types, so the protocol tests cover it; the SKSE plugin feeds it from its input sink.
//
// Keyboard events carry DirectInput scan codes (DIK_*). San Andreas gets actions, not keys, in one of
// two layouts (AhShitRim.ini, [Input] KeyLayout):
// - Modern (the default): WASD (or the arrows) move, Left Shift sprints (mashed, San Andreas's faster
//   sprint), Space jumps, Left Ctrl crouches, Left Alt walks, F or Enter gets in and out of a vehicle.
//   In a vehicle Space is the handbrake and H the horn.
// - Classic, San Andreas's own PC defaults (gta-reversed's InitDefaultControlConfiguration): Space
//   sprints, Left Shift or Right Ctrl jumps, C crouches, the rest as above. In a vehicle Space or
//   Right Ctrl is the handbrake and Caps Lock or H the horn.
// In both, X has CJ insult whoever's about and the middle mouse button has him swear to himself
// (v16); Skyrim counts those itself.
// In a vehicle the up and down arrows lean a bike forward and back (and pitch an aircraft) instead, Q and E
// look left and right (an aircraft's rudder), and the numpad's 4, 6, 8 and 2 (8 and 2 also on Page Up and
// Page Down) are San Andreas's turret keys: the Hydra's nozzles (v19). ], [ and \ spawn a Sandking, a ZR-350
// and a Sanchez; ' spawns the dev pick, which ; steps through (Skyrim's side, VehicleKinds::DevPicker).
// The mouse: the left button fires and the right one aims on foot, and in a vehicle they fire its first and
// second weapons (v19); the wheel switches weapons on foot and tunes the radio in a vehicle. Everything else
// stays with Skyrim (see docs/PROTOCOL.md#input).

#pragma once

#include <cstdint>
#include <string_view>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Input
{
    // Scan codes Skyrim keeps for itself while San Andreas has the player: Escape and Tab open its
    // menus (the map is in Tab's, as San Andreas's is in its pause menu), ~ the console, F10 hands the
    // player back. M is Skyrim's map key, and (v20) only a letter of the cheat codes here.
    inline constexpr std::uint32_t kDikEscape = 0x01;
    inline constexpr std::uint32_t kDikTab = 0x0F;
    inline constexpr std::uint32_t kDikGrave = 0x29;
    inline constexpr std::uint32_t kDikM = 0x32;
    inline constexpr std::uint32_t kDikSlash = 0x35;
    inline constexpr std::uint32_t kDikF10 = 0x44;
    inline constexpr std::uint32_t kDikF11 = 0x57;  // a screenshot with everything on screen
    inline constexpr std::uint32_t kDikEquals = 0x0D;  // the debug key: gives CJ San Andreas's next weapon set

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
    inline constexpr std::uint32_t kDikF = 0x21;
    inline constexpr std::uint32_t kDikReturn = 0x1C;
    inline constexpr std::uint32_t kDikH = 0x23;
    inline constexpr std::uint32_t kDikLeftBracket = 0x1A;
    inline constexpr std::uint32_t kDikRightBracket = 0x1B;
    inline constexpr std::uint32_t kDikBackslash = 0x2B;
    inline constexpr std::uint32_t kDikE = 0x12;  // activates what CJ faces, on Skyrim's side
    inline constexpr std::uint32_t kDikRightCtrl = 0x9D;
    inline constexpr std::uint32_t kDikLeftCtrl = 0x1D;
    inline constexpr std::uint32_t kDikX = 0x2D;  // the insult key (v16): CJ insults whoever's about, on Skyrim's side
    inline constexpr std::uint32_t kDikCapsLock = 0x3A;
    inline constexpr std::uint32_t kDikQ = 0x10;            // v19: look left in a vehicle (Skyrim's favorites key, which is blocked while CJ has the player)
    inline constexpr std::uint32_t kDikSemicolon = 0x27;    // v19: the dev key that picks the next vehicle to spawn
    inline constexpr std::uint32_t kDikApostrophe = 0x28;   // v19: the dev key that spawns the pick
    inline constexpr std::uint32_t kDikNumpad8 = 0x48;
    inline constexpr std::uint32_t kDikNumpad4 = 0x4B;
    inline constexpr std::uint32_t kDikNumpad5 = 0x4C;  // v20: the hydraulics (LOWRIDER) bounce with the numpad's arrows
    inline constexpr std::uint32_t kDikNumpad6 = 0x4D;
    inline constexpr std::uint32_t kDikNumpad2 = 0x50;
    inline constexpr std::uint32_t kDikPageUp = 0xC9;
    inline constexpr std::uint32_t kDikPageDown = 0xD1;

    // Whether a key still opens one of Skyrim's menus while San Andreas has the player: Escape, Tab and the
    // console. Not M (v20): codes such as UZUMYMW have it in the middle.
    constexpr bool OpensSkyrimMenu(std::uint32_t a_dik) { return a_dik == kDikEscape || a_dik == kDikTab || a_dik == kDikGrave; }

    enum class KeyLayout : std::uint8_t
    {
        Modern,   // Shift sprints, Space jumps
        Classic,  // San Andreas's PC defaults: Space sprints, Shift jumps
    };

    // AhShitRim.ini's [Input] KeyLayout: "Classic" (any case) picks San Andreas's layout, anything
    // else the modern one.
    constexpr KeyLayout ParseKeyLayout(std::string_view a_text)
    {
        constexpr std::string_view classic = "classic";
        if (a_text.size() != classic.size()) {
            return KeyLayout::Modern;
        }
        for (std::size_t i = 0; i < classic.size(); ++i) {
            const char c = a_text[i] >= 'A' && a_text[i] <= 'Z' ? static_cast<char>(a_text[i] - 'A' + 'a') : a_text[i];
            if (c != classic[i]) {
                return KeyLayout::Modern;
            }
        }
        return KeyLayout::Classic;
    }

    constexpr const char* NameOf(KeyLayout a_layout) { return a_layout == KeyLayout::Classic ? "Classic" : "Modern"; }

    // San Andreas vehicle model ids the spawn keys ask for.
    inline constexpr std::uint16_t kModelSandking = 495;  // the test car
    inline constexpr std::uint16_t kModelZr350 = 477;     // the fun car
    inline constexpr std::uint16_t kModelSanchez = 468;   // the dirt bike

    // DirectInput scan code -> the vehicle model its key spawns, 0 for any other key.
    constexpr std::uint16_t SpawnModelFromScanCode(std::uint32_t a_dik)
    {
        switch (a_dik) {
        case kDikRightBracket: return kModelSandking;
        case kDikLeftBracket: return kModelZr350;
        case kDikBackslash: return kModelSanchez;
        default: return 0;
        }
    }

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
        EnterExit,
        Horn,
        LeanForward,  // the up arrow: forwards on foot
        LeanBack,     // the down arrow: back on foot
        LookLeft,     // v19, Q: in a vehicle only
        LookRight,    // v19, E: in a vehicle only (on foot Skyrim's side activates with it)
        TurretLeft,   // v19, the numpad: in a vehicle only
        TurretRight,
        TurretUp,
        TurretDown,
        Hydraulics,   // v20, numpad 5: in a vehicle only, San Andreas's R3
    };

    // DirectInput scan code -> the action it drives in San Andreas, None for keys San Andreas doesn't get.
    constexpr Action ActionFromScanCode(std::uint32_t a_dik, KeyLayout a_layout = KeyLayout::Modern)
    {
        if (a_layout == KeyLayout::Classic) {
            switch (a_dik) {
            case kDikSpace: return Action::Sprint;
            case kDikLeftShift:
            case kDikRightCtrl: return Action::Jump;
            case kDikCapsLock: return Action::Horn;
            case kDikC: return Action::Crouch;
            case kDikLeftCtrl: return Action::None;
            default: break;
            }
        }
        switch (a_dik) {
        case kDikW: return Action::Forward;
        case kDikUp: return Action::LeanForward;
        case kDikS: return Action::Back;
        case kDikDown: return Action::LeanBack;
        case kDikA:
        case kDikLeft: return Action::Left;
        case kDikD:
        case kDikRight: return Action::Right;
        case kDikLeftShift: return Action::Sprint;
        case kDikSpace: return Action::Jump;
        case kDikLeftCtrl: return Action::Crouch;
        case kDikLeftAlt: return Action::Walk;
        case kDikF:
        case kDikReturn: return Action::EnterExit;
        case kDikH: return Action::Horn;
        case kDikQ: return Action::LookLeft;
        case kDikE: return Action::LookRight;
        case kDikNumpad4: return Action::TurretLeft;
        case kDikNumpad5: return Action::Hydraulics;
        case kDikNumpad6: return Action::TurretRight;
        case kDikNumpad8:
        case kDikPageUp: return Action::TurretUp;
        case kDikNumpad2:
        case kDikPageDown: return Action::TurretDown;
        default: return Action::None;  // Escape, Tab, M, ~, F10 and anything unmapped
        }
    }

    // Whether a key is the handbrake in a vehicle: Space, which jumps on foot in the modern layout and
    // sprints in the classic one, and Right Ctrl in the classic one.
    constexpr bool IsHandbrakeKey(std::uint32_t a_dik, KeyLayout a_layout = KeyLayout::Modern)
    {
        return a_dik == kDikSpace || (a_layout == KeyLayout::Classic && a_dik == kDikRightCtrl);
    }

    // Accumulates held keys and mouse movement and produces the InputState to publish.
    class Builder
    {
    public:
        void      SetLayout(KeyLayout a_layout) { _layout = a_layout; }
        KeyLayout Layout() const { return _layout; }

        // Keyboard event: a_dik is the scan code, a_down whether it is held now. Pressing a spawn key
        // asks for its vehicle (once per press), unless a_canSpawn is false (a menu or the console
        // has the keyboard).
        void SetKey(std::uint32_t a_dik, bool a_down, bool a_canSpawn = true)
        {
            if (a_dik >= 256) {
                return;
            }
            const auto model = a_dik == kDikApostrophe ? _devModel : SpawnModelFromScanCode(a_dik);
            if (model && a_down && a_canSpawn && !IsKeyDown(a_dik)) {
                _spawnModel = model;
                _spawnCount++;
            }
            const std::uint64_t bit = 1ull << (a_dik & 63);
            if (a_down) {
                _keys[a_dik >> 6] |= bit;
            } else {
                _keys[a_dik >> 6] &= ~bit;
            }
        }

        // A spawn request that comes from no key (v20: a cheat typed in Skyrim), as a spawn key's press.
        void RequestSpawn(std::uint16_t a_model)
        {
            if (a_model) {
                _spawnModel = a_model;
                _spawnCount++;
            }
        }

        // The model ' spawns (v19): the dev pick, a vehicle or the gear (VehicleKinds); 0 spawns nothing.
        void          SetDevModel(std::uint16_t a_model) { _devModel = a_model; }
        std::uint16_t DevModel() const { return _devModel; }

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

        // The mouse buttons (v12): the left one fires, the right one aims, on foot.
        void SetFire(bool a_down) { _fire = a_down; }
        void SetAim(bool a_down) { _aim = a_down; }

        // A mouse wheel step (v7): up tunes the next radio station in a vehicle, down the one before; on
        // foot (v12) up switches to the next weapon and down to the previous one. San Andreas acts on
        // the change by where CJ is.
        void AddWheel(bool a_up)
        {
            (a_up ? _radioNext : _radioPrev)++;
        }

        // The insult key (v16), X: each press has CJ insult whoever's about, and give the finger on foot.
        // The swear key, the middle mouse button: each press has him swear to himself.
        void AddInsult() { _insultCount++; }
        void AddSwear() { _swearCount++; }

        // A line for CJ to say (v21), of a Dialogue::Category: his answer in a conversation.
        void AddLine(std::uint8_t a_kind)
        {
            _lineCount++;
            _lineKind = a_kind;
        }

        // A sound for San Andreas to play (v23, Protocol::kCue*): the mission passed tune, CJ's intro line.
        void AddCue(std::uint8_t a_cue)
        {
            _cueCount++;
            _cue = a_cue;
        }

        // Everything released, e.g. when ownership ends. Mouse totals keep accumulating.
        void ReleaseAll()
        {
            for (auto& k : _keys) {
                k = 0;
            }
            _fire = _aim = false;
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
            s.spawnModel = _spawnModel;
            s.spawnCount = _spawnCount;
            s.radioNext = _radioNext;
            s.radioPrev = _radioPrev;
            s.insultCount = _insultCount;
            s.swearCount = _swearCount;
            s.lineCount = _lineCount;
            s.lineKind = _lineKind;
            s.cueCount = _cueCount;
            s.cue = _cue;
            if (!a_live) {
                return s;
            }
            s.moveX = Axis(Action::Right, Action::Left);
            s.moveY = Axis(Action::Forward, Action::Back);
            if (Held(Action::Sprint)) s.buttons |= Protocol::kButtonSprint;
            if (Held(Action::Jump)) s.buttons |= Protocol::kButtonJump;
            if (Held(Action::Crouch)) s.buttons |= Protocol::kButtonCrouch;
            if (Held(Action::Walk)) s.buttons |= Protocol::kButtonWalk;
            if (Held(Action::EnterExit)) s.buttons |= Protocol::kButtonEnterExit;
            if (Held(Action::Horn)) s.buttons |= Protocol::kButtonHorn;
            if (Held(Action::LeanForward)) s.buttons |= Protocol::kButtonLeanForward;
            if (Held(Action::LeanBack)) s.buttons |= Protocol::kButtonLeanBack;
            if (Held(Action::LookLeft)) s.buttons |= Protocol::kButtonLookLeft;
            if (Held(Action::LookRight)) s.buttons |= Protocol::kButtonLookRight;
            if (Held(Action::TurretLeft)) s.buttons |= Protocol::kButtonTurretLeft;
            if (Held(Action::TurretRight)) s.buttons |= Protocol::kButtonTurretRight;
            if (Held(Action::TurretUp)) s.buttons |= Protocol::kButtonTurretUp;
            if (Held(Action::TurretDown)) s.buttons |= Protocol::kButtonTurretDown;
            if (Held(Action::Hydraulics)) s.buttons |= Protocol::kButtonHydraulics;
            if (_fire) s.buttons |= Protocol::kButtonFire;
            if (_aim) s.buttons |= Protocol::kButtonAim;
            for (std::uint32_t dik = 0; dik < 256; ++dik) {
                if (IsKeyDown(dik) && IsHandbrakeKey(dik, _layout)) {
                    s.buttons |= Protocol::kButtonHandbrake;
                    break;
                }
            }
            return s;
        }

    private:
        bool Held(Action a_action) const
        {
            for (std::uint32_t dik = 0; dik < 256; ++dik) {
                if (IsKeyDown(dik) && ActionFromScanCode(dik, _layout) == a_action) {
                    return true;
                }
            }
            return false;
        }

        float Axis(Action a_pos, Action a_neg) const
        {
            return (Held(a_pos) ? 1.0f : 0.0f) - (Held(a_neg) ? 1.0f : 0.0f);
        }

        KeyLayout     _layout = KeyLayout::Modern;
        bool          _fire = false;
        bool          _aim = false;
        std::uint64_t _keys[4]{};
        double        _mouseX = 0.0;
        double        _mouseY = 0.0;
        std::uint16_t _spawnModel = 0;
        std::uint16_t _spawnCount = 0;
        std::uint16_t _devModel = 0;
        std::uint16_t _radioNext = 0;
        std::uint16_t _radioPrev = 0;
        std::uint16_t _insultCount = 0;
        std::uint16_t _swearCount = 0;
        std::uint16_t _lineCount = 0;
        std::uint8_t  _lineKind = 0;
        std::uint16_t _cueCount = 0;
        std::uint8_t  _cue = 0;
    };

    // The San Andreas side: an InputState as CControllerState values (pad mode 0), for CJ on foot or
    // in a vehicle. Sticks run -128..128 with forward negative; buttons are 255 held, 0 released.
    struct PadState
    {
        std::int16_t leftStickX = 0;      // on foot: move; in a vehicle: steer
        std::int16_t leftStickY = 0;      // on foot: move
        std::int16_t buttonCross = 0;     // on foot: sprint; in a vehicle: accelerate
        std::int16_t buttonSquare = 0;    // on foot: jump; in a vehicle: brake and reverse
        std::int16_t buttonTriangle = 0;  // get in or out of a vehicle
        std::int16_t buttonCircle = 0;    // on foot (v12): fire; a melee weapon attacks
        std::int16_t rightShoulder1 = 0;  // on foot (v12): aim; in a vehicle: handbrake
        std::int16_t shockButtonL = 0;    // on foot: crouch; in a vehicle: horn (a plane's landing gear)
        std::int16_t pedWalk = 0;         // on foot: walk
        std::int16_t leftShoulder1 = 0;   // in a vehicle (v19): its second weapon
        std::int16_t leftShoulder2 = 0;   // in a vehicle (v19): look left, an aircraft's rudder
        std::int16_t rightShoulder2 = 0;  // in a vehicle (v19): look right
        std::int16_t rightStickX = 0;     // in a vehicle (v19): the turret keys, left negative
        std::int16_t rightStickY = 0;     // in a vehicle (v19): the turret keys, up (numpad 8) negative
        std::int16_t shockButtonR = 0;    // in a vehicle (v20): numpad 5, the hydraulics' jump
    };

    constexpr std::int16_t Stick(float a_value)
    {
        const float v = a_value < -1.0f ? -1.0f : a_value > 1.0f ? 1.0f : a_value;
        return static_cast<std::int16_t>(v * 128.0f);
    }

    // a_inVehicle: CJ drives or rides, so the keys take San Andreas's vehicle layout (W
    // accelerates, S brakes and reverses, A/D steer, kButtonHandbrake is the handbrake, kButtonHorn
    // the horn, the up and down arrows lean a bike forward and back; v19: the mouse buttons fire the
    // vehicle's weapons, Q and E look left and right, the numpad is the right stick). On foot the arrows
    // walk like W and S.
    constexpr PadState ToPad(const Protocol::InputState& a_input, bool a_inVehicle = false)
    {
        const auto held = [&](std::uint32_t a_button) -> std::int16_t { return (a_input.buttons & a_button) ? 255 : 0; };
        PadState   p;
        p.leftStickX = Stick(a_input.moveX);
        p.buttonTriangle = held(Protocol::kButtonEnterExit);
        const float lean = ((a_input.buttons & Protocol::kButtonLeanForward) ? 1.0f : 0.0f) - ((a_input.buttons & Protocol::kButtonLeanBack) ? 1.0f : 0.0f);
        if (a_inVehicle) {
            p.leftStickY = Stick(-lean);  // stick forward (negative) leans forward
            p.buttonCross = a_input.moveY > 0.0f ? 255 : 0;
            p.buttonSquare = a_input.moveY < 0.0f ? 255 : 0;
            p.rightShoulder1 = held(Protocol::kButtonHandbrake);
            p.shockButtonL = held(Protocol::kButtonHorn);
            p.buttonCircle = held(Protocol::kButtonFire);
            p.leftShoulder1 = held(Protocol::kButtonAim);
            p.leftShoulder2 = held(Protocol::kButtonLookLeft);
            p.rightShoulder2 = held(Protocol::kButtonLookRight);
            const auto axis = [&](std::uint32_t a_neg, std::uint32_t a_pos) {
                return Stick(((a_input.buttons & a_pos) ? 1.0f : 0.0f) - ((a_input.buttons & a_neg) ? 1.0f : 0.0f));
            };
            p.rightStickX = axis(Protocol::kButtonTurretLeft, Protocol::kButtonTurretRight);
            p.rightStickY = axis(Protocol::kButtonTurretUp, Protocol::kButtonTurretDown);
            p.shockButtonR = held(Protocol::kButtonHydraulics);
            return p;
        }
        p.leftStickY = Stick(-(a_input.moveY + lean));  // clamped: W and the up arrow together are still full
        p.buttonCross = held(Protocol::kButtonSprint);
        p.buttonSquare = held(Protocol::kButtonJump);
        p.shockButtonL = held(Protocol::kButtonCrouch);
        p.pedWalk = held(Protocol::kButtonWalk);
        p.buttonCircle = held(Protocol::kButtonFire);
        p.rightShoulder1 = held(Protocol::kButtonAim);
        return p;
    }
}
