#include <doctest/doctest.h>

#include "../../cpp/pad.h"

using namespace AhShitRim;
using namespace AhShitRim::Pad;

namespace
{
    constexpr float kFrame = 1.0f / 60.0f;

    Frame OnFoot(Mapper& a_pad, bool a_scoped = false) { return a_pad.Update(kFrame, Context::OnFoot, true, true, a_scoped); }
    Frame InCar(Mapper& a_pad) { return a_pad.Update(kFrame, Context::Vehicle, true, true, false); }

    bool Has(const Frame& a_frame, std::uint32_t a_button) { return (a_frame.actions.buttons & a_button) != 0; }
}

TEST_CASE("the pad on foot: San Andreas's console buttons, the triggers aim and fire")
{
    Mapper pad;
    pad.SetStick(false, 0.4f, 0.7f);
    pad.SetButton(kA, 1.0f);
    pad.SetButton(kX, 1.0f);
    pad.SetButton(kY, 1.0f);
    pad.SetButton(kLeftThumb, 1.0f);
    pad.SetButton(kLeftTrigger, 0.8f);
    pad.SetButton(kRightTrigger, 0.5f);
    const auto f = OnFoot(pad);
    CHECK(f.actions.moveX == doctest::Approx(0.4f));  // analog: a light push walks
    CHECK(f.actions.moveY == doctest::Approx(0.7f));
    CHECK(Has(f, Protocol::kButtonSprint));
    CHECK(Has(f, Protocol::kButtonJump));
    CHECK(Has(f, Protocol::kButtonEnterExit));
    CHECK(Has(f, Protocol::kButtonCrouch));
    CHECK(Has(f, Protocol::kButtonAim));
    CHECK(Has(f, Protocol::kButtonFire));
    CHECK_FALSE(Has(f, Protocol::kButtonAccelerate));  // the vehicle's meanings stay out on foot
    CHECK_FALSE(Has(f, Protocol::kButtonHorn));

    SUBCASE("a trigger barely touched holds nothing")
    {
        pad.SetButton(kLeftTrigger, 0.1f);
        pad.SetButton(kRightTrigger, 0.0f);
        const auto g = OnFoot(pad);
        CHECK_FALSE(Has(g, Protocol::kButtonAim));
        CHECK_FALSE(Has(g, Protocol::kButtonFire));
    }
}

TEST_CASE("the pad on foot: LB activates, R3 insults and the d-pad steps the weapons, once per press")
{
    Mapper pad;
    pad.SetButton(kLeftShoulder, 1.0f);
    pad.SetButton(kRightThumb, 1.0f);
    pad.SetButton(kRight, 1.0f);
    auto f = OnFoot(pad);
    CHECK(f.activate);
    CHECK(f.insult);
    CHECK(f.nextSteps == 1);
    CHECK(f.prevSteps == 0);
    // Still held next frame: nothing more.
    f = OnFoot(pad);
    CHECK_FALSE(f.activate);
    CHECK_FALSE(f.insult);
    CHECK(f.nextSteps == 0);
    pad.SetButton(kRight, 0.0f);
    pad.SetButton(kLeft, 1.0f);
    f = OnFoot(pad);
    CHECK(f.prevSteps == 1);
    // The d-pad's up and down are kept for the crew.
    pad.SetButton(kUp, 1.0f);
    pad.SetButton(kDown, 1.0f);
    f = OnFoot(pad);
    CHECK(f.nextSteps == 0);
    CHECK(f.prevSteps == 0);
}

