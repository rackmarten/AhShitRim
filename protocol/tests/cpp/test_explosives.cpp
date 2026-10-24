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
    CHECK(KindOf(Weapons::SatchelCharge) == Kind::Satchel);
    CHECK(WeaponOf(Weapons::SatchelCharge) == Weapons::SatchelCharge);
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

TEST_CASE("a Molotov's fire grows from the middle to 3 m and burns 30 s")
{
    const Patch middle = FirePatch(0);
    CHECK(Length(middle.offset) == 0.0f);
    CHECK(middle.start == 0.0f);
    float last = 0.0f, reach = 0.0f;
    for (int i = 1; i < kFirePatchCount; ++i) {
        const Patch p = FirePatch(i);
        CHECK(p.offset.z == 0.0f);
        CHECK(p.start >= last);  // outwards, never back in
        last = p.start;
        reach = (std::max)(reach, Length(p.offset) + kFirePatchRadius);
    }
    CHECK(last <= kFireGrow);
    CHECK(reach == doctest::Approx(3.3f * kMeter));
    CHECK(Length(FirePatch(1).offset) == doctest::Approx(kFireInner));
    CHECK(Length(FirePatch(5).offset) == doctest::Approx(kFireOuter));
    CHECK(Length(Sub(FirePatch(1).offset, FirePatch(3).offset)) == doctest::Approx(2.0f * kFireInner));  // opposite sides
    CHECK(kFireSeconds - last >= 20.0f);  // burning on well after it stopped growing
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

TEST_CASE("the spray can and the extinguisher are cones of their own")
{
    CHECK(ConeOf(Weapons::Flamethrower) == Cone::Flame);
    CHECK(ConeOf(Weapons::Spraycan) == Cone::Spray);
    CHECK(ConeOf(Weapons::Extinguisher) == Cone::Hose);
    CHECK(ConeOf(Weapons::Molotov) == Cone::None);

    CHECK(ConeRange(Cone::Spray, 0.0f) == kSprayRangeMin);  // San Andreas didn't say
    CHECK(ConeRange(Cone::Spray, 50.0f * kMeter) == kSprayRangeMax);
    CHECK(ConeRange(Cone::Hose, 5.0f * kMeter) == doctest::Approx(5.0f * kMeter));
    CHECK(ConeRange(Cone::Flame, NAN) == kFlameRangeMin);
    CHECK(kSprayRangeMax < kHoseRangeMax);

    const Vec3  nozzle{ 0.0f, 0.0f, 100.0f };
    const Vec3  north{ 0.0f, 1.0f, 0.0f };
    const float spray = ConeRange(Cone::Spray, 3.0f * kMeter);
    // The spray can is wider than the flamethrower, but short.
    const Vec3 aside{ 0.6f * kMeter, 2.0f * kMeter, 100.0f };
    CHECK(InCone(nozzle, north, spray, aside, 0.0f, HalfAngle(Cone::Spray)));
    CHECK_FALSE(InCone(nozzle, north, spray, aside, 0.0f, HalfAngle(Cone::Flame)));
    CHECK_FALSE(InCone(nozzle, north, spray, { 0.0f, 5.0f * kMeter, 100.0f }, 20.0f, HalfAngle(Cone::Spray)));

    // The extinguisher aimed down at a Molotov's patch puts it out, one beside it too, not one behind CJ or far off.
    const float hose = ConeRange(Cone::Hose, 6.0f * kMeter);
    Vec3        down{ 0.0f, 3.0f, -1.0f };
    down = Scale(down, 1.0f / Length(down));
    const Vec3 patch{ 0.0f, 300.0f, 0.0f };  // where that aim meets the ground
    CHECK(PutsOut(nozzle, down, hose, patch));
    CHECK(PutsOut(nozzle, down, hose, Add(patch, Vec3{ kFireInner, 0.0f, 0.0f })));
    CHECK_FALSE(PutsOut(nozzle, down, hose, { 0.0f, -2.0f * kMeter, 0.0f }));
    CHECK_FALSE(PutsOut(nozzle, down, hose, { 0.0f, 20.0f * kMeter, 0.0f }));
    CHECK_FALSE(PutsOut(nozzle, north, hose, { 4.0f * kMeter, 3.0f * kMeter, 0.0f }));

    // San Andreas's own fires go out round the axis, wider further out.
    CHECK(HoseRadius(0.0f) == kFirePatchRadius);
    CHECK(HoseRadius(4.0f * kMeter) > HoseRadius(1.0f * kMeter));
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

TEST_CASE("a blast hits big and seasoned targets in full")
{
    // The level 38 mammoth from the first in-game test (931 health) took 61 a rocket; now about five kill it.
    const float rocket = Damage::ToSkyrim(BlastDamage(Kind::Rocket, 0.0f), Weapons::RocketLauncher, Damage::Part::Torso, 38, Damage::Class::Giant);
    CHECK(rocket == doctest::Approx(240.0f));
    CHECK(Damage::HitsToKill(931.0f, rocket) <= 5);
    CHECK(Damage::ToSkyrim(300.0f, Weapons::Grenade, Damage::Part::Torso, 50, Damage::Class::Dragon) == doctest::Approx(240.0f));
    // Guns keep their falloff.
    CHECK(Damage::ToSkyrim(30.0f, Weapons::Ak47, Damage::Part::Torso, 38, Damage::Class::Giant) < 30.0f * Damage::kScale * 0.5f);
    CHECK_FALSE(Damage::Explosive(Weapons::Flamethrower));
    CHECK_FALSE(Damage::Explosive(Weapons::TearGas));
}

TEST_CASE("who stands in a Molotov's fire")
{
    const Vec3 patch{ 0.0f, 0.0f, 0.0f };
    CHECK(InFire(patch, { 0.5f * kMeter, 0.0f, 0.0f }, 20.0f));
    CHECK(InFire(patch, { kFirePatchRadius + 15.0f, 0.0f, 10.0f }, 20.0f));  // its capsule's edge in the flames
    CHECK_FALSE(InFire(patch, { 2.0f * kMeter, 0.0f, 0.0f }, 20.0f));
    CHECK_FALSE(InFire(patch, { 0.0f, 0.0f, 3.0f * kMeter }, 20.0f));  // on a ledge above it
}

TEST_CASE("a satchel sticks, waits for the detonator and blows like a grenade")
{
    CHECK(SticksOnContact(Kind::Satchel));
    CHECK_FALSE(SticksOnContact(Kind::Grenade));
    CHECK_FALSE(BurstsOnContact(Kind::Satchel));
    CHECK(Falls(Kind::Satchel));
    CHECK(FuseSeconds(Kind::Satchel, 0) >= kNever);
    CHECK(FuseSeconds(Kind::Satchel, 2000) >= kNever);  // San Andreas's own destroy time doesn't count
    CHECK(BlastOf(Kind::Satchel).radius == doctest::Approx(BlastOf(Kind::Grenade).radius));
    CHECK(BlastOf(Kind::Satchel).opensLocks);
    CHECK(OpensLock(Kind::Satchel, 1.0f * kMeter, true, false));
}

namespace
{
    void CheckNear(const Vec3& a, const Vec3& b, float eps = 0.001f)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(eps));
        CHECK(a.y == doctest::Approx(b.y).epsilon(eps));
        CHECK(a.z == doctest::Approx(b.z).epsilon(eps));
    }

    // a_node turned a_angle radians round Z.
    Node Turned(float a_angle, const Vec3& a_at, float a_scale = 1.0f)
    {
        const float c = std::cos(a_angle), s = std::sin(a_angle);
        Node        n;
        n.x = { c, s, 0.0f };
        n.y = { -s, c, 0.0f };
        n.translate = a_at;
        n.scale = a_scale;
        return n;
    }
}

