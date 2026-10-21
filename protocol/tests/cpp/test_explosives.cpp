#include <cmath>

#include <doctest/doctest.h>

#include "../../cpp/explosives.h"

using namespace AhShitRim;
using namespace AhShitRim::Explosives;

TEST_CASE("San Andreas's projectile types")
{
    CHECK(KindOf(Weapons::Grenade) == Kind::Grenade);
    CHECK(KindOf(Weapons::TearGas) == Kind::TearGas);
    CHECK(KindOf(Weapons::Molotov) == Kind::Molotov);
    CHECK(KindOf(kRocket) == Kind::Rocket);
    CHECK(KindOf(kHeatSeeker) == Kind::Rocket);
    CHECK(KindOf(Weapons::SatchelCharge) == Kind::None);  // #863
    CHECK(KindOf(Weapons::Pistol) == Kind::None);
    CHECK(WeaponOf(kRocket) == Weapons::RocketLauncher);
    CHECK(WeaponOf(kHeatSeeker) == Weapons::RocketLauncherHs);
    CHECK(WeaponOf(Weapons::Grenade) == Weapons::Grenade);
}

TEST_CASE("what goes off where")
{
    CHECK(BurstsOnContact(Kind::Rocket));
    CHECK(BurstsOnContact(Kind::Molotov));
    CHECK_FALSE(BurstsOnContact(Kind::Grenade));
    CHECK_FALSE(BurstsOnContact(Kind::TearGas));
    CHECK(FuseSeconds(Kind::Grenade, 0) == doctest::Approx(2.0f));
    CHECK(FuseSeconds(Kind::Grenade, 1500) == doctest::Approx(1.5f));  // San Andreas's own wins
    CHECK(FuseSeconds(Kind::Molotov, 0) == doctest::Approx(kLifeMax));
}