TEST_CASE("the pad in a vehicle: the triggers drive, the d-pad tunes the radio and looks aside")
{
    Mapper pad;
    pad.SetStick(false, -0.6f, 0.9f);
    pad.SetButton(kRightTrigger, 1.0f);
    pad.SetButton(kLeftTrigger, 0.5f);
    pad.SetButton(kA, 1.0f);
    pad.SetButton(kLeftShoulder, 1.0f);
    pad.SetButton(kRightShoulder, 1.0f);
    pad.SetButton(kX, 1.0f);
    pad.SetButton(kY, 1.0f);
    pad.SetButton(kLeftThumb, 1.0f);
    pad.SetButton(kUp, 1.0f);
    pad.SetButton(kLeft, 1.0f);
    const auto f = InCar(pad);
    CHECK(f.actions.moveX == doctest::Approx(-0.6f));  // steering, analog
    CHECK(f.actions.moveY == 0.0f);                    // the stick's up leans; it never accelerates
    CHECK(Has(f, Protocol::kButtonAccelerate));
    CHECK(Has(f, Protocol::kButtonBrake));  // both: San Andreas's burnout
    CHECK(Has(f, Protocol::kButtonLeanForward));
    CHECK(Has(f, Protocol::kButtonVehicleFire));
    CHECK(Has(f, Protocol::kButtonVehicleFireAlt));
    CHECK(Has(f, Protocol::kButtonHandbrake));
    CHECK(Has(f, Protocol::kButtonLookBehind));
    CHECK(Has(f, Protocol::kButtonEnterExit));
    CHECK(Has(f, Protocol::kButtonHorn));
    CHECK(Has(f, Protocol::kButtonLookLeft));
    CHECK(f.nextSteps == 1);  // a station, not a weapon
    CHECK_FALSE(f.activate);  // LB is the second weapon here
    CHECK_FALSE(Has(f, Protocol::kButtonSprint));
    CHECK_FALSE(Has(f, Protocol::kButtonFire));  // RT accelerates, it doesn't fire

    SUBCASE("the stick down leans back")
    {
        pad.SetStick(false, 0.0f, -0.8f);
        const auto g = InCar(pad);
        CHECK(Has(g, Protocol::kButtonLeanBack));
        CHECK_FALSE(Has(g, Protocol::kButtonLeanForward));
    }
}

TEST_CASE("R3 held makes the right stick the turret keys, which bounce hydraulics; the Hydra's nozzles are its up and down")
{
    Mapper pad;
    pad.SetStick(true, 0.9f, -0.9f);
    auto f = InCar(pad);
    CHECK(f.mouseX > 0.0);  // the camera, as the mouse
    CHECK(f.mouseY > 0.0);  // the stick down looks down
    CHECK_FALSE(Has(f, Protocol::kButtonTurretRight));

    pad.SetButton(kRightThumb, 1.0f);
    f = InCar(pad);
    CHECK(Has(f, Protocol::kButtonHydraulics));
    CHECK(Has(f, Protocol::kButtonTurretRight));
    CHECK(Has(f, Protocol::kButtonTurretDown));
    CHECK(f.mouseX == 0.0);
    CHECK(f.mouseY == 0.0);

    pad.SetButton(kRightThumb, 0.0f);
    pad.SetStick(true, 0.9f, 0.9f);
    f = pad.Update(kFrame, Context::Hydra, true, true, false);
    CHECK(Has(f, Protocol::kButtonTurretUp));
    CHECK_FALSE(Has(f, Protocol::kButtonTurretRight));  // left and right still look round
    CHECK(f.mouseX > 0.0);
    CHECK(f.mouseY == 0.0);
}

TEST_CASE("the right stick turns the camera as mouse counts: a dead zone, a curve, slower aiming and through a scope")
{
    Look look;
    CHECK(Curve(0.1f, look.deadZone) == 0.0f);
    CHECK(Curve(1.0f, look.deadZone) == doctest::Approx(1.0f));
    CHECK(Curve(-1.0f, look.deadZone) == doctest::Approx(-1.0f));
    CHECK(Curve(0.575f, look.deadZone) == doctest::Approx(0.25f));  // half way past the dead zone: a quarter of the speed

    Mapper pad;
    pad.SetStick(true, 1.0f, 1.0f);
    auto f = pad.Update(1.0f, Context::OnFoot, true, true, false, look);
    CHECK(f.mouseX == doctest::Approx(look.lookSpeed));
    CHECK(f.mouseY == doctest::Approx(-look.lookSpeed * look.vertical));  // up looks up: the mouse moving up

    pad.SetButton(kLeftTrigger, 1.0f);
    f = pad.Update(1.0f, Context::OnFoot, true, true, false, look);
    CHECK(f.mouseX == doctest::Approx(look.lookSpeed * look.aimShare));
    f = pad.Update(1.0f, Context::OnFoot, true, true, true, look);
    CHECK(f.mouseX == doctest::Approx(look.lookSpeed * look.scopeShare));

    look.invertY = true;
    f = pad.Update(1.0f, Context::OnFoot, true, true, false, look);
    CHECK(f.mouseY > 0.0);
}

