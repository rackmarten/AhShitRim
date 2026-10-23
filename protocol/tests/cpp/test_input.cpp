#include <doctest/doctest.h>

#include "../../cpp/input.h"

#include <string>

using namespace AhShitRim;
using namespace AhShitRim::Input;

TEST_CASE("GTA's keyboard layout maps to CJ's actions")
{
    CHECK(ActionFromScanCode(kDikW) == Action::Forward);
    CHECK(ActionFromScanCode(kDikS) == Action::Back);
    CHECK(ActionFromScanCode(kDikA) == Action::Left);
    CHECK(ActionFromScanCode(kDikD) == Action::Right);
    CHECK(ActionFromScanCode(kDikUp) == Action::LeanForward);
    CHECK(ActionFromScanCode(kDikDown) == Action::LeanBack);
    CHECK(ActionFromScanCode(kDikLeft) == Action::Left);
    CHECK(ActionFromScanCode(kDikRight) == Action::Right);
    CHECK(ActionFromScanCode(kDikLeftShift) == Action::Sprint);
    CHECK(ActionFromScanCode(kDikSpace) == Action::Jump);
    CHECK(ActionFromScanCode(kDikLeftCtrl) == Action::Crouch);
    CHECK(ActionFromScanCode(kDikC) == Action::None);  // C crouches only in the classic layout
    CHECK(ActionFromScanCode(kDikX) == Action::None);  // the insult key, counted on Skyrim's side
    CHECK(ActionFromScanCode(kDikLeftAlt) == Action::Walk);
}

TEST_CASE("keys that stay with Skyrim never reach San Andreas")
{
    CHECK(ActionFromScanCode(kDikEscape) == Action::None);
    CHECK(ActionFromScanCode(kDikTab) == Action::None);
    CHECK(ActionFromScanCode(kDikM) == Action::None);
    CHECK(ActionFromScanCode(kDikGrave) == Action::None);
    CHECK(ActionFromScanCode(kDikF10) == Action::None);
    // E activates on Skyrim's side on foot, so Skyrim only passes it on in a vehicle (v19: look right).
    CHECK(ActionFromScanCode(kDikE) == Action::LookRight);
    CHECK(ActionFromScanCode(kDikE, KeyLayout::Classic) == Action::LookRight);
}

TEST_CASE("the builder turns held keys into axes and buttons")
{
    Builder b;
    auto s = b.Build(7, true, true);
    CHECK(s.frame == 7);
    CHECK(s.flags == Protocol::kInputOwnsPlayer);
    CHECK(s.moveX == 0.0f);
    CHECK(s.moveY == 0.0f);
    CHECK(s.buttons == 0);

    b.SetKey(kDikW, true);
    b.SetKey(kDikD, true);
    b.SetKey(kDikLeftShift, true);
    s = b.Build(8, true, true);
    CHECK(s.moveY == 1.0f);
    CHECK(s.moveX == 1.0f);
    CHECK(s.buttons == Protocol::kButtonSprint);

    b.SetKey(kDikS, true);  // forwards and back cancel
    b.SetKey(kDikSpace, true);
    b.SetKey(kDikLeftCtrl, true);
    b.SetKey(kDikLeftAlt, true);
    s = b.Build(9, true, true);
    CHECK(s.moveY == 0.0f);
    // v23: Left Ctrl and Left Alt are a vehicle's fire keys as well, which ToPad only reads in one.
    CHECK(s.buttons == (Protocol::kButtonSprint | Protocol::kButtonJump | Protocol::kButtonCrouch | Protocol::kButtonWalk | Protocol::kButtonHandbrake |
                           Protocol::kButtonVehicleFire | Protocol::kButtonVehicleFireAlt));

    b.SetKey(kDikW, false);
    b.SetKey(kDikD, false);
    s = b.Build(10, true, true);
    CHECK(s.moveY == -1.0f);
    CHECK(s.moveX == 0.0f);

    b.SetKey(300, true);  // out of range, ignored
    CHECK_FALSE(b.IsKeyDown(300));
}

