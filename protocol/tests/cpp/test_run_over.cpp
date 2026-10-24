#include <doctest/doctest.h>

#include "../../cpp/damage.h"
#include "../../cpp/run_over.h"
#include "../../cpp/wanted.h"

using namespace AhShitRim;
using namespace AhShitRim::RunOver;

namespace
{
    // A ZR-350's collision box, roughly: 2 m wide, 4.6 m long, 1.3 m high, its origin in the middle.
    const Box kCar{ { -1.0f, -2.3f, -0.6f }, { 1.0f, 2.3f, 0.7f } };

    Frame At(float x, float y, float z = 0.6f)
    {
        Frame f;
        f.pos = { x, y, z };
        return f;
    }

    // A person standing at (x, y, 0), in metres (San Andreas's side works in metres).
    Capsule PersonAt(float x, float y)
    {
        const Shape s = ShapeOf(Build::Person, 1.0f, 0.0f);
        Capsule     c = Place(s, { x * kMeter, y * kMeter, 0.0f }, 0.0f);
        c.a = Scale(c.a, Pose::kMetersPerUnit);
        c.b = Scale(c.b, Pose::kMetersPerUnit);
        c.radius *= Pose::kMetersPerUnit;
        return c;
    }
}

TEST_CASE("run over: races to builds, giants and mammoths heavy")
{
    CHECK(BuildOf("NordRace", true, false) == Build::Person);
    CHECK(BuildOf("ElderRace", true, false) == Build::Person);
    CHECK(BuildOf("HorseRace", false, false) == Build::Horse);
    CHECK(BuildOf("GiantRace", false, false) == Build::Giant);
    CHECK(BuildOf("MammothRace", false, false) == Build::Mammoth);
    CHECK(BuildOf("DragonRace", false, true) == Build::Dragon);
    CHECK(BuildOf("AlduinRace", false, false) == Build::Dragon);
    CHECK(BuildOf("FrostbiteSpiderRaceGiant", false, false) == Build::Beast);
    CHECK(BuildOf("ChickenRace", false, false) == Build::Small);
    CHECK(BuildOf("DogRace", false, false) == Build::Small);
    CHECK(BuildOf("WolfRace", false, false) == Build::Beast);
    CHECK(BuildOf("DraugrRace", false, false) == Build::Beast);
    CHECK(Heavy(Build::Giant));
    CHECK(Heavy(Build::Mammoth));
    CHECK(Heavy(Build::Dragon));
    CHECK_FALSE(Heavy(Build::Horse));
    CHECK_FALSE(Heavy(Build::Person));
}

TEST_CASE("run over: capsules sized by race and scale")
{
    const Shape person = ShapeOf(Build::Person, 1.0f, 128.0f);
    CHECK_FALSE(person.lying);
    CHECK(person.radius == doctest::Approx(0.3f * kMeter));
    CHECK(person.length + 2.0f * person.radius == doctest::Approx(128.0f));  // Skyrim's height for it
    CHECK(person.mass == doctest::Approx(80.0f));
    CHECK_FALSE(person.heavy);

    // Unknown height: a person's default, scaled.
    const Shape big = ShapeOf(Build::Person, 1.1f, 0.0f);
    CHECK(big.length + 2.0f * big.radius == doctest::Approx(1.8f * kMeter * 1.1f));

    const Shape horse = ShapeOf(Build::Horse, 1.0f, 0.0f);
    CHECK(horse.lying);
    CHECK(horse.mass == doctest::Approx(500.0f));
    CHECK(horse.mass > person.mass * 5.0f);

    CHECK(ShapeOf(Build::Giant, 1.0f, 0.0f).heavy);
    CHECK(ShapeOf(Build::Mammoth, 1.0f, 0.0f).heavy);
    CHECK(ShapeOf(Build::Mammoth, 1.0f, 0.0f).radius > ShapeOf(Build::Horse, 1.0f, 0.0f).radius);
    CHECK(ShapeOf(Build::Small, 1.0f, 0.0f).mass < 20.0f);
    CHECK(ShapeOf(Build::Person, 0.0f, 0.0f).radius == doctest::Approx(person.radius));  // a broken scale counts as 1
}