TEST_CASE("a satchel stuck on a node stays where it was on it")
{
    // Stuck to a mammoth's spine where it touched.
    const Node  spine = Turned(0.3f, { 1000.0f, 2000.0f, 300.0f }, 1.5f);
    const Vec3  at{ 1040.0f, 2010.0f, 360.0f };
    const Quat  turn = AxisAngle({ 0.0f, 0.0f, 1.0f }, 0.5f);
    const Stuck stuck = StickTo(spine, at, turn);
    CheckNear(StuckAt(spine, stuck), at);  // right where it touched until the node moves
    const Vec3 forward = Rotate(StuckTurn(spine, stuck), { 0.0f, 1.0f, 0.0f });
    CheckNear(forward, Rotate(turn, { 0.0f, 1.0f, 0.0f }));

    // The mammoth walks 5 m on: the satchel goes with it.
    Node moved = spine;
    moved.translate = Add(spine.translate, { 5.0f * kMeter, 0.0f, 0.0f });
    CheckNear(StuckAt(moved, stuck), Add(at, { 5.0f * kMeter, 0.0f, 0.0f }));

    // It turns half round where it stands: the satchel is on its other side, turned with it.
    const Node back = Turned(0.3f + 3.14159265f, spine.translate, 1.5f);
    const Vec3 offset = Sub(at, spine.translate);
    CheckNear(StuckAt(back, stuck), { spine.translate.x - offset.x, spine.translate.y - offset.y, at.z });
    CheckNear(Rotate(StuckTurn(back, stuck), { 0.0f, 1.0f, 0.0f }), Scale(Rotate(turn, { 0.0f, 1.0f, 0.0f }), -1.0f) );
}