TEST_CASE("Back: a tap is the camera key with CJ and Skyrim's wait menu without him, a second's hold the handover")
{
    Mapper pad;
    pad.SetButton(kBack, 1.0f);
    auto f = OnFoot(pad);
    CHECK_FALSE(f.handover);
    CHECK_FALSE(Has(f, Protocol::kButtonCameraView));  // not before the release: it might be a hold
    pad.SetButton(kBack, 0.0f);
    f = OnFoot(pad);
    CHECK(Has(f, Protocol::kButtonCameraView));
    CHECK_FALSE(f.waitTap);
    // Held for a few frames, so San Andreas sees it, then let go.
    for (int i = 0; i < 20; ++i) {
        f = OnFoot(pad);
    }
    CHECK_FALSE(Has(f, Protocol::kButtonCameraView));

    SUBCASE("held a second: the handover, once, and no tap on the release")
    {
        pad.SetButton(kBack, 1.0f);
        int handovers = 0;
        for (int i = 0; i < 90; ++i) {
            handovers += OnFoot(pad).handover ? 1 : 0;
        }
        CHECK(handovers == 1);
        pad.SetButton(kBack, 0.0f);
        f = OnFoot(pad);
        CHECK_FALSE(Has(f, Protocol::kButtonCameraView));
    }

    SUBCASE("while Skyrim has the player: the tap opens its wait menu, the hold hands the player to CJ")
    {
        pad.SetButton(kBack, 1.0f);
        f = pad.Update(kFrame, Context::OnFoot, false, true, false);
        pad.SetButton(kBack, 0.0f);
        f = pad.Update(kFrame, Context::OnFoot, false, true, false);
        CHECK(f.waitTap);
        CHECK_FALSE(Has(f, Protocol::kButtonCameraView));

        pad.SetButton(kBack, 1.0f);
        bool handover = false;
        for (int i = 0; i < 70 && !handover; ++i) {
            handover = pad.Update(kFrame, Context::OnFoot, false, false, false).handover;  // a menu open: the hold still counts
        }
        CHECK(handover);
    }
}

TEST_CASE("the pad does nothing for CJ while Skyrim has the player or a menu has the input")
{
    Mapper pad;
    pad.SetStick(false, 1.0f, 1.0f);
    pad.SetStick(true, 1.0f, 0.0f);
    pad.SetButton(kA, 1.0f);
    pad.SetButton(kLeftShoulder, 1.0f);
    auto f = pad.Update(kFrame, Context::OnFoot, true, false, false);
    CHECK(f.actions.buttons == 0);
    CHECK(f.actions.moveY == 0.0f);
    CHECK(f.mouseX == 0.0);
    CHECK_FALSE(f.activate);  // the press is dropped, not kept for later
    f = OnFoot(pad);
    CHECK_FALSE(f.activate);
    CHECK(Has(f, Protocol::kButtonSprint));  // held is held
    f = pad.Update(kFrame, Context::OnFoot, false, true, false);
    CHECK(f.actions.buttons == 0);
}

TEST_CASE("the input builder merges the pad with the keyboard")
{
    Input::Builder builder;
    Input::PadActions pad;
    pad.moveX = 0.5f;
    pad.moveY = 0.25f;
    pad.buttons = Protocol::kButtonAim | Protocol::kButtonHandbrake | Protocol::kButtonHorn;
    builder.SetPad(pad);
    auto s = builder.Build(1, true, true);
    CHECK(s.moveX == doctest::Approx(0.5f));
    CHECK(s.moveY == doctest::Approx(0.25f));
    CHECK((s.buttons & Protocol::kButtonAim) != 0);
    CHECK(builder.Aiming());
    CHECK(builder.HandbrakeHeld());
    CHECK(builder.HornHeld());
    // A key held wins its axis.
    builder.SetKey(Input::kDikW, true);
    s = builder.Build(2, true, true);
    CHECK(s.moveY == 1.0f);
    CHECK(s.moveX == doctest::Approx(0.5f));
    // A menu: nothing.
    s = builder.Build(3, true, false);
    CHECK(s.buttons == 0);
    CHECK(s.moveX == 0.0f);
    builder.AddMouseCounts(10.5, -2.25);
    s = builder.Build(4, true, true);
    CHECK(s.mouseX == doctest::Approx(10.5));
    CHECK(s.mouseY == doctest::Approx(-2.25));
    builder.ReleaseAll();
    CHECK_FALSE(builder.Aiming());
}
