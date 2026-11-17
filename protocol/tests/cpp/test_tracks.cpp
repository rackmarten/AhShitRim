#include <doctest/doctest.h>

#include <cmath>

#include "../../cpp/tracks.h"

using namespace AhShitRim;

namespace
{
    std::array<float, Tracks::kLines> Lines(float a_all)
    {
        std::array<float, Tracks::kLines> lines{};
        lines.fill(a_all);
        return lines;
    }
}

TEST_CASE("each corner's track lines are its side's two at its end")
{
    CHECK(Tracks::LinesFor(0) == std::array<int, 2>{ 0, 1 });  // front left
    CHECK(Tracks::LinesFor(1) == std::array<int, 2>{ 3, 2 });  // rear left
    CHECK(Tracks::LinesFor(2) == std::array<int, 2>{ 4, 5 });  // front right
    CHECK(Tracks::LinesFor(3) == std::array<int, 2>{ 7, 6 });  // rear right
}

TEST_CASE("a track line grips only for a corner in the air past San Andreas's grace")
{
    auto lines = Lines(1.0f);
    lines[1] = 0.9f;
    CHECK(Tracks::GripLine(0, 0.95f, 4.0f, lines) == -1);  // the wheel is down: San Andreas grips through it
    CHECK(Tracks::GripLine(0, 1.0f, 2.0f, lines) == -1);   // just left the ground: San Andreas still grips through it
    CHECK(Tracks::GripLine(0, 1.0f, 0.0f, lines) == 1);    // in the air: its side's line touches
    CHECK(Tracks::GripLine(2, 1.0f, 0.0f, lines) == -1);   // the other side's lines are in the air too
    CHECK(Tracks::GripLine(1, 1.0f, 0.0f, lines) == -1);   // line 1 isn't one of the rear wheel's
    lines[0] = 0.8f;
    CHECK(Tracks::GripLine(0, 1.0f, 0.0f, lines) == 0);  // the more compressed one
    CHECK(Tracks::GripLine(-1, 1.0f, 0.0f, lines) == -1);
    CHECK(Tracks::GripLine(4, 1.0f, 0.0f, lines) == -1);
}

TEST_CASE("adhesion is San Andreas's for the Rhino's handling")
{
    // RHINO: traction 2.5, traction bias 0.5, suspension force 0.4, bias 0.5; on tarmac's limit of 1, at 30 fps
    // (1.667 steps a frame), its spring 10% in: 1 x 0.0025 x 1 x min(1 x 0.4 x 4 x 0.1, 2) x 1.667.
    CHECK(Tracks::Adhesion(1.0f, 2.5f, 0.5f, 0.5f, 0.4f, true, 0.9f, 1.6667f) == doctest::Approx(0.0025f * 0.16f * 1.6667f));
    CHECK(Tracks::Adhesion(1.0f, 2.5f, 0.5f, 0.5f, 0.4f, false, 0.9f, 1.6667f) == doctest::Approx(0.0025f * 0.16f * 1.6667f));
    // Pressed in hard, the press caps at 2.
    CHECK(Tracks::Adhesion(1.0f, 2.5f, 0.5f, 0.5f, 4.0f, true, 0.0f, 1.0f) == doctest::Approx(0.0025f * 2.0f));
    // Fully relaxed: nothing presses it down, no grip.
    CHECK(Tracks::Adhesion(1.0f, 2.5f, 0.5f, 0.5f, 0.4f, true, 1.0f, 1.0f) == 0.0f);
    // A rear-biased axle grips more at the back.
    CHECK(Tracks::Adhesion(1.0f, 2.5f, 0.3f, 0.5f, 0.4f, false, 0.9f, 1.0f) > Tracks::Adhesion(1.0f, 2.5f, 0.3f, 0.5f, 0.4f, true, 0.9f, 1.0f));
}

