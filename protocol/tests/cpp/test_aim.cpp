#include <doctest/doctest.h>

#include "../../cpp/aim.h"

using namespace AhShitRim;
using namespace AhShitRim::Aim;

namespace
{
    float Length(const Vec3& a) { return std::sqrt(Body::Dot(a, a)); }
    float Distance(const Vec3& a, const Vec3& b) { return Length(Body::Sub(a, b)); }
    float Degrees(const Vec3& a, const Vec3& b) { return std::acos(std::clamp(Body::Dot(Body::Normalize(a), Body::Normalize(b)), -1.0f, 1.0f)) * 180.0f / 3.14159265f; }

    constexpr std::uint8_t kFollowPed = 0x04;
}

TEST_CASE("from the hip a shot goes ahead, aiming through the crosshair, at a target or from a vehicle as San Andreas fired it")
{
    // Standing or walking, the left button alone: ahead, level.
    CHECK(LineOf(kFollowPed, true, false) == Line::Ahead);
    // The right button up: through the crosshair.
    CHECK(LineOf(Weapons::kCamAimWeapon, true, false) == Line::Crosshair);
    // A lock-on target, a drive-by, the sights: San Andreas's own line.
    CHECK(LineOf(kFollowPed, true, true) == Line::AsFired);
    CHECK(LineOf(Weapons::kCamAimWeapon, true, true) == Line::AsFired);
    CHECK(LineOf(kFollowPed, false, false) == Line::AsFired);
    CHECK(LineOf(Weapons::kCamSniper, true, false) == Line::AsFired);
    CHECK(LineOf(Weapons::kCamM16FirstPerson, true, false) == Line::AsFired);
    CHECK(LineOf(Weapons::kCamRocketLauncher, true, false) == Line::AsFired);
}

TEST_CASE("ahead is his facing, level")
{
    const Vec3 level = Ahead({ 0.6f, 0.8f, 0.0f });
    CHECK(level.x == doctest::Approx(0.6f));
    CHECK(level.y == doctest::Approx(0.8f));
    CHECK(level.z == 0.0f);
    // A forward tipped up (the hidden arm IK's sky) still fires level, along the same heading.
    const Vec3 tipped = Ahead({ 0.0f, 0.5f, 0.87f });
    CHECK(tipped.x == doctest::Approx(0.0f));
    CHECK(tipped.y == doctest::Approx(1.0f));
    CHECK(tipped.z == 0.0f);
    // No heading at all: north rather than nothing.
    const Vec3 none = Ahead({ 0.0f, 0.0f, 1.0f });
    CHECK(none.y == doctest::Approx(1.0f));
    CHECK(Length(none) == doctest::Approx(1.0f));
}

TEST_CASE("ahead is where his chest faces, square to his shoulders, whatever his heading")
{
    // Shoulders across X, the right one east: he faces north, even with his heading 50 degrees off.
    const Vec3 right{ 0.2f, 0.0f, 1.4f }, left{ -0.2f, 0.0f, 1.42f };
    const Vec3 facing = ChestFacing(right, left, { 0.77f, 0.64f, 0.0f });
    CHECK(facing.x == doctest::Approx(0.0f));
    CHECK(facing.y == doctest::Approx(1.0f));
    CHECK(facing.z == 0.0f);
    // Turned: shoulders along Y, the right one south, he faces east.
    const Vec3 east = ChestFacing({ 0.0f, -0.2f, 1.4f }, { 0.0f, 0.2f, 1.4f }, { 0.0f, 1.0f, 0.0f });
    CHECK(east.x == doctest::Approx(1.0f));
    CHECK(east.y == doctest::Approx(0.0f));
    // One shoulder above the other: his heading.
    const Vec3 lying = ChestFacing({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.6f }, { 1.0f, 0.0f, 0.0f });
    CHECK(lying.x == doctest::Approx(1.0f));
}

TEST_CASE("from the hip twin guns mirror each other, meeting ahead of his chest")
{
    const Vec3 right{ 0.2f, 0.0f, 1.4f }, left{ -0.2f, 0.0f, 1.4f };
    const Vec3 facing = ChestFacing(right, left, { 1.0f, 0.0f, 0.0f });
    const Vec3 point = HipAimPoint(right, left, facing);
    CHECK(point.x == doctest::Approx(0.0f));
    CHECK(point.y == doctest::Approx(kHipAimDistance));
    CHECK(point.z == doctest::Approx(1.4f));
    // Each arm's line: mirrored about his middle, both turned in, not the same way side by side.
    const Vec3 sideRight{ facing.y, -facing.x, 0.0f };
    const Vec3 rightLine = ArmLine(facing, sideRight, Body::Sub(point, right), false);
    const Vec3 leftLine = ArmLine(facing, sideRight, Body::Sub(point, left), true);
    CHECK(rightLine.x < 0.0f);
    CHECK(leftLine.x > 0.0f);
    CHECK(rightLine.x == doctest::Approx(-leftLine.x));
    CHECK(rightLine.y == doctest::Approx(leftLine.y));
    CHECK(rightLine.z == doctest::Approx(leftLine.z));
    // And so are the elbows.
    const Arm rightArm = ReachAlong(right, rightLine, 0.28f, 0.26f, sideRight);
    const Arm leftArm = ReachAlong(left, leftLine, 0.28f, 0.26f, Body::Scale(sideRight, -1.0f));
    CHECK(rightArm.elbow.x == doctest::Approx(-leftArm.elbow.x));
    CHECK(rightArm.elbow.y == doctest::Approx(leftArm.elbow.y));
    CHECK(rightArm.hand.x == doctest::Approx(-leftArm.hand.x));
}

