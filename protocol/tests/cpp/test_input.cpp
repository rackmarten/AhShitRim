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
    CHECK(ActionFromScanCode(kDikC) == Action::Crouch);
    CHECK(ActionFromScanCode(kDikLeftAlt) == Action::Walk);
}

TEST_CASE("keys that stay with Skyrim never reach San Andreas")
{
    CHECK(ActionFromScanCode(kDikEscape) == Action::None);
    CHECK(ActionFromScanCode(kDikTab) == Action::None);
    CHECK(ActionFromScanCode(kDikM) == Action::None);
    CHECK(ActionFromScanCode(kDikGrave) == Action::None);
    CHECK(ActionFromScanCode(kDikF10) == Action::None);
    CHECK(ActionFromScanCode(kDikE) == Action::None);  // E activates on Skyrim's side
    CHECK(ActionFromScanCode(kDikE, KeyLayout::Classic) == Action::None);
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
    b.SetKey(kDikC, true);
    b.SetKey(kDikLeftAlt, true);
    s = b.Build(9, true, true);
    CHECK(s.moveY == 0.0f);
    CHECK(s.buttons == (Protocol::kButtonSprint | Protocol::kButtonJump | Protocol::kButtonCrouch | Protocol::kButtonWalk | Protocol::kButtonHandbrake));

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

TEST_CASE("each swear press counts, also while a menu has the input, and picks the next bleeped line")
{
    Builder b;
    CHECK(b.Build(1, true, true).swearCount == 0);
    b.AddSwear();
    b.AddSwear();
    CHECK(b.Build(2, true, true).swearCount == 2);
    CHECK(b.Build(3, true, false).swearCount == 2);  // the count carries over; San Andreas decides what a change means
    CHECK(std::string(SwearSubtitle(1)) != SwearSubtitle(2));
    CHECK(std::string(SwearSubtitle(0)) == SwearSubtitle(5));  // round the lines in turn
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
    // Everything else as in the modern layout.
    CHECK(ActionFromScanCode(kDikW, classic) == Action::Forward);
    CHECK(ActionFromScanCode(kDikC, classic) == Action::Crouch);
    CHECK(ActionFromScanCode(kDikLeftAlt, classic) == Action::Walk);
    CHECK(ActionFromScanCode(kDikF, classic) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikReturn, classic) == Action::EnterExit);
    CHECK(ActionFromScanCode(kDikUp, classic) == Action::LeanForward);
    // The modern layout leaves Right Ctrl and Caps Lock to Skyrim.
    CHECK(ActionFromScanCode(kDikRightCtrl) == Action::None);
    CHECK(ActionFromScanCode(kDikCapsLock) == Action::None);

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
    CHECK(in.buttons == (Protocol::kButtonJump | Protocol::kButtonHandbrake));  // San Andreas's second jump and handbrake key

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

TEST_CASE("fire and aim reach San Andreas's circle and right shoulder on foot, and only on foot")
{
    Protocol::InputState in{};
    in.buttons = Protocol::kButtonFire | Protocol::kButtonAim;
    auto pad = ToPad(in);
    CHECK(pad.buttonCircle == 255);
    CHECK(pad.rightShoulder1 == 255);

    pad = ToPad(in, true);  // in a vehicle the right shoulder is the handbrake, which is its own button
    CHECK(pad.buttonCircle == 0);
    CHECK(pad.rightShoulder1 == 0);
    in.buttons = Protocol::kButtonHandbrake;
    CHECK(ToPad(in, true).rightShoulder1 == 255);
    CHECK(ToPad(in, false).rightShoulder1 == 0);
}

TEST_CASE("the debug key is = and nothing else maps to it")
{
    CHECK(kDikEquals == 0x0D);
    CHECK(ActionFromScanCode(kDikEquals) == Action::None);
    CHECK(SpawnModelFromScanCode(kDikEquals) == 0);
}