TEST_CASE("a satchel stuck on a static stays put")
{
    Node wall;
    wall.translate = { 10.0f, 20.0f, 30.0f };
    const Vec3  at{ 10.0f, 20.0f, 30.0f };
    const Stuck stuck = StickTo(wall, at, {});
    CheckNear(StuckAt(wall, stuck), at);
    CheckNear(stuck.offset, {});
}

TEST_CASE("the heat seeker locks on to the big ones")
{
    CHECK(HeatSeekerLocks("DragonRace", true, false));
    CHECK(HeatSeekerLocks("UndeadDragonRace", false, false));
    CHECK(HeatSeekerLocks("HorseRace", false, false));
    CHECK(HeatSeekerLocks("NordRace", false, true));  // riding one
    CHECK(HeatSeekerLocks("GiantRace", false, false));
    CHECK(HeatSeekerLocks("MammothRace", false, false));
    CHECK(HeatSeekerLocks("DwarvenCenturionRace", false, false));
    CHECK_FALSE(HeatSeekerLocks("NordRace", false, false));
    CHECK_FALSE(HeatSeekerLocks("TrollRace", false, false));
    CHECK_FALSE(HeatSeekerLocks("SabreCatRace", false, false));
    CHECK_FALSE(HeatSeekerLocks("BearBlackRace", false, false));
    CHECK_FALSE(HeatSeekerLocks("FrostbiteSpiderRaceGiant", false, false));
    CHECK_FALSE(HeatSeekerLocks("DragonPriestRace", false, false));
}

TEST_CASE("the heat seeker rates targets as San Andreas does")
{
    const Vec3 origin{};
    const Vec3 ahead{ 0.0f, 1.0f, 0.0f };
    // Straight ahead: only the distance counts.
    CHECK(HsRating(origin, ahead, { 0.0f, 100.0f, 0.0f }, false) == doctest::Approx(1.0f));
    // Nearer and on the line beats farther; off the line costs.
    CHECK(HsRating(origin, ahead, { 0.0f, 25.0f, 0.0f }, false) < HsRating(origin, ahead, { 0.0f, 100.0f, 0.0f }, false));
    CHECK(HsRating(origin, ahead, { 20.0f, 100.0f, 0.0f }, false) > HsRating(origin, ahead, { 0.0f, 100.0f, 0.0f }, false));
    // Out of the cone, behind, or out of reach: not a target.
    CHECK(HsRating(origin, ahead, { 100.0f, 50.0f, 0.0f }, false) < 0.0f);
    CHECK(HsRating(origin, ahead, { 0.0f, -50.0f, 0.0f }, false) < 0.0f);
    CHECK(HsRating(origin, ahead, { 0.0f, kHsReach + 1.0f, 0.0f }, false) < 0.0f);
    // The one it has rates four times better.
    CHECK(HsRating(origin, ahead, { 0.0f, 100.0f, 0.0f }, true) == doctest::Approx(0.25f));
}

