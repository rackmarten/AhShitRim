#include <doctest/doctest.h>

#include "../../cpp/parachute.h"

#include <ostream>
#include <string>

using namespace AhShitRim::Parachute;

namespace
{
    Around Falling(float a_fall, float a_height)
    {
        Around a;
        a.holdsChute = true;
        a.fall = a_fall;
        a.height = a_height;
        return a;
    }

    State Run(State a_state, const Around& a_around, const Keys& a_keys, float a_seconds)
    {
        for (float t = 0.0f; t < a_seconds; t += 1.0f / 30.0f) {
            a_state = Step(a_state, a_around, a_keys, 1.0f / 30.0f);
        }
        return a_state;
    }
}

TEST_CASE("parachute: a fall with the parachute and air under him starts a skydive")
{
    State s;
    CHECK(Step(s, Falling(2.0f, 100.0f), {}, 0.03f).phase == Phase::Off);  // a hop
    CHECK(Step(s, Falling(8.0f, 5.0f), {}, 0.03f).phase == Phase::Off);    // off a wall: too low
    Around noChute = Falling(8.0f, 100.0f);
    noChute.holdsChute = false;
    CHECK(Step(s, noChute, {}, 0.03f).phase == Phase::Off);
    Around standing = Falling(8.0f, 100.0f);
    standing.standing = true;
    CHECK(Step(s, standing, {}, 0.03f).phase == Phase::Off);
    s = Step(s, Falling(8.0f, 100.0f), {}, 0.03f);
    CHECK(s.phase == Phase::Freefall);
    CHECK(s.fall == doctest::Approx(8.0f));
    CHECK(Flying(s.phase));
    CHECK_FALSE(CanopyOut(s.phase));
}

TEST_CASE("parachute: the freefall reaches its speed, faster diving, and turns with A and D")
{
    State s = Step({}, Falling(8.0f, 500.0f), {}, 0.03f);
    s = Run(s, Falling(0.0f, 500.0f), {}, 5.0f);
    CHECK(s.fall == doctest::Approx(kFreefallFall));
    CHECK(s.forward == doctest::Approx(kFreefallForward).epsilon(0.05));
    CHECK(s.turn == 0.0f);

    Keys dive;
    dive.pitch = 1.0f;
    s = Run(s, Falling(0.0f, 500.0f), dive, 5.0f);
    CHECK(s.fall == doctest::Approx(kDiveFall));
    CHECK(s.forward == doctest::Approx(kDiveForward).epsilon(0.05));
    // Out of the dive the air brakes him back to the flat speed, not at once.
    s = Step(s, Falling(0.0f, 500.0f), {}, 1.0f / 30.0f);
    CHECK(s.fall < kDiveFall);
    CHECK(s.fall > kFreefallFall);

    Keys left;
    left.steer = -1.0f;
    s = Step(s, Falling(0.0f, 500.0f), left, 1.0f / 30.0f);
    CHECK(s.turn == doctest::Approx(-kFreefallTurn));
}

TEST_CASE("parachute: the fire key opens the chute, which brakes him to a glide")
{
    State s = Run(Step({}, Falling(8.0f, 500.0f), {}, 0.03f), Falling(0.0f, 500.0f), {}, 3.0f);
    Keys open;
    open.open = true;
    s = Step(s, Falling(0.0f, 400.0f), open, 1.0f / 30.0f);
    CHECK(s.phase == Phase::Opening);
    CHECK(CanopyOut(s.phase));
    s = Run(s, Falling(0.0f, 400.0f), {}, kOpenSeconds + 0.1f);
    CHECK(s.phase == Phase::Floating);
    s = Run(s, Falling(0.0f, 300.0f), {}, 3.0f);
    CHECK(s.fall == doctest::Approx(kFloatFall).epsilon(0.02));
    CHECK(s.forward == doctest::Approx(kFloatForward).epsilon(0.02));

    Keys flare;
    flare.pitch = -1.0f;
    s = Run(s, Falling(0.0f, 300.0f), flare, 3.0f);
    CHECK(s.fall == doctest::Approx(kFlareFall).epsilon(0.02));
    CHECK(s.fall < kFloatFall);

    Keys right;
    right.steer = 1.0f;
    s = Step(s, Falling(0.0f, 300.0f), right, 1.0f / 30.0f);
    CHECK(s.turn == doctest::Approx(kFloatTurn));
}