TEST_CASE("a menu with the input stops CJ but keeps the mouse totals")
{
    Builder b;
    b.SetKey(kDikW, true);
    b.SetKey(kDikSpace, true);
    b.AddMouse(10, -4);
    b.AddMouse(5, 1);
    const auto s = b.Build(1, true, false, true);
    CHECK(s.moveY == 0.0f);
    CHECK(s.buttons == 0);
    CHECK(s.flags == (Protocol::kInputOwnsPlayer | Protocol::kInputPaused));
    CHECK(s.mouseX == 15.0);
    CHECK(s.mouseY == -3.0);
}

TEST_CASE("pausing only counts while San Andreas has the player")
{
    Builder b;
    CHECK(b.Build(1, false, true, true).flags == 0);
}

TEST_CASE("ReleaseAll lets go of every key")
{
    Builder b;
    b.SetKey(kDikW, true);
    b.SetKey(kDikLeftShift, true);
    b.AddMouse(3, 3);
    b.ReleaseAll();
    const auto s = b.Build(1, true, true);
    CHECK(s.moveY == 0.0f);
    CHECK(s.buttons == 0);
    CHECK(s.mouseX == 3.0);
}

TEST_CASE("input becomes San Andreas pad values")
{
    Protocol::InputState in{};
    in.moveX = 1.0f;
    in.moveY = 1.0f;
    in.buttons = Protocol::kButtonSprint | Protocol::kButtonCrouch;
    auto pad = ToPad(in);
    CHECK(pad.leftStickX == 128);
    CHECK(pad.leftStickY == -128);  // forwards is negative on San Andreas's stick
    CHECK(pad.buttonCross == 255);
    CHECK(pad.buttonSquare == 0);
    CHECK(pad.shockButtonL == 255);
    CHECK(pad.pedWalk == 0);

    in.moveX = -3.0f;  // clamped
    in.moveY = -1.0f;
    in.buttons = Protocol::kButtonJump | Protocol::kButtonWalk;
    pad = ToPad(in);
    CHECK(pad.leftStickX == -128);
    CHECK(pad.leftStickY == 128);
    CHECK(pad.buttonCross == 0);
    CHECK(pad.buttonSquare == 255);
    CHECK(pad.pedWalk == 255);
}

TEST_CASE("F, Enter and H drive the vehicle actions")
{
    CHECK(ActionFromScanCode(kDikF) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikReturn) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikH) == Action::Horn);
    Builder b;
    b.SetKey(kDikF, true);
    b.SetKey(kDikH, true);
    CHECK(b.Build(1, true, true).buttons == (Protocol::kButtonEnterExit | Protocol::kButtonHorn));
}

TEST_CASE("each press of a spawn key asks for its vehicle once")
{
    CHECK(SpawnModelFromScanCode(kDikRightBracket) == 495);
    CHECK(SpawnModelFromScanCode(kDikLeftBracket) == 477);
    CHECK(SpawnModelFromScanCode(kDikBackslash) == 468);
    CHECK(SpawnModelFromScanCode(kDikW) == 0);
    CHECK(ActionFromScanCode(kDikRightBracket) == Action::None);

    Builder b;
    CHECK(b.Build(1, true, true).spawnCount == 0);
    b.SetKey(kDikRightBracket, true);
    b.SetKey(kDikRightBracket, true);  // key repeat: still one press
    auto s = b.Build(2, true, true);
    CHECK(s.spawnModel == kModelSandking);
    CHECK(s.spawnCount == 1);
    b.SetKey(kDikRightBracket, false);
    b.SetKey(kDikBackslash, true);
    s = b.Build(3, true, false);  // still in the input while a menu has it afterwards
    CHECK(s.spawnModel == kModelSanchez);
    CHECK(s.spawnCount == 2);

    b.SetKey(kDikBackslash, false);
    b.SetKey(kDikLeftBracket, true, false);  // typed into the console
    CHECK(b.Build(4, true, true).spawnCount == 2);
    CHECK(b.IsKeyDown(kDikLeftBracket));

    // v20: a cheat asks for one with no key.
    b.RequestSpawn(520);
    s = b.Build(5, true, true);
    CHECK(s.spawnModel == 520);
    CHECK(s.spawnCount == 3);
    b.RequestSpawn(0);  // nothing
    CHECK(b.Build(6, true, true).spawnCount == 3);
}

