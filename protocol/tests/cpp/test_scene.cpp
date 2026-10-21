#include <doctest/doctest.h>

#include "../../cpp/camera.h"
#include "../../cpp/scene.h"

#include <algorithm>
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

TEST_CASE("a furniture marker lands where the furniture's 3D puts it")
{
    // A carriage turned 90 degrees (model +Y to world +X), scale 2, its seat 50 forward and 40 up.
    const float rotate[3][3] = { { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    const auto  at = Scene::MarkerInWorld(rotate, { 1000.0f, 2000.0f, 300.0f }, 2.0f, { 0.0f, 50.0f, 40.0f });
    CHECK(at.x == doctest::Approx(1100.0f));
    CHECK(at.y == doctest::Approx(2000.0f));
    CHECK(at.z == doctest::Approx(380.0f));
}

TEST_CASE("seated, CJ moves so his pelvis is on the seat, and a bad read changes nothing")
{
    Scene::SeatMatch seat;
    const Pose::Vec3 chair{ 10.0f, 20.0f, 0.45f };  // the seat's surface
    seat.Update(false, 0.4f, 0.0f, -0.3f, 1.0f / 30.0f);
    CHECK_FALSE(seat.Have());
    CHECK(seat.Place(chair, 1.0f).z == doctest::Approx(1.45f));  // until then, standing on it
    seat.Update(true, -0.45f, 0.1f, -0.5f, 1.0f / 30.0f);  // the first read is taken as it is
    CHECK(seat.X() == doctest::Approx(-0.45f));
    CHECK(seat.Y() == doctest::Approx(0.1f));
    CHECK(seat.Z() == doctest::Approx(-0.5f));
    // Moved forward by his pelvis's offset, and down so it is on the seat, not his soles.
    const auto at = seat.Place(chair, 1.0f);
    CHECK(at.x == doctest::Approx(10.45f));
    CHECK(at.y == doctest::Approx(19.9f));
    CHECK(at.z + seat.Z() == doctest::Approx(chair.z + Scene::SeatMatch::kPelvisOverSeat));
    seat.Update(true, 5.0f, 0.0f, 0.0f, 1.0f / 30.0f);  // further than a pelvis can be
    CHECK(seat.X() == doctest::Approx(-0.45f));
    for (int i = 0; i < 60; ++i) {  // the idle sways: it settles in between
        seat.Update(true, i % 2 ? -0.40f : -0.50f, 0.1f, -0.5f, 1.0f / 30.0f);
    }
    CHECK(seat.X() == doctest::Approx(-0.45f).epsilon(0.05));
    seat.Reset();
    CHECK(seat.X() == 0.0f);
}

TEST_CASE("in a dialogue Skyrim's turn to the speaker stays out of the camera")
{
    // Skyrim's camera node: its own axes (L) on the player's heading and pitch; in a dialogue it also
    // turns 0.6 rad to the speaker.
    Camera::Matrix axes, plain, built, turned, held, posed, expected;
    Camera::Basis(0.0f, 0.0f, 0.3f, axes);
    Camera::Basis(1.0f, 0.2f, 0.0f, plain);
    Camera::Multiply(plain, axes, built);
    Camera::OffsetOf(built, 1.0f, 0.2f, held);  // before the menu
    Camera::Basis(1.6f, 0.2f, 0.0f, plain);
    Camera::Multiply(plain, axes, turned);      // in it, still built for heading 1.0
    Camera::Retarget(turned, 1.0f, 0.2f, 2.0f, -0.1f, 0.0f, posed);
    Camera::Apply(held, 2.0f, -0.1f, 0.0f, expected);
    float retargeted = 0.0f;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            retargeted = (std::max)(retargeted, std::abs(posed[r][c] - expected[r][c]));
        }
    }
    CHECK(retargeted > 0.1f);  // retargeting each frame would carry the turn over
    Camera::Basis(2.0f, -0.1f, 0.0f, plain);
    Camera::Multiply(plain, axes, posed);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            CHECK(expected[r][c] == doctest::Approx(posed[r][c]));
        }
    }
}
TEST_CASE("a view's forward and up give back its heading, pitch and roll, straight down too")
{
    constexpr float kHalfPi = 1.5707963f;
    const float views[][3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.2f, 0.0f }, { 2.5f, -0.4f, 0.3f }, { 4.0f, 1.2f, -0.5f },
        { 0.7f, kHalfPi, 0.0f }, { 0.0f, kHalfPi, 0.4f }, { 3.0f, kHalfPi - 1e-4f, -0.4f } };  // the death camera, swinging
    for (const auto& v : views) {
        Camera::Matrix m, back;
        Camera::Basis(v[0], v[1], v[2], m);
        const float forward[3] = { m[0][1], m[1][1], m[2][1] };
        const float up[3] = { m[0][2], m[1][2], m[2][2] };
        const auto angles = Camera::AnglesOf(forward, up);
        Camera::Basis(angles.heading, angles.pitch, angles.roll, back);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                CHECK(back[r][c] == doctest::Approx(m[r][c]).epsilon(1e-3));
            }
        }
    }
    const float forward[3] = { 0.0f, 1.0f, 0.0f };
    const float none[3] = { 0.0f, 0.0f, 0.0f };
    CHECK(Camera::AnglesOf(forward, none).roll == 0.0f);
}

TEST_CASE("San Andreas's death camera as logged: looking down, its up is the screen's top")
{
    // From a Wasted in game: the camera 2 m over CJ, front nearly straight down, up along world -y.
    const float forward[3] = { 0.058f, -0.003f, -0.998f };
    const float up[3] = { -0.050f, -0.999f, 0.000f };
    const auto  angles = Camera::AnglesOf(forward, up);
    Camera::Matrix m;
    Camera::Basis(angles.heading, angles.pitch, angles.roll, m);
    for (int r = 0; r < 3; ++r) {
        CHECK(m[r][1] == doctest::Approx(forward[r]).epsilon(0.01));
        CHECK(m[r][2] == doctest::Approx(up[r]).epsilon(0.01));
    }
}
