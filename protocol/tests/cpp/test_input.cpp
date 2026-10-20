#include <doctest/doctest.h>

#include "../../cpp/input.h"

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
    CHECK(ActionFromScanCode(0x12) == Action::None);  // E: nothing on foot yet
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
    CHECK(s.buttons == (Protocol::kButtonSprint | Protocol::kButtonJump | Protocol::kButtonCrouch | Protocol::kButtonWalk));

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
    in.buttons = Protocol::kButtonJump | Protocol::kButtonHorn | Protocol::kButtonCrouch | Protocol::kButtonSprint;
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