TEST_CASE("in a vehicle the keys take San Andreas's vehicle layout")
{
    Protocol::InputState in{};
    in.moveX = -1.0f;
    in.moveY = 1.0f;
    in.buttons = Protocol::kButtonHandbrake | Protocol::kButtonHorn | Protocol::kButtonCrouch | Protocol::kButtonSprint;
    auto pad = ToPad(in, true);
    CHECK(pad.leftStickX == -128);  // steer
    CHECK(pad.leftStickY == 0);
    CHECK(pad.buttonCross == 255);  // accelerate
    CHECK(pad.buttonSquare == 0);
    CHECK(pad.rightShoulder1 == 255);  // handbrake
    CHECK(pad.shockButtonL == 255);    // horn, not crouch
    CHECK(pad.pedWalk == 0);

    in.moveY = -1.0f;
    in.buttons = Protocol::kButtonCrouch | Protocol::kButtonEnterExit;
    pad = ToPad(in, true);
    CHECK(pad.buttonCross == 0);
    CHECK(pad.buttonSquare == 255);  // brake, then reverse
    CHECK(pad.shockButtonL == 0);    // crouch does nothing in a vehicle
    CHECK(pad.buttonTriangle == 255);

    pad = ToPad(in, false);  // on foot the same key gets in
    CHECK(pad.buttonTriangle == 255);
    CHECK(pad.rightShoulder1 == 0);
}

TEST_CASE("the arrows walk on foot and lean a bike in a vehicle")
{
    Builder b;
    b.SetKey(kDikUp, true);
    auto in = b.Build(1, true, true);
    CHECK(in.moveY == 0.0f);  // W and S only
    CHECK(in.buttons == Protocol::kButtonLeanForward);
    CHECK(ToPad(in, false).leftStickY == -128);  // walks forwards on foot
    auto pad = ToPad(in, true);
    CHECK(pad.leftStickY == -128);  // leans forward on a bike
    CHECK(pad.buttonCross == 0);    // without accelerating

    b.SetKey(kDikUp, false);
    b.SetKey(kDikDown, true);
    b.SetKey(kDikW, true);
    in = b.Build(2, true, true);
    pad = ToPad(in, true);
    CHECK(pad.leftStickY == 128);  // leans back: a wheelie...
    CHECK(pad.buttonCross == 255);  // ... under power
    CHECK(ToPad(in, false).leftStickY == 0);  // on foot W and the down arrow cancel
}

TEST_CASE("each wheel step counts a radio retune, also while a menu has the input")
{
    Builder b;
    b.AddWheel(true);
    b.AddWheel(true);
    b.AddWheel(false);
    auto s = b.Build(1, true, true);
    CHECK(s.radioNext == 2);
    CHECK(s.radioPrev == 1);
    s = b.Build(2, true, false);  // the counts carry over; San Andreas decides what a change means
    CHECK(s.radioNext == 2);
    CHECK(s.radioPrev == 1);
}

TEST_CASE("each insult and swear press counts on its own, also while a menu has the input")
{
    Builder b;
    CHECK(b.Build(1, true, true).insultCount == 0);
    b.AddInsult();
    b.AddInsult();
    b.AddSwear();
    auto s = b.Build(2, true, true);
    CHECK(s.insultCount == 2);
    CHECK(s.swearCount == 1);
    s = b.Build(3, true, false);  // the counts carry over; San Andreas decides what a change means
    CHECK(s.insultCount == 2);
    CHECK(s.swearCount == 1);
}