TEST_CASE("a throw falls under San Andreas's gravity, a rocket flies straight")
{
    CHECK(kGravity == doctest::Approx(20.0f * kMeter));
    Flight thrown{ { 0.0f, 0.0f, 0.0f }, { 10.0f * kMeter, 0.0f, 10.0f * kMeter } };
    // Integrated in small steps it lands where the formula says: up and down in 1 s, 10 m out.
    Flight b = thrown;
    float t = 0.0f;
    do {
        b = Step(b, Kind::Grenade, 0.01f);
        t += 0.01f;
    } while (b.position.z > 0.0f && t < 5.0f);
    CHECK(t == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(b.position.x == doctest::Approx(10.0f * kMeter).epsilon(0.02));

    Flight rocket{ { 0.0f, 0.0f, 100.0f }, { 50.0f * kMeter, 0.0f, 0.0f } };
    const Flight r = Step(rocket, Kind::Rocket, 1.0f);
    CHECK(r.position.z == doctest::Approx(100.0f));
    CHECK(r.position.x == doctest::Approx(50.0f * kMeter));
}

TEST_CASE("a grenade bounces, losing speed, and comes to rest")
{
    const Vec3 up{ 0.0f, 0.0f, 1.0f };
    const Vec3 v = Bounce({ 100.0f, 0.0f, -200.0f }, up);
    CHECK(v.z == doctest::Approx(200.0f * kRestitution));
    CHECK(v.x == doctest::Approx(100.0f * kFriction));
    // Moving off the surface already: kept.
    const Vec3 away = Bounce({ 0.0f, 0.0f, 50.0f }, up);
    CHECK(away.z == doctest::Approx(50.0f));
    // A wall facing west sends it back.
    const Vec3 wall = Bounce({ 300.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f });
    CHECK(wall.x < 0.0f);
    // Bounce after bounce it stops.
    Vec3 s{ 50.0f, 0.0f, -400.0f };
    int  bounces = 0;
    while (!Rests(s) && bounces < 20) {
        s = Bounce(s, up);
        s.z = -s.z;  // falls back onto the ground at the speed it left
        bounces++;
    }
    CHECK(bounces < 10);
}

TEST_CASE("a blast hurts less and throws less the farther out")
{
    const Blast rocket = BlastOf(Kind::Rocket);
    CHECK(rocket.opensLocks);
    CHECK(BlastDamage(Kind::Rocket, 0.0f) == doctest::Approx(300.0f));
    CHECK(BlastDamage(Kind::Rocket, rocket.radius * kCore) == doctest::Approx(300.0f));
    CHECK(BlastDamage(Kind::Rocket, rocket.radius * 0.6f) < 300.0f);
    CHECK(BlastDamage(Kind::Rocket, rocket.radius) == doctest::Approx(300.0f * kEdge));
    CHECK(BlastDamage(Kind::Rocket, rocket.radius + 1.0f) == 0.0f);
    CHECK(BlastDamage(Kind::TearGas, 0.0f) == 0.0f);  // its cloud chokes instead
    // A rocket at CJ's feet is Wasted (San Andreas's 100 health).
    CHECK(BlastDamage(Kind::Rocket, 0.5f * kMeter) > 100.0f);
    // An early bandit next to it dies (Damage::ToSkyrim, level 3, about 100 health).
    CHECK(Damage::ToSkyrim(BlastDamage(Kind::Rocket, 2.0f * kMeter), Weapons::RocketLauncher, Damage::Part::Torso, 3, Damage::Class::Normal) > 100.0f);

    CHECK(KnockMagnitude(Kind::Rocket, 0.0f, Damage::Class::Normal) == doctest::Approx(rocket.knock));
    CHECK(KnockMagnitude(Kind::Rocket, 3.0f * kMeter, Damage::Class::Normal) > 0.0f);
    CHECK(KnockMagnitude(Kind::Rocket, 3.0f * kMeter, Damage::Class::Normal) < rocket.knock);
    CHECK(KnockMagnitude(Kind::Rocket, rocket.radius * 0.9f, Damage::Class::Normal) == 0.0f);
    CHECK(KnockMagnitude(Kind::Rocket, 0.0f, Damage::Class::Giant) == 0.0f);
    CHECK(KnockMagnitude(Kind::Rocket, 0.0f, Damage::Class::Dragon) == 0.0f);
    CHECK(KnockMagnitude(Kind::Molotov, 0.0f, Damage::Class::Normal) == 0.0f);
}

TEST_CASE("a blast opens locks close by, but not a key's")
{
    CHECK(OpensLock(Kind::Rocket, 1.0f * kMeter, true, false));
    CHECK(OpensLock(Kind::Grenade, kLockRadius, true, false));
    CHECK_FALSE(OpensLock(Kind::Grenade, kLockRadius + 1.0f, true, false));
    CHECK_FALSE(OpensLock(Kind::Grenade, 1.0f * kMeter, false, false));  // not locked
    CHECK_FALSE(OpensLock(Kind::Grenade, 1.0f * kMeter, true, true));    // only its key opens it
    CHECK_FALSE(OpensLock(Kind::Molotov, 0.0f, true, false));            // fire, not a blast
    CHECK_FALSE(OpensLock(Kind::TearGas, 0.0f, true, false));
}

TEST_CASE("a Molotov's fire spreads round where it burst")
{
    const Vec3 middle = FirePatch(0);
    CHECK(Length(middle) == 0.0f);
    for (int i = 1; i <= kFirePatches; ++i) {
        const Vec3 p = FirePatch(i);
        CHECK(Length(p) == doctest::Approx(kFireSpread));
        CHECK(p.z == 0.0f);
    }
    CHECK(Length(Sub(FirePatch(1), FirePatch(3))) == doctest::Approx(2.0f * kFireSpread));  // opposite sides
}

TEST_CASE("tear gas and the flamethrower reach who they should")
{
    const Vec3 canister{ 0.0f, 0.0f, 0.0f };
    CHECK(InGas(canister, { kGasRadius * 0.5f, 0.0f, 50.0f }));
    CHECK_FALSE(InGas(canister, { kGasRadius + 10.0f, 0.0f, 0.0f }));

    const Vec3 nozzle{ 0.0f, 0.0f, 100.0f };
    const Vec3 north{ 0.0f, 1.0f, 0.0f };
    const float range = 6.0f * kMeter;
    CHECK(InFlame(nozzle, north, range, { 0.0f, 3.0f * kMeter, 100.0f }, 20.0f));
    CHECK(InFlame(nozzle, north, range, { 0.5f * kMeter, 4.0f * kMeter, 100.0f }, 20.0f));  // off the axis, inside the cone
    CHECK_FALSE(InFlame(nozzle, north, range, { 3.0f * kMeter, 3.0f * kMeter, 100.0f }, 20.0f));  // well outside it
    CHECK_FALSE(InFlame(nozzle, north, range, { 0.0f, range + 100.0f, 100.0f }, 20.0f));       // beyond its reach
    CHECK_FALSE(InFlame(nozzle, north, range, { 0.0f, -2.0f * kMeter, 100.0f }, 20.0f));      // behind the nozzle
    CHECK(InFlame(nozzle, north, range, { 15.0f, 0.0f, 100.0f }, 20.0f));                      // touching the nozzle
}

TEST_CASE("a projectile's parts are drawn where it is, turned with it")
{
    const Quat quarter = AxisAngle({ 0.0f, 0.0f, 1.0f }, 1.5707963f);  // a quarter turn left round Z
    const Vec3 east = Rotate(quarter, { 1.0f, 0.0f, 0.0f });
    CHECK(east.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(east.y == doctest::Approx(1.0f));

    Protocol::BodyBone part{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 10.0f, 0.0f, 0.0f } };
    const auto bone = PartBone(quarter, part);
    CHECK(bone.translation[0] == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(bone.translation[1] == doctest::Approx(10.0f));
    CHECK(bone.rotation[2] == doctest::Approx(quarter.z));
    CHECK(bone.rotation[3] == doctest::Approx(quarter.w));

    // A rocket keeps its turn, a thrown one tumbles while it flies and not once it lies still.
    const Quat none{};
    CHECK(Tumble(none, Kind::Rocket, { 1000.0f, 0.0f, 0.0f }, 0.1f).w == doctest::Approx(1.0f));
    CHECK(Tumble(none, Kind::Grenade, { 1000.0f, 0.0f, 0.0f }, 0.1f).w < 0.99f);
    CHECK(Tumble(none, Kind::Grenade, { 0.0f, 0.0f, 0.0f }, 0.1f).w == doctest::Approx(1.0f));
}

TEST_CASE("a rocket leaves at rocket speed, a throw as San Andreas threw it")
{
    const Vec3 slow = LaunchVelocity(Kind::Rocket, { 0.0f, 100.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
    CHECK(Length(slow) == doctest::Approx(kRocketSpeed));
    CHECK(slow.y > 0.0f);  // along its own velocity
    const Vec3 still = LaunchVelocity(Kind::Rocket, {}, { 1.0f, 0.0f, 0.0f });
    CHECK(still.x == doctest::Approx(kRocketSpeed));  // the way it faces
    const Vec3 fast{ 0.0f, 2.0f * kRocketSpeed, 0.0f };
    CHECK(LaunchVelocity(Kind::Rocket, fast, {}).y == doctest::Approx(fast.y));
    CHECK(LaunchVelocity(Kind::Grenade, { 5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }).x == doctest::Approx(5.0f));
}
