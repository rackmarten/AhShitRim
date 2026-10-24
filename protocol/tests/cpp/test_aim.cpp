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

TEST_CASE("an arm reaches along the aim with its hand on the line and the elbow bent down and out")
{
    const Vec3 shoulder{ 0.2f, 0.0f, 1.4f };
    const Vec3 line{ 0.0f, 1.0f, 0.0f };
    const float upper = 0.28f, fore = 0.26f;
    const Arm arm = ReachAlong(shoulder, line, upper, fore, { 1.0f, 0.0f, 0.0f });
    // The bones keep their lengths.
    CHECK(Distance(shoulder, arm.elbow) == doctest::Approx(upper).epsilon(0.001));
    CHECK(Distance(arm.elbow, arm.hand) == doctest::Approx(fore).epsilon(0.001));
    // The hand on the aim line, almost the whole arm out.
    CHECK(arm.hand.x == doctest::Approx(shoulder.x));
    CHECK(arm.hand.z == doctest::Approx(shoulder.z));
    CHECK(arm.hand.y == doctest::Approx(kArmReach * (upper + fore)));
    // A slight bend, not locked straight: the elbow below the line and out to the arm's side.
    const float bend = 180.0f - Degrees(Body::Sub(shoulder, arm.elbow), Body::Sub(arm.hand, arm.elbow));
    CHECK(bend > 15.0f);
    CHECK(bend < 35.0f);
    CHECK(arm.elbow.z < shoulder.z);
    CHECK(arm.elbow.x > shoulder.x);
    // The left arm's elbow goes out to the left.
    CHECK(ReachAlong(shoulder, line, upper, fore, { -1.0f, 0.0f, 0.0f }).elbow.x < shoulder.x);
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