TEST_CASE("each line asked of CJ counts, with the kind of the latest")
{
    Builder b;
    CHECK(b.Build(1, true, true).lineCount == 0);
    b.AddLine(2);
    b.AddLine(7);
    auto s = b.Build(2, true, true);
    CHECK(s.lineCount == 2);
    CHECK(s.lineKind == 7);
    s = b.Build(3, true, false);  // a menu has the input: the dialogue's lines still count
    CHECK(s.lineCount == 2);
    CHECK(s.lineKind == 7);
}

TEST_CASE("the ini picks the classic layout by name, anything else is modern")
{
    CHECK(ParseKeyLayout("Classic") == KeyLayout::Classic);
    CHECK(ParseKeyLayout("CLASSIC") == KeyLayout::Classic);
    CHECK(ParseKeyLayout("classic") == KeyLayout::Classic);
    CHECK(ParseKeyLayout("Modern") == KeyLayout::Modern);
    CHECK(ParseKeyLayout("") == KeyLayout::Modern);
    CHECK(ParseKeyLayout("classical") == KeyLayout::Modern);
    CHECK(std::string(NameOf(KeyLayout::Classic)) == "Classic");
    CHECK(Builder().Layout() == KeyLayout::Modern);
}

TEST_CASE("the classic layout is San Andreas's own: Space sprints, Shift jumps")
{
    constexpr auto classic = KeyLayout::Classic;
    CHECK(ActionFromScanCode(kDikSpace, classic) == Action::Sprint);
    CHECK(ActionFromScanCode(kDikLeftShift, classic) == Action::Jump);
    CHECK(ActionFromScanCode(kDikRightCtrl, classic) == Action::Jump);
    CHECK(ActionFromScanCode(kDikCapsLock, classic) == Action::Horn);
    CHECK(ActionFromScanCode(kDikH, classic) == Action::Horn);
    CHECK(ActionFromScanCode(kDikC, classic) == Action::Crouch);  // San Andreas's own crouch key
    CHECK(ActionFromScanCode(kDikLeftCtrl, classic) == Action::None);
    // Everything else as in the modern layout.
    CHECK(ActionFromScanCode(kDikW, classic) == Action::Forward);
    CHECK(ActionFromScanCode(kDikLeftAlt, classic) == Action::Walk);
    CHECK(ActionFromScanCode(kDikF, classic) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikReturn, classic) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikUp, classic) == Action::LeanForward);
    // The modern layout has no Right Ctrl on foot; (v23) Caps Lock is the horn there too.
    CHECK(ActionFromScanCode(kDikRightCtrl) == Action::None);
    CHECK(ActionFromScanCode(kDikCapsLock) == Action::Horn);

    Builder b;
    b.SetLayout(classic);
    b.SetKey(kDikSpace, true);
    b.SetKey(kDikW, true);
    auto in = b.Build(1, true, true);
    CHECK(in.buttons == (Protocol::kButtonSprint | Protocol::kButtonHandbrake));
    auto pad = ToPad(in, false);
    CHECK(pad.buttonCross == 255);   // sprints
    CHECK(pad.buttonSquare == 0);    // doesn't jump
    pad = ToPad(in, true);
    CHECK(pad.rightShoulder1 == 255);  // the handbrake in a vehicle
    CHECK(pad.buttonCross == 255);     // W accelerates

    b.SetKey(kDikSpace, false);
    b.SetKey(kDikLeftShift, true);
    in = b.Build(2, true, true);
    CHECK(in.buttons == Protocol::kButtonJump);
    CHECK(ToPad(in, false).buttonSquare == 255);  // jumps
    CHECK(ToPad(in, true).rightShoulder1 == 0);   // Shift does nothing in a vehicle

    b.SetKey(kDikLeftShift, false);
    b.SetKey(kDikRightCtrl, true);
    in = b.Build(3, true, true);
    // San Andreas's second jump and handbrake key, and (v23) a vehicle's fire key, all three as San Andreas binds it.
    CHECK(in.buttons == (Protocol::kButtonJump | Protocol::kButtonHandbrake | Protocol::kButtonVehicleFire));
    CHECK(ToPad(in, true).buttonCircle == 255);

    b.SetKey(kDikRightCtrl, false);
    b.SetKey(kDikCapsLock, true);
    in = b.Build(4, true, true);
    CHECK(ToPad(in, true).shockButtonL == 255);  // the horn
}

