#include <doctest/doctest.h>

#include "../../cpp/scene.h"

#include <cmath>

using namespace AhShitRim;

TEST_CASE("CJ moves the player unless Skyrim takes it: a scene, a seat or a bed")
{
    using Scene::Classify;
    using Scene::Control;
    CHECK(Classify({}) == Control::Cj);
    CHECK(Classify({ 1, true, false }) == Control::Seated);  // sitting down
    CHECK(Classify({ 3, true, false }) == Control::Seated);  // seated, a carriage too
    CHECK(Classify({ 4, true, false }) == Control::Seated);  // standing up
    CHECK(Classify({ 3, false, true }) == Control::Seated);  // a seat beats a scene: CJ sits
    CHECK(Classify({ 7, true, false }) == Control::Scripted);  // in bed
    CHECK(Classify({ 0, false, false }) == Control::Scripted);  // DisablePlayerControls
    CHECK(Classify({ 0, true, true }) == Control::Scripted);    // SetPlayerAIDriven: a package walks the player
}

TEST_CASE("the scene goes into the input, and facing whoever talks only outside one")
{
    Protocol::InputState input{};
    const float          offset[3] = { 10.0f, -20.0f, 30.0f };
    Scene::Follow(input, Scene::Control::Cj, offset, 1.0f);
    CHECK(input.flags == 0);

    Scene::Follow(input, Scene::Control::Seated, offset, 1.5f);
    CHECK((input.flags & Protocol::kInputScripted) != 0);
    CHECK((input.flags & Protocol::kInputSeated) != 0);
    CHECK(input.sceneOffset[1] == doctest::Approx(-20.0f));
    CHECK(input.sceneHeading == doctest::Approx(1.5f));
    Scene::Face(input, 0.0f, 0.0f, 100.0f, 0.0f);
    CHECK((input.flags & Protocol::kInputFace) == 0);
    CHECK(input.sceneHeading == doctest::Approx(1.5f));

    Protocol::InputState talk{};
    Scene::Face(talk, 0.0f, 0.0f, 100.0f, 0.0f);  // the speaker due east
    CHECK((talk.flags & Protocol::kInputFace) != 0);
    CHECK(talk.sceneHeading == doctest::Approx(Pose::kPi / 2.0f));
    Protocol::InputState same{};
    Scene::Face(same, 5.0f, 5.0f, 5.2f, 5.0f);  // standing on the spot: no heading to take
    CHECK(same.flags == 0);
}

TEST_CASE("a shout is let out when Skyrim's voice cooldown starts")
{
    CHECK(Scene::ShoutReleased(0.0f, 5.0f));
    CHECK_FALSE(Scene::ShoutReleased(4.9f, 5.0f));  // cooling down
    CHECK_FALSE(Scene::ShoutReleased(0.1f, 0.0f));  // cooled down
    CHECK_FALSE(Scene::ShoutReleased(0.0f, 0.0f));
}

TEST_CASE("CJ turns to whoever talks the short way round, a step at a time")
{
    CHECK(Scene::TurnToward(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
    CHECK(Scene::TurnToward(0.0f, -1.0f, 0.25f) == doctest::Approx(-0.25f));
    CHECK(Scene::TurnToward(0.9f, 1.0f, 0.25f) == doctest::Approx(1.0f));
    // Across the back, from 170 degrees to -170, turning on through 180.
    const float from = Pose::kPi * 170.0f / 180.0f;
    const float h = Scene::TurnToward(from, -from, 0.1f);
    CHECK(std::abs(h) > from);
    CHECK(h <= Pose::kPi);
    const float to = Scene::TurnToward(Scene::TurnToward(Scene::TurnToward(from, -from, 0.2f), -from, 0.2f), -from, 0.2f);
    CHECK(to == doctest::Approx(-from));
}

TEST_CASE("CJ walks, runs and sits by what the player he follows does")
{
    using Scene::Motion;
    Scene::MotionTracker tracker;
    const float dt = 1.0f / 30.0f;
    Pose::Vec3  at;
    CHECK(tracker.Update(false, at, dt) == Motion::Idle);
    CHECK(tracker.Update(true, at, dt) == Motion::Seated);

    // Walking at 1.5 m/s: a walk once the speed settles, kept through uneven frames.
    Motion m = Motion::Idle;
    for (int i = 0; i < 30; ++i) {
        at.y += 1.5f * dt * (i % 2 ? 0.0f : 2.0f);  // Skyrim's frames land unevenly on San Andreas's
        m = tracker.Update(false, at, dt);
    }
    CHECK(m == Motion::Walk);

    // Running at 5 m/s.
    for (int i = 0; i < 30; ++i) {
        at.y += 5.0f * dt;
        m = tracker.Update(false, at, dt);
    }
    CHECK(m == Motion::Run);
    // Slowing to 3 m/s stays a run (inside the margin).
    for (int i = 0; i < 30; ++i) {
        at.y += 3.0f * dt;
        m = tracker.Update(false, at, dt);
    }
    CHECK(m == Motion::Run);

    // Stopping.
    for (int i = 0; i < 30; ++i) {
        m = tracker.Update(false, at, dt);
    }
    CHECK(m == Motion::Idle);

    // A load door or a re-anchor jumps the player far: no run out of it.
    at.x += 500.0f;
    CHECK(tracker.Update(false, at, dt) == Motion::Idle);
    CHECK(tracker.Speed() < Scene::MotionTracker::kIdleBelow);
}