TEST_CASE("run over: an upright capsule stands on the feet, a lying one along the heading")
{
    const Shape   person = ShapeOf(Build::Person, 1.0f, 128.0f);
    const Capsule up = Place(person, { 100.0f, 200.0f, 10.0f }, 1.0f);
    CHECK(up.a.x == doctest::Approx(100.0f));
    CHECK(up.a.z == doctest::Approx(10.0f + person.radius));
    CHECK(up.b.z == doctest::Approx(10.0f + 128.0f - person.radius));

    const Shape   horse = ShapeOf(Build::Horse, 1.0f, 0.0f);
    const Capsule east = Place(horse, {}, Pose::kPi / 2.0f);  // facing east
    CHECK(east.a.x == doctest::Approx(-0.5f * horse.length));
    CHECK(east.b.x == doctest::Approx(0.5f * horse.length));
    CHECK(east.a.y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(east.a.z == doctest::Approx(horse.centre));
}

TEST_CASE("run over: the nearest actors within range are sent, at most kProxyMax")
{
    std::vector<Candidate> all;
    for (std::uint32_t i = 0; i < 30; i++) {
        all.push_back({ i, static_cast<float>(30 - i) * kMeter });  // 30 m down to 1 m
    }
    all.push_back({ 99, 50.0f * kMeter });  // out of range
    all.push_back({ 98, std::nanf("") });
    const auto picked = Nearest(all);
    REQUIRE(picked.size() == Protocol::kProxyMax);
    CHECK(all[picked[0]].id == 29);  // 1 m off
    CHECK(all[picked[15]].id == 14);
    for (const auto i : picked) {
        CHECK(all[i].distance <= kProxyRange);
    }
    CHECK(Nearest(all, 4, 2.5f * kMeter).size() == 2);
    CHECK(Nearest({}).empty());
}

TEST_CASE("run over: a sphere touches the box, its normal points at the actor")
{
    const auto front = SphereTouch(kCar, At(0.0f, 0.0f), { 0.0f, 2.5f, 0.6f }, 0.3f);
    REQUIRE(front);
    CHECK(front->normal.y == doctest::Approx(1.0f));
    CHECK(front->point.y == doctest::Approx(2.3f));
    CHECK(front->depth == doctest::Approx(0.1f));
    CHECK_FALSE(SphereTouch(kCar, At(0.0f, 0.0f), { 0.0f, 2.7f, 0.6f }, 0.3f));
    CHECK_FALSE(SphereTouch(kCar, At(0.0f, 0.0f), { 1.5f, 0.0f, 0.6f }, 0.3f));

    // Inside the box: out through the nearest side.
    const auto inside = SphereTouch(kCar, At(0.0f, 0.0f), { 0.8f, 0.0f, 0.6f }, 0.3f);
    REQUIRE(inside);
    CHECK(inside->normal.x == doctest::Approx(1.0f));

    // A car turned to face east: its front is +x.
    Frame east = At(0.0f, 0.0f);
    east.right = { 0.0f, -1.0f, 0.0f };
    east.forward = { 1.0f, 0.0f, 0.0f };
    const auto hit = SphereTouch(kCar, east, { 2.5f, 0.0f, 0.6f }, 0.3f);
    REQUIRE(hit);
    CHECK(hit->normal.x == doctest::Approx(1.0f));
}

TEST_CASE("run over: a fast car's frame is swept, so it can't pass through someone")
{
    const Capsule person = PersonAt(0.0f, 10.0f);
    // 50 m/s at 30 frames a second: 1.67 m a frame. From 5 m short of the person's front to past them.
    const auto touch = Sweep(kCar, At(0.0f, 5.0f), At(0.0f, 14.0f), person);
    REQUIRE(touch);
    CHECK(touch->normal.y > 0.9f);
    CHECK(touch->along > 0.0f);
    CHECK(touch->along < 0.5f);
    // Testing only where it ended would have missed: the car is past them.
    CHECK_FALSE(Sweep(kCar, At(0.0f, 14.0f), At(0.0f, 14.0f), person));
    // Driving by a metre to the side misses.
    CHECK_FALSE(Sweep(kCar, At(-2.4f, 5.0f), At(-2.4f, 14.0f), person));
}

TEST_CASE("run over: damage by speed, so a car at speed kills a bandit and a nudge hurts no one")
{
    CHECK(ImpactDamage(2.0f, 1500.0f) == 0.0f);
    CHECK(ImpactDamage(kMinImpact, 1500.0f) == 0.0f);  // nothing over kDamageFrom yet
    CHECK(ImpactDamage(10.0f, 1500.0f) == doctest::Approx(0.9f * 49.0f));
    CHECK(ImpactDamage(20.0f, 1500.0f) > ImpactDamage(15.0f, 1500.0f));
    CHECK(ImpactDamage(60.0f, 1500.0f) == doctest::Approx(kDamageMax));
    CHECK(ImpactDamage(15.0f, 500.0f) < ImpactDamage(15.0f, 1500.0f));    // a bike hurts less
    CHECK(ImpactDamage(15.0f, 25000.0f) > ImpactDamage(15.0f, 1500.0f));  // a tank more
    CHECK(ImpactDamage(15.0f, 25000.0f) == doctest::Approx(ImpactDamage(15.0f, 1500.0f) * 2.0f));
    CHECK(ImpactDamage(std::nanf(""), 1500.0f) == 0.0f);

    // Through the damage table: an early bandit (level 5, about 100 health) dies to one hit at 20 m/s (72 km/h)
    // and takes several at 8 m/s; a guard at level 20 with 250 health takes two at 25 m/s and one at 30 m/s (108 km/h).
    const float fast = Damage::ToSkyrim(ImpactDamage(20.0f, 1500.0f), kRammedByCar, Damage::Part::Torso, 5, Damage::Class::Normal);
    CHECK(Damage::HitsToKill(100.0f, fast) == 1);
    const float slow = Damage::ToSkyrim(ImpactDamage(8.0f, 1500.0f), kRammedByCar, Damage::Part::Torso, 5, Damage::Class::Normal);
    CHECK(Damage::HitsToKill(100.0f, slow) > 3);
    const float guard = Damage::ToSkyrim(ImpactDamage(25.0f, 1500.0f), kRammedByCar, Damage::Part::Torso, 20, Damage::Class::Normal);
    CHECK(Damage::HitsToKill(250.0f, guard) == 2);
    const float faster = Damage::ToSkyrim(ImpactDamage(30.0f, 1500.0f), kRammedByCar, Damage::Part::Torso, 20, Damage::Class::Normal);
    CHECK(Damage::HitsToKill(250.0f, faster) == 1);
    // A mammoth shrugs it off: half for its size.
    const float mammoth = Damage::ToSkyrim(ImpactDamage(20.0f, 1500.0f), kRammedByCar, Damage::Part::Torso, 20, Damage::Class::Giant);
    CHECK(Damage::HitsToKill(1000.0f, mammoth) > 5);
}

TEST_CASE("run over: a person barely slows a car and flies off ahead of it")
{
    const auto r = Respond(1500.0f, { 0.0f, 20.0f, 0.0f }, {}, 80.0f, false, { 0.0f, 1.0f, 0.0f });
    CHECK(r.closing == doctest::Approx(20.0f));
    CHECK(r.vehicleChange.y < 0.0f);
    CHECK(r.vehicleChange.y > -1.5f);  // about 1 m/s off 20
    CHECK(r.actorVelocity.y > 20.0f);  // thrown faster than the car
    CHECK(r.actorVelocity.z > 0.0f);   // and up
    // Momentum along the normal is kept, the bounce aside.
    CHECK(1500.0f * r.vehicleChange.y + 80.0f * r.actorVelocity.y / (1.0f + kThrowBounce) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("run over: a giant or a mammoth stops a car, which comes off it a little")
{
    const auto r = Respond(1500.0f, { 0.0f, 20.0f, 0.0f }, {}, 6000.0f, true, { 0.0f, 1.0f, 0.0f });
    CHECK(r.vehicleChange.y == doctest::Approx(-(1.0f + kHeavyBounce) * 20.0f));
    CHECK(20.0f + r.vehicleChange.y < 0.0f);  // going back
    CHECK(r.actorVelocity.y == 0.0f);         // it doesn't move
    // It dents the car about as a wall does.
    CHECK(HeavyCrashDamage(2.0f) == 0.0f);
    CHECK(HeavyCrashDamage(20.0f) == doctest::Approx(136.0f));
    CHECK(HeavyCrashDamage(std::nanf("")) == 0.0f);
    // A tank too.
    CHECK(Respond(25000.0f, { 0.0f, 10.0f, 0.0f }, {}, 6000.0f, true, { 0.0f, 1.0f, 0.0f }).vehicleChange.y == doctest::Approx(-12.0f));
}

TEST_CASE("run over: moving apart, or hitting from above, is no hit")
{
    CHECK(Respond(1500.0f, { 0.0f, -5.0f, 0.0f }, {}, 80.0f, false, { 0.0f, 1.0f, 0.0f }).closing == 0.0f);
    // Someone running away as fast as the car goes isn't hit.
    CHECK(Respond(1500.0f, { 0.0f, 6.0f, 0.0f }, { 0.0f, 6.0f, 0.0f }, 80.0f, false, { 0.0f, 1.0f, 0.0f }).closing == 0.0f);
    // A normal straight up keeps a share of its height only: a car landing on someone doesn't launch upwards.
    const auto down = Respond(1500.0f, { 0.0f, 0.0f, -8.0f }, {}, 6000.0f, true, { 0.0f, 0.0f, -1.0f });
    CHECK(down.vehicleChange.z > 0.0f);
}

TEST_CASE("run over: a throw's knock grows with its speed, within Skyrim's range")
{
    CHECK(KnockOf(1.0f) == 0.0f);
    CHECK(KnockOf(5.0f) == doctest::Approx(4.0f));
    CHECK(KnockOf(20.0f) == doctest::Approx(16.0f));
    CHECK(KnockOf(80.0f) == doctest::Approx(20.0f));
}

TEST_CASE("run over: one actor is hit once while the car is still in it")
{
    HitMemory hits;
    CHECK(hits.Lands(7, 10.0));
    CHECK_FALSE(hits.Lands(7, 10.03));
    CHECK_FALSE(hits.Lands(7, 10.9));
    CHECK(hits.Lands(8, 10.9));  // someone else
    CHECK(hits.Lands(7, 11.1));
    // More actors than it remembers: the oldest are forgotten, not the newest.
    for (std::uint32_t id = 100; id < 140; id++) {
        CHECK(hits.Lands(id, 20.0 + id * 0.001));
    }
    CHECK_FALSE(hits.Lands(139, 20.2));
    hits.Clear();
    CHECK(hits.Lands(139, 20.2));
}

TEST_CASE("run over: CJ says a line at most every 4 seconds")
{
    SpeechGap gap;
    CHECK(gap.Ready(1.0));
    CHECK_FALSE(gap.Ready(2.0));
    CHECK_FALSE(gap.Ready(4.9));
    CHECK(gap.Ready(5.0));
    CHECK_FALSE(gap.Ready(8.5));
    CHECK(gap.Ready(2.0));  // a clock that went back (San Andreas restarted) doesn't silence him
}

TEST_CASE("run over: a guard run down is Skyrim's assault, a star; running him over dead is a murder, more")
{
    // A hit goes through Skyrim's own hit with the player as the aggressor, so the hold's bounty rises by Skyrim's
    // own amounts and the wanted level takes it as any crime (Police reads the bounties).
    Wanted::Pursuit pursuit;
    CHECK(pursuit.Crime(40, false) == 1);  // knocked over: an assault
    Wanted::Pursuit killed;
    killed.Crime(40, false);
    killed.Crime(1000, false);  // and dead: the murder
    CHECK(killed.Heat() > pursuit.Heat());
}