TEST_CASE("the modern layout: Shift sprints, Space jumps and is the handbrake")
{
    Builder b;
    b.SetKey(kDikLeftShift, true);
    auto in = b.Build(1, true, true);
    CHECK(in.buttons == Protocol::kButtonSprint);
    CHECK(ToPad(in, false).buttonCross == 255);
    CHECK(ToPad(in, true).rightShoulder1 == 0);

    // Mashing Shift: every press and release reaches San Andreas, which counts the taps itself.
    b.SetKey(kDikLeftShift, false);
    CHECK(ToPad(b.Build(2, true, true), false).buttonCross == 0);
    b.SetKey(kDikLeftShift, true);
    CHECK(ToPad(b.Build(3, true, true), false).buttonCross == 255);

    b.SetKey(kDikLeftShift, false);
    b.SetKey(kDikSpace, true);
    in = b.Build(4, true, true);
    CHECK(in.buttons == (Protocol::kButtonJump | Protocol::kButtonHandbrake));
    CHECK(ToPad(in, false).buttonSquare == 255);  // jumps on foot
    CHECK(ToPad(in, true).rightShoulder1 == 255);  // the handbrake in a vehicle
    CHECK(ToPad(in, true).buttonSquare == 0);     // without braking
}

TEST_CASE("the mouse buttons fire and aim on foot, and let go with everything else")
{
    Builder b;
    b.SetFire(true);
    auto s = b.Build(1, true, true);
    CHECK((s.buttons & Protocol::kButtonFire) != 0);
    CHECK((s.buttons & Protocol::kButtonAim) == 0);
    b.SetAim(true);
    s = b.Build(2, true, true);
    CHECK((s.buttons & Protocol::kButtonFire) != 0);
    CHECK((s.buttons & Protocol::kButtonAim) != 0);

    s = b.Build(3, true, false);  // a menu has the input: nobody fires into it
    CHECK(s.buttons == 0);

    b.SetFire(false);
    s = b.Build(4, true, true);
    CHECK((s.buttons & Protocol::kButtonFire) == 0);
    CHECK((s.buttons & Protocol::kButtonAim) != 0);
    b.ReleaseAll();
    CHECK(b.Build(5, true, true).buttons == 0);
}

TEST_CASE("fire and aim reach San Andreas's circle and right shoulder on foot, and a vehicle's two weapons in one")
{
    Protocol::InputState in{};
    in.buttons = Protocol::kButtonFire | Protocol::kButtonAim;
    auto pad = ToPad(in);
    CHECK(pad.buttonCircle == 255);
    CHECK(pad.rightShoulder1 == 255);
    CHECK(pad.leftShoulder1 == 0);

    pad = ToPad(in, true);  // in a vehicle the right shoulder is the handbrake, which is its own button
    CHECK(pad.buttonCircle == 255);   // v19: the vehicle's guns (CPad::GetCarGunFired's 1)
    CHECK(pad.leftShoulder1 == 255);  // and its second weapon (GetCarGunFired's 2)
    CHECK(pad.rightShoulder1 == 0);
    in.buttons = Protocol::kButtonHandbrake;
    CHECK(ToPad(in, true).rightShoulder1 == 255);
    CHECK(ToPad(in, false).rightShoulder1 == 0);
}

