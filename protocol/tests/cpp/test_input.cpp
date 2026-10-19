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
    CHECK(ActionFromScanCode(kDikUp) == Action::Forward);
    CHECK(ActionFromScanCode(kDikDown) == Action::Back);
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

    b.SetKey(kDikDown, true);  // forwards and back cancel
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