TEST_CASE("parachute: touching down lands him, then the parachute is used up")
{
    State s;
    s.phase = Phase::Floating;
    s.fall = kFloatFall;
    Around ground = Falling(kFloatFall, 0.0f);
    ground.standing = true;
    s = Step(s, ground, {}, 1.0f / 30.0f);
    CHECK(s.phase == Phase::Landing);
    CHECK(s.fall == 0.0f);
    CHECK_FALSE(Flying(s.phase));
    CHECK(CanopyOut(s.phase));
    s = Run(s, ground, {}, kLandSeconds - 0.2f);
    CHECK(s.phase == Phase::Landing);
    bool used = false;
    for (int i = 0; i < 30 && !used; i++) {
        s = Step(s, ground, {}, 1.0f / 30.0f);
        used = s.usedUp;
    }
    CHECK(used);
    CHECK(s.phase == Phase::Off);
    CHECK_FALSE(Step(s, ground, {}, 1.0f / 30.0f).usedUp);  // once

    // Hitting the ground with it shut: San Andreas's own fall takes over.
    State freefall;
    freefall.phase = Phase::Freefall;
    CHECK(Step(freefall, ground, {}, 1.0f / 30.0f).phase == Phase::Off);

    // A vehicle, the jetpack, a scene or Wasted ends it at once, the parachute kept.
    Around busy = Falling(5.0f, 100.0f);
    busy.free = false;
    const auto stopped = Step(freefall, busy, {}, 1.0f / 30.0f);
    CHECK(stopped.phase == Phase::Off);
    CHECK_FALSE(stopped.usedUp);
}

TEST_CASE("parachute: its animations from parachute.ifp")
{
    Keys none, dive, left, right, flare;
    dive.pitch = 1.0f;
    left.steer = -1.0f;
    right.steer = 1.0f;
    flare.pitch = -1.0f;
    CHECK(std::string(AnimOf(Phase::Freefall, none)) == "FALL_skyDive");
    CHECK(std::string(AnimOf(Phase::Freefall, dive)) == "FALL_SkyDive_Accel");
    CHECK(std::string(AnimOf(Phase::Freefall, left)) == "FALL_SkyDive_L");
    CHECK(std::string(AnimOf(Phase::Freefall, right)) == "FALL_SkyDive_R");
    CHECK(std::string(AnimOf(Phase::Opening, dive)) == "PARA_open");
    CHECK(std::string(AnimOf(Phase::Floating, none)) == "PARA_float");
    CHECK(std::string(AnimOf(Phase::Floating, flare)) == "PARA_decel");
    CHECK(std::string(AnimOf(Phase::Floating, left)) == "PARA_steerL");
    CHECK(std::string(AnimOf(Phase::Floating, right)) == "PARA_steerR");
    CHECK(std::string(AnimOf(Phase::Landing, none)) == "PARA_Land");
    CHECK(AnimOf(Phase::Off, none).empty());
    CHECK(Loops(Phase::Freefall));
    CHECK(Loops(Phase::Floating));
    CHECK_FALSE(Loops(Phase::Opening));
    CHECK_FALSE(Loops(Phase::Landing));
}

TEST_CASE("parachute: what each of CJ's animations is held at during a skydive")
{
    constexpr int kPlayerGroup = 54;  // his movement group, as the log showed it
    CHECK(HoldOf(true, 65535, -1, false, kPlayerGroup) == Hold::Full);
    CHECK(HoldOf(false, 0, 120, true, kPlayerGroup) == Hold::None);  // FALL_FALL, partial: the flailing
    CHECK(HoldOf(false, 0, 121, true, kPlayerGroup) == Hold::None);  // FALL_GLIDE
    CHECK(HoldOf(false, 0, 160, true, kPlayerGroup) == Hold::Keep);  // FACTALK: his face talking
    CHECK(HoldOf(false, 0, 154, true, kPlayerGroup) == Hold::Keep);
    CHECK(HoldOf(false, 0, 162, true, kPlayerGroup) == Hold::None);
    CHECK(HoldOf(false, kPlayerGroup, 3, false, kPlayerGroup) == Hold::Idle);
    CHECK(HoldOf(false, 0, 3, false, kPlayerGroup) == Hold::None);  // another group's idle
    CHECK(HoldOf(false, kPlayerGroup, 0, false, kPlayerGroup) == Hold::None);  // walking
    CHECK(kIdleBlend > 0.0f);
    CHECK(kIdleBlend < 0.05f);
}