TEST_CASE("in a vehicle Q and E look left and right and the numpad is the right stick")
{
    CHECK(ActionFromScanCode(kDikQ) == Action::LookLeft);
    CHECK(ActionFromScanCode(kDikNumpad4) == Action::TurretLeft);
    CHECK(ActionFromScanCode(kDikNumpad6) == Action::TurretRight);
    CHECK(ActionFromScanCode(kDikNumpad8) == Action::TurretUp);
    CHECK(ActionFromScanCode(kDikPageUp) == Action::TurretUp);
    CHECK(ActionFromScanCode(kDikNumpad2) == Action::TurretDown);
    CHECK(ActionFromScanCode(kDikPageDown) == Action::TurretDown);

    Builder b;
    b.SetKey(kDikQ, true);
    b.SetKey(kDikNumpad8, true);
    b.SetKey(kDikNumpad6, true);
    auto in = b.Build(1, true, true);
    CHECK(in.buttons == (Protocol::kButtonLookLeft | Protocol::kButtonTurretUp | Protocol::kButtonTurretRight));
    auto pad = ToPad(in, true);
    CHECK(pad.leftShoulder2 == 255);   // a plane's rudder left
    CHECK(pad.rightShoulder2 == 0);
    CHECK(pad.rightStickY == -128);    // numpad 8 is the stick up
    CHECK(pad.rightStickX == 128);
    pad = ToPad(in, false);  // on foot none of them do anything
    CHECK(pad.leftShoulder2 == 0);
    CHECK(pad.rightStickX == 0);
    CHECK(pad.rightStickY == 0);

    b.SetKey(kDikQ, false);
    b.SetKey(kDikNumpad8, false);
    b.SetKey(kDikNumpad6, false);
    b.SetKey(kDikE, true);
    b.SetKey(kDikPageDown, true);
    b.SetKey(kDikNumpad4, true);
    pad = ToPad(b.Build(2, true, true), true);
    CHECK(pad.rightShoulder2 == 255);
    CHECK(pad.rightStickY == 128);
    CHECK(pad.rightStickX == -128);
    b.SetKey(kDikNumpad2, true);  // Page Down and numpad 2 together are still the stick down
    CHECK(ToPad(b.Build(3, true, true), true).rightStickY == 128);
}

TEST_CASE("' spawns the dev pick, once per press, and nothing without one")
{
    Builder b;
    b.SetKey(kDikApostrophe, true);
    CHECK(b.Build(1, true, true).spawnCount == 0);  // no pick yet
    b.SetKey(kDikApostrophe, false);
    b.SetDevModel(520);
    CHECK(b.DevModel() == 520);
    b.SetKey(kDikApostrophe, true);
    b.SetKey(kDikApostrophe, true);  // key repeat
    auto s = b.Build(2, true, true);
    CHECK(s.spawnModel == 520);
    CHECK(s.spawnCount == 1);
    b.SetKey(kDikApostrophe, false);
    b.SetDevModel(370);  // the jetpack goes the same way
    b.SetKey(kDikApostrophe, true);
    s = b.Build(3, true, true);
    CHECK(s.spawnModel == 370);
    CHECK(s.spawnCount == 2);
    CHECK(ActionFromScanCode(kDikApostrophe) == Action::None);
    CHECK(ActionFromScanCode(kDikSemicolon) == Action::None);
}

TEST_CASE("the debug key is = and nothing else maps to it")
{
    CHECK(kDikEquals == 0x0D);
    CHECK(ActionFromScanCode(kDikEquals) == Action::None);
    CHECK(SpawnModelFromScanCode(kDikEquals) == 0);
}

TEST_CASE("numpad 5 is the hydraulics' jump in a vehicle (v20)")
{
    Builder b;
    b.SetKey(kDikNumpad5, true);
    const auto s = b.Build(1, true, true);
    CHECK((s.buttons & AhShitRim::Protocol::kButtonHydraulics) != 0);
    CHECK(ToPad(s, true).shockButtonR == 255);
    CHECK(ToPad(s, false).shockButtonR == 0);  // on foot it does nothing
    CHECK(ActionFromScanCode(kDikNumpad5, KeyLayout::Classic) == Action::Hydraulics);
}