TEST_CASE("an arm keeps to its own side of his body")
{
    const Vec3 forward{ 0.0f, 1.0f, 0.0f }, right{ 1.0f, 0.0f, 0.0f };
    // Ahead and out to its side: as wanted.
    const Vec3 ahead = ArmLine(forward, right, { 0.0f, 20.0f, 1.0f }, false);
    CHECK(ahead.y == doctest::Approx(20.0f));
    CHECK(ArmLine(forward, right, { 10.0f, 10.0f, 0.0f }, false).x == doctest::Approx(10.0f));
    CHECK(ArmLine(forward, right, { -10.0f, 10.0f, 0.0f }, true).x == doctest::Approx(-10.0f));
    // The right arm aiming far to his left stops 15 degrees across, at the same height.
    const Vec3 across = ArmLine(forward, right, { -10.0f, 1.0f, 2.0f }, false);
    CHECK(across.x == doctest::Approx(-kArmAcross * std::hypot(10.0f, 1.0f)).epsilon(0.01));
    CHECK(across.y > 0.0f);
    CHECK(across.z == 2.0f);
    // Behind him: out to its own side.
    const Vec3 behind = ArmLine(forward, right, { 1.0f, -10.0f, 0.0f }, false);
    CHECK(behind.x > 0.0f);
    CHECK(behind.y == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("an arm reaches along the aim straight out, as San Andreas's arm IK leaves it")
{
    const Vec3 shoulder{ 0.2f, 0.0f, 1.4f };
    const Vec3 line{ 0.0f, 1.0f, 0.0f };
    const float upper = 0.28f, fore = 0.26f;
    const Arm arm = ReachAlong(shoulder, line, upper, fore, { 1.0f, 0.0f, 0.0f });
    // The bones keep their lengths.
    CHECK(Distance(shoulder, arm.elbow) == doctest::Approx(upper).epsilon(0.001));
    CHECK(Distance(arm.elbow, arm.hand) == doctest::Approx(fore).epsilon(0.001));
    // The hand on the aim line, the whole arm out.
    CHECK(arm.hand.x == doctest::Approx(shoulder.x));
    CHECK(arm.hand.z == doctest::Approx(shoulder.z));
    CHECK(arm.hand.y == doctest::Approx(upper + fore).epsilon(0.001));
    // The elbow as straight as San Andreas's limit lets it go, the little bend left below the line and out to
    // the arm's side: not dropped down and out, which tipped the forearm up into the gun.
    const float bend = 180.0f - Degrees(Body::Sub(shoulder, arm.elbow), Body::Sub(arm.hand, arm.elbow));
    CHECK(bend == doctest::Approx(1.0f).epsilon(0.05));
    CHECK(Degrees(Body::Sub(arm.elbow, shoulder), line) < 1.0f);
    CHECK(arm.elbow.z < shoulder.z);
    CHECK(arm.elbow.x > shoulder.x);
    // The left arm's elbow goes out to the left.
    CHECK(ReachAlong(shoulder, line, upper, fore, { -1.0f, 0.0f, 0.0f }).elbow.x < shoulder.x);
}

TEST_CASE("an arm bent further reaches less far, its hand still on the line")
{
    const float upper = 0.28f, fore = 0.26f;
    CHECK(ReachOf(upper, fore, 0.0f) == doctest::Approx(upper + fore));
    CHECK(ReachOf(upper, fore, Pose::kPi / 2) == doctest::Approx(std::hypot(upper, fore)));
    CHECK(ReachOf(upper, upper, Pose::kPi) == doctest::Approx(0.0f).epsilon(0.001));
    const Vec3 shoulder{ 0.0f, 0.0f, 1.4f }, line{ 0.0f, 1.0f, 0.0f };
    const Arm  bent = ReachAlong(shoulder, line, upper, fore, { 1.0f, 0.0f, 0.0f }, 30.0f * Pose::kPi / 180.0f);
    CHECK(180.0f - Degrees(Body::Sub(shoulder, bent.elbow), Body::Sub(bent.hand, bent.elbow)) == doctest::Approx(30.0f).epsilon(0.01));
    CHECK(bent.hand.y == doctest::Approx(ReachOf(upper, fore, 30.0f * Pose::kPi / 180.0f)));
    CHECK(Distance(shoulder, bent.elbow) == doctest::Approx(upper).epsilon(0.001));
    CHECK(Distance(bent.elbow, bent.hand) == doctest::Approx(fore).epsilon(0.001));
}

TEST_CASE("an arm aimed up or down still bends, and its hand stays on the line")
{
    const Vec3 shoulder{ 0.0f, 0.0f, 1.4f };
    for (const Vec3 line : { Vec3{ 0.0f, 1.0f, 1.0f }, Vec3{ 0.0f, 1.0f, -1.0f }, Vec3{ 0.0f, 0.0f, 1.0f }, Vec3{ 0.0f, 0.0f, -1.0f } }) {
        const Arm arm = ReachAlong(shoulder, line, 0.28f, 0.26f, { 1.0f, 0.0f, 0.0f });
        CHECK(Distance(shoulder, arm.elbow) == doctest::Approx(0.28f).epsilon(0.001));
        CHECK(Distance(arm.elbow, arm.hand) == doctest::Approx(0.26f).epsilon(0.001));
        CHECK(Degrees(Body::Sub(arm.hand, shoulder), line) < 0.1f);
        CHECK(std::isfinite(arm.elbow.x + arm.elbow.y + arm.elbow.z));
    }
}

TEST_CASE("the barrel points from the muzzle at the aim point, not along the hand-to-muzzle offset")
{
    // The sawn-off: its muzzle 0.56 m along the hand's X axis and 0.15 m above it.
    const Vec3 hand{ 0.0f, 0.0f, 1.4f };
    const Vec3 muzzle = Body::Add(hand, { 0.0f, 0.56f, 0.15f });
    const Vec3 target = Body::Add(muzzle, { 0.0f, 20.0f, 0.0f });
    const Vec3 barrel = BarrelLine(muzzle, target);
    CHECK(barrel.y == doctest::Approx(1.0f));
    CHECK(barrel.z == doctest::Approx(0.0f));
    // Pointing the offset there instead would tip the barrel about 15 degrees down.
    CHECK(Degrees(Body::Sub(muzzle, hand), barrel) == doctest::Approx(15.0f).epsilon(0.02));
}

TEST_CASE("his head turns to the aim point within San Andreas's head limits")
{
    const Vec3  north{ 0.0f, 1.0f, 0.0f };
    const float degree = Pose::kPi / 180.0f;
    // Straight ahead and level: no turn.
    const Look ahead = HeadLook(north, { 0.0f, 20.0f, 0.0f });
    CHECK(ahead.turn == doctest::Approx(0.0f));
    CHECK(ahead.nod == doctest::Approx(0.0f));
    // Aiming 30 degrees up: the head nods all the way up to it, and looks along the aim.
    const Vec3 up{ 0.0f, std::cos(30.0f * degree), std::sin(30.0f * degree) };
    const Look nod = HeadLook(north, up);
    CHECK(nod.nod == doctest::Approx(30.0f * degree));
    CHECK(Degrees(LookLine(north, nod), up) < 0.1f);
    // Straight up or down: no further than 45 degrees.
    CHECK(HeadLook(north, { 0.0f, 0.1f, 5.0f }).nod == doctest::Approx(kHeadNod));
    CHECK(HeadLook(north, { 0.0f, 0.1f, -5.0f }).nod == doctest::Approx(-kHeadNod));
    // To his right (east, facing north) and left: turned, to the right positive.
    CHECK(HeadLook(north, { 1.0f, 1.0f, 0.0f }).turn == doctest::Approx(45.0f * degree));
    CHECK(HeadLook(north, { -1.0f, 1.0f, 0.0f }).turn == doctest::Approx(-45.0f * degree));
    // Behind him: no more than 65 degrees round, and turned that far it doesn't nod.
    const Look behind = HeadLook(north, { 0.5f, -10.0f, 3.0f });
    CHECK(behind.turn == doctest::Approx(kHeadTurn));
    CHECK(behind.nod == doctest::Approx(0.0f));
    // Turned half way to the limit of the nod, half the nod is left.
    const Look half = HeadLook(north, { std::sin(22.5f * degree), std::cos(22.5f * degree), 5.0f });
    CHECK(half.turn == doctest::Approx(22.5f * degree));
    CHECK(half.nod == doctest::Approx(kHeadNod / 2));
    // The look line is a unit vector along his facing turned right and up.
    const Vec3 line = LookLine(north, { 90.0f * degree, 0.0f });
    CHECK(line.x == doctest::Approx(1.0f));
    CHECK(line.y == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(Length(LookLine({ 3.0f, 4.0f, 2.0f }, half)) == doctest::Approx(1.0f));
    // Nothing to look at: no turn.
    CHECK(HeadLook(north, { 0.0f, 0.0f, 0.0f }).turn == 0.0f);
}