TEST_CASE("the lock takes 1.5 s on one target in sight")
{
    HsLock lock;
    Track(lock, 7, true, 0.1f);
    CHECK(lock.target == 7);
    CHECK_FALSE(lock.Locked());
    for (int i = 0; i < 16; ++i) {
        Track(lock, 7, true, 0.1f);
    }
    CHECK(lock.Locked());
    Track(lock, 9, true, 0.1f);  // another target: start again
    CHECK(lock.target == 9);
    CHECK_FALSE(lock.Locked());
    for (int i = 0; i < 20; ++i) {
        Track(lock, 9, true, 0.1f);
    }
    CHECK(lock.Locked());
    Track(lock, 9, false, 0.1f);  // out of sight: lost
    CHECK(lock.target == 0);
    CHECK_FALSE(lock.Locked());
    Track(lock, 0, true, 0.1f);
    CHECK_FALSE(lock.Locked());
}

TEST_CASE("a locked rocket turns towards its target at its turn rate and keeps its speed")
{
    const float speed = kRocketSpeed;
    const Vec3  v{ 0.0f, speed, 0.0f };
    // Target to the right, 90 degrees off: one 0.1 s step turns it kHsTurn * 0.1.
    const Vec3  turned = Steer(v, {}, { 1000.0f, 0.0f, 0.0f }, 0.1f);
    CHECK(Length(turned) == doctest::Approx(speed));
    CHECK(std::atan2(turned.x, turned.y) == doctest::Approx(kHsTurn * 0.1f));
    // Nearly on it already: straight at it.
    const Vec3 on = Steer(v, {}, { 1.0f, 1000.0f, 0.0f }, 0.1f);
    CHECK(on.x / on.y == doctest::Approx(1.0f / 1000.0f));
    // Straight behind it still turns.
    const Vec3 back = Steer(v, {}, { 0.0f, -1000.0f, 0.0f }, 0.1f);
    CHECK(Length(back) == doctest::Approx(speed));
    CHECK(back.y < speed);
    // Flown in steps after a dragon flying across, it closes in and comes near enough to go off.
    Flight rocket{ {}, v };
    Vec3   dragon{ 30.0f * kMeter, 60.0f * kMeter, 20.0f * kMeter };
    const Vec3 dragonVelocity{ -10.0f * kMeter, 0.0f, 0.0f };
    bool   reached = false;
    for (int i = 0; i < 300 && !reached; ++i) {
        const float dt = 1.0f / 60.0f;
        rocket.velocity = Steer(rocket.velocity, rocket.position, dragon, dt);
        rocket = Step(rocket, Kind::Rocket, dt);
        dragon = Add(dragon, Scale(dragonVelocity, dt));
        reached = NearTarget(rocket.position, dragon, 3.0f * kMeter);
    }
    CHECK(reached);
}

TEST_CASE("a steered rocket's model turns with it")
{
    const Vec3 a{ 0.0f, 10.0f, 0.0f }, b{ 10.0f, 0.0f, 0.0f };
    CheckNear(Rotate(FromTo(a, b), { 0.0f, 1.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
    CheckNear(Rotate(FromTo(a, a), { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f });
}

TEST_CASE("the Rhino fires from its barrel's tip along its turret's turn and lift")
{
    // Straight ahead and level: along the tank's +y, from the tip in front of the turret's pivot.
    const Vec3 ahead = TankShotDirection(0.0f, 0.0f);
    CHECK(ahead.x == doctest::Approx(0.0f));
    CHECK(ahead.y == doctest::Approx(1.0f));
    CHECK(ahead.z == doctest::Approx(0.0f));
    const Vec3 tip = TankShotMuzzle(0.0f);
    CHECK(tip.y == doctest::Approx(2.95f));
    CHECK(tip.z == doctest::Approx(1.97f));  // TankControl's own drop of a metre

    // Turned a quarter left (counter-clockwise): along -x, the tip out to the left of the pivot.
    const Vec3 left = TankShotDirection(1.5707963f, 0.0f);
    CHECK(left.x == doctest::Approx(-1.0f));
    CHECK(left.y == doctest::Approx(0.0f).epsilon(0.001));
    const Vec3 leftTip = TankShotMuzzle(1.5707963f);
    CHECK(leftTip.x == doctest::Approx(-(2.95f + 1.394f)));
    CHECK(leftTip.y == doctest::Approx(-1.394f));

    // Lifted 20 degrees up a hill: it climbs, a unit vector still.
    const Vec3 up = TankShotDirection(0.0f, 0.349f);
    CHECK(up.z > 0.3f);
    CHECK(Length(up) == doctest::Approx(1.0f));

    CHECK(kTankCannonNode == 22);
}