TEST_CASE("v23: San Andreas's keyboard fire keys join the mouse buttons in a vehicle")
{
    for (const auto layout : { KeyLayout::Modern, KeyLayout::Classic }) {
        CAPTURE(NameOf(layout));
        CHECK(VehicleActionFromScanCode(kDikLeftAlt) == Action::VehicleFire);
        CHECK(VehicleActionFromScanCode(kDikRightCtrl) == Action::VehicleFire);
        CHECK(VehicleActionFromScanCode(kDikLeftCtrl) == Action::VehicleFireAlt);
        CHECK(VehicleActionFromScanCode(kDikNumpad0) == Action::VehicleFireAlt);

        Builder b;
        b.SetLayout(layout);
        b.SetKey(kDikLeftAlt, true);
        auto in = b.Build(1, true, true);
        CHECK((in.buttons & Protocol::kButtonVehicleFire) != 0);
        CHECK(ToPad(in, true).buttonCircle == 255);  // the Hunter's rockets, the Hydra's flares, nitro
        CHECK(ToPad(in, false).buttonCircle == 0);   // on foot Left Alt only walks
        CHECK(ToPad(in, false).pedWalk == 255);

        b.SetKey(kDikLeftAlt, false);
        b.SetKey(kDikNumpad0, true);
        in = b.Build(2, true, true);
        CHECK(ToPad(in, true).leftShoulder1 == 255);  // the second weapon, a bicycle's bunny hop
        CHECK(ToPad(in, false).rightShoulder1 == 0);  // on foot it doesn't aim
        b.SetKey(kDikNumpad0, false);
        b.SetKey(kDikLeftCtrl, true);
        in = b.Build(3, true, true);
        CHECK(ToPad(in, true).leftShoulder1 == 255);
        CHECK(ToPad(in, true).shockButtonL == 0);  // not the horn
    }
    // Left Ctrl still crouches on foot in the modern layout.
    Builder b;
    b.SetKey(kDikLeftCtrl, true);
    const auto in = b.Build(1, true, true);
    CHECK(ToPad(in, false).shockButtonL == 255);
    CHECK(ToPad(in, false).leftShoulder1 == 0);
}

TEST_CASE("v23: the right mouse button and its keys are the same L1: the BMX's bunny hop")
{
    Builder b;
    b.SetAim(true);
    const auto mouse = ToPad(b.Build(1, true, true), true);
    b.SetAim(false);
    b.SetKey(kDikLeftCtrl, true);
    const auto key = ToPad(b.Build(2, true, true), true);
    CHECK(mouse.leftShoulder1 == 255);
    CHECK(key.leftShoulder1 == mouse.leftShoulder1);
    CHECK(key.buttonCircle == 0);
}

TEST_CASE("v23: the middle mouse button looks behind in a vehicle, both shoulders as San Andreas reads it")
{
    Builder b;
    b.SetLookBehind(true);
    auto in = b.Build(1, true, true);
    CHECK(in.buttons == Protocol::kButtonLookBehind);
    auto pad = ToPad(in, true);
    CHECK(pad.leftShoulder2 == 255);  // CPad::GetLookBehindForCar: L2 and R2 together
    CHECK(pad.rightShoulder2 == 255);
    pad = ToPad(in, false);
    CHECK(pad.leftShoulder2 == 0);
    CHECK(pad.rightShoulder2 == 0);
    b.ReleaseAll();
    CHECK(b.Build(2, true, true).buttons == 0);

    // Q and E together look behind too.
    b.SetKey(kDikQ, true);
    b.SetKey(kDikE, true);
    pad = ToPad(b.Build(3, true, true), true);
    CHECK(pad.leftShoulder2 == 255);
    CHECK(pad.rightShoulder2 == 255);

    b.ReleaseAll();
    b.SetLookBehind(true);
    CHECK(b.Build(4, true, false).buttons == 0);  // a menu has the input
}