TEST_CASE("gripping takes the slide off as ProcessWheel does, shared and capped")
{
    CHECK(Tracks::SideChange(0.02f, 2, 1.0f) == doctest::Approx(-0.01f));   // its share of the slide
    CHECK(Tracks::SideChange(-0.02f, 1, 1.0f) == doctest::Approx(0.02f));
    CHECK(Tracks::SideChange(0.2f, 1, 0.005f) == doctest::Approx(-0.005f));  // at most its adhesion
    CHECK(Tracks::SideChange(-0.2f, 4, 0.005f) == doctest::Approx(0.005f));
    CHECK(Tracks::SideChange(0.02f, 0, 1.0f) == doctest::Approx(-0.02f));   // never a share of nothing
    CHECK(Tracks::SideChange(0.0f, 2, 1.0f) == 0.0f);
    CHECK(Tracks::SideChange(0.02f, 2, 0.0f) == 0.0f);
}

TEST_CASE("without the handbrake both axles steer to San Andreas's own turning circle")
{
    const float lock = Tracks::SteeringLock(35.0f, false);
    CHECK(lock == doctest::Approx(19.29f).epsilon(0.001));
    // Front and rear together turn as tightly as the front alone did at 35 degrees.
    CHECK(2.0f * std::tan(lock / 57.29578f) == doctest::Approx(std::tan(35.0f / 57.29578f)));
    CHECK(Tracks::SteeringLock(35.0f, true) == 35.0f);  // the handbrake: San Andreas's full lock, its tighter turn
    CHECK(Tracks::SteeringLock(0.0f, false) == 0.0f);
}

TEST_CASE("the rear wheels counter-steer after San Andreas eases them")
{
    // San Andreas eases m_f2ndSteerAngle to 0.9 of itself before the next step: it ends up at -front.
    CHECK(Tracks::RearSteer(0.3f, false, 0.0f) * 0.9f == doctest::Approx(-0.3f));
    CHECK(Tracks::RearSteer(-0.2f, false, 0.5f) * 0.9f == doctest::Approx(0.2f));
    CHECK(Tracks::RearSteer(0.3f, true, 0.12f) == 0.12f);  // the handbrake: San Andreas's own
}

TEST_CASE("only vehicles heavier than 8 t keep less of a hull hit")
{
    CHECK(Tracks::HeavyKeep(25000.0f) == doctest::Approx(0.32f));  // the Rhino
    CHECK(Tracks::HeavyKeep(20000.0f) == doctest::Approx(0.4f));   // the Dumper
    CHECK(Tracks::HeavyKeep(8000.0f) == 1.0f);                     // a Packer
    CHECK(Tracks::HeavyKeep(5000.0f) == 1.0f);                     // the monster truck
    CHECK(Tracks::HeavyKeep(1400.0f) == 1.0f);
}

TEST_CASE("a hull hit's push up and its spin are cut, nothing else")
{
    // Thrown up from resting: a third of it.
    CHECK(Tracks::SettleUp(0.0f, 0.06f, 0.32f) == doctest::Approx(0.0192f));
    // Thrown up while falling: only the part above zero.
    CHECK(Tracks::SettleUp(-0.02f, 0.04f, 0.32f) == doctest::Approx(0.0128f));
    // Climbing already, thrown harder: only the gain.
    CHECK(Tracks::SettleUp(0.02f, 0.05f, 0.32f) == doctest::Approx(0.0296f));
    // Falling, or slowed: San Andreas's.
    CHECK(Tracks::SettleUp(0.0f, -0.03f, 0.32f) == -0.03f);
    CHECK(Tracks::SettleUp(0.05f, 0.02f, 0.32f) == 0.02f);
    // A light vehicle: all of it.
    CHECK(Tracks::SettleUp(0.0f, 0.06f, 1.0f) == 0.06f);
    // Spin gained either way is cut; spin slowing is kept.
    CHECK(Tracks::Settle(0.0f, -0.01f, 0.32f) == doctest::Approx(-0.0032f));
    CHECK(Tracks::Settle(0.005f, -0.01f, 0.32f) == doctest::Approx(-0.0032f));
    CHECK(Tracks::Settle(0.01f, 0.004f, 0.32f) == 0.004f);
    CHECK(Tracks::Settle(-0.01f, -0.004f, 0.32f) == -0.004f);
}