TEST_CASE("v23: V and Home change the camera distance in a vehicle")
{
    CHECK(VehicleActionFromScanCode(kDikV) == Action::CameraView);
    CHECK(VehicleActionFromScanCode(kDikHome) == Action::CameraView);
    CHECK(ActionFromScanCode(kDikV) == Action::None);
    Builder b;
    b.SetKey(kDikHome, true);
    const auto in = b.Build(1, true, true);
    CHECK(in.buttons == Protocol::kButtonCameraView);
    CHECK(ToPad(in, true).select == 255);
    CHECK(ToPad(in, false).select == 0);
}

TEST_CASE("v23: 2 and numpad + are the sub-mission key with numpad 5: the hydraulics' jump, a plane's landing gear")
{
    for (const auto layout : { KeyLayout::Modern, KeyLayout::Classic }) {
        CHECK(ActionFromScanCode(kDik2, layout) == Action::Hydraulics);
        CHECK(ActionFromScanCode(kDikNumpadPlus, layout) == Action::Hydraulics);
    }
    Builder b;
    b.SetKey(kDik2, true);
    const auto in = b.Build(1, true, true);
    CHECK(in.buttons == Protocol::kButtonHydraulics);
    CHECK(ToPad(in, true).shockButtonR == 255);  // CPlane::ProcessControlInputs toggles the gear on its press
    CHECK(ToPad(in, true).shockButtonL == 0);    // not the horn
}

TEST_CASE("v23: Caps Lock is the horn in both layouts")
{
    Builder b;
    b.SetKey(kDikCapsLock, true);
    const auto in = b.Build(1, true, true);
    CHECK(in.buttons == Protocol::kButtonHorn);
    CHECK(ToPad(in, true).shockButtonL == 255);
    CHECK(ToPad(in, false).shockButtonL == 0);  // not a crouch on foot
}

TEST_CASE("v23: Insert and 4 tune the next station, Delete and R the one before")
{
    CHECK(RadioStepFromScanCode(kDikInsert) == 1);
    CHECK(RadioStepFromScanCode(kDik4) == 1);
    CHECK(RadioStepFromScanCode(kDikDelete) == -1);
    CHECK(RadioStepFromScanCode(kDikR) == -1);
    CHECK(RadioStepFromScanCode(kDikW) == 0);
    for (const auto dik : { kDikInsert, kDik4, kDikDelete, kDikR }) {
        CHECK(ActionFromScanCode(dik) == Action::None);  // a step each press, nothing held
        CHECK(VehicleActionFromScanCode(dik) == Action::None);
    }
}

TEST_CASE("v23: the handbrake key held is the Hydra's lock-on")
{
    Builder b;
    CHECK_FALSE(b.HandbrakeHeld());
    b.SetKey(kDikSpace, true);
    CHECK(b.HandbrakeHeld());
    b.SetKey(kDikSpace, false);
    b.SetKey(kDikRightCtrl, true);
    CHECK_FALSE(b.HandbrakeHeld());  // Right Ctrl is the handbrake in the classic layout only
    b.SetLayout(KeyLayout::Classic);
    CHECK(b.HandbrakeHeld());
}

TEST_CASE("v23: none of the new keys is one of Skyrim's own")
{
    for (const auto dik : { kDikNumpad0, kDikNumpadPlus, kDik2, kDik4, kDikR, kDikV, kDikHome, kDikInsert, kDikDelete, kDikLeftAlt, kDikRightCtrl, kDikLeftCtrl, kDikCapsLock }) {
        CHECK_FALSE(OpensSkyrimMenu(dik));
        CHECK(dik != kDikF10);
        CHECK(dik != kDikF11);
        CHECK(dik != kDikEquals);
        CHECK(dik != kDikSlash);
        CHECK(dik != kDikSemicolon);
        CHECK(dik != kDikApostrophe);
        CHECK(dik != kDikM);
        CHECK(dik != 0x3F);  // F5, quick save
        CHECK(dik != 0x43);  // F9, quick load
        CHECK(SpawnModelFromScanCode(dik) == 0);
    }
}
