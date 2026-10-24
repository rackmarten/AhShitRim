#include <cmath>
#include <ostream>  // before doctest, so it can print string_view
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "../../cpp/vehicle_hits.h"

using namespace AhShitRim;
using namespace AhShitRim::VehicleHits;

namespace
{
    // A ZR-350's collision box in Skyrim units: 2 m wide, 4.6 m long, 1.3 m high, its origin in the middle.
    const Box kCar{ { -70.0f, -161.0f, -42.0f }, { 70.0f, 161.0f, 49.0f } };

    // The car at (x, y), facing a_heading (Skyrim radians: 0 north, clockwise), level.
    Frame CarAt(float x, float y, float a_heading = 0.0f)
    {
        Frame f;
        f.pos = { x, y, 42.0f };
        f.right = { std::cos(a_heading), -std::sin(a_heading), 0.0f };
        f.forward = { std::sin(a_heading), std::cos(a_heading), 0.0f };
        f.up = { 0.0f, 0.0f, 1.0f };
        return f;
    }
}

TEST_CASE("vehicle hits: Skyrim's hit sources to attacks")
{
    CHECK(AttackOf({ .weapon = true, .type = Weapon::Bow }) == Attack::Arrow);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::Crossbow }) == Attack::Bolt);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::Sword }) == Attack::Melee);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::Battleaxe, .bash = true }) == Attack::Bash);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::HandToHand, .person = true }) == Attack::Unarmed);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::HandToHand, .person = false }) == Attack::Claws);  // a wolf's bite, a giant's club
    CHECK(AttackOf({ .spell = true }) == Attack::Spell);
    CHECK(AttackOf({ .spell = true, .stream = true }) == Attack::Flames);
    CHECK(AttackOf({ .spell = true, .explodes = true }) == Attack::Explosion);
    CHECK(AttackOf({ .weapon = true, .type = Weapon::Staff, .stream = true }) == Attack::Flames);  // a staff casts
    CHECK(AttackOf({ .person = false }) == Attack::Claws);  // no source at all: a creature's own attack
    CHECK(std::string_view(NameOf(Attack::Arrow)) == "an arrow");
    CHECK(std::string_view(NameOf(static_cast<Attack>(200))) == "something");
}

TEST_CASE("vehicle hits: the damage table, light to heavy")
{
    const float dagger = DamageOf({ Attack::Melee, Build::Person, 2.5f });
    const float sword = DamageOf({ Attack::Melee, Build::Person, 9.0f });
    const float hammer = DamageOf({ Attack::Melee, Build::Person, 26.0f });
    CHECK(dagger < sword);
    CHECK(sword < hammer);
    CHECK(sword == doctest::Approx(34.5f));
    CHECK(hammer <= kMeleeMax);
    CHECK(DamageOf({ Attack::Melee, Build::Person, 9.0f, true }) == doctest::Approx(sword * kPowerScale));
    CHECK(DamageOf({ Attack::Bash, Build::Person, 9.0f }) == doctest::Approx(sword * kBashScale));

    // A giant's club hits hard whatever it holds, a power attack no harder; four blows set a car on fire.
    const float giant = DamageOf({ Attack::Claws, Build::Giant });
    CHECK(giant == doctest::Approx(240.0f));
    CHECK(DamageOf({ Attack::Melee, Build::Giant, 2.0f, true }) == doctest::Approx(240.0f));
    CHECK(kVehicleHealth - 3.0f * giant > 250.0f);
    CHECK(kVehicleHealth - 4.0f * giant < 250.0f);
    CHECK(DamageOf({ Attack::Claws, Build::Mammoth }) > giant);
    CHECK(DamageOf({ Attack::Claws, Build::Beast }) > DamageOf({ Attack::Claws, Build::Small }));
    CHECK(DamageOf({ Attack::Unarmed }) < dagger);

    // Forty arrows or so burn a car; a bolt hits harder; a spell harder still, a fireball's blast hardest.
    const float arrow = DamageOf({ Attack::Arrow });
    CHECK((kVehicleHealth - 250.0f) / arrow == doctest::Approx(34.1f).epsilon(0.01));
    CHECK(DamageOf({ Attack::Bolt }) > arrow);
    CHECK(DamageOf({ Attack::Spell }) > DamageOf({ Attack::Bolt }));
    CHECK(DamageOf({ Attack::Explosion }) > DamageOf({ Attack::Spell }));
    CHECK(DamageOf({ Attack::Arrow, Build::Person, 0.0f, true }) == doctest::Approx(arrow));  // power is a blow's
    // A dragon's breath burns faster than a mage's Flames.
    CHECK(DamageOf({ Attack::Flames, Build::Dragon }) > DamageOf({ Attack::Flames, Build::Person }));

    CHECK(DamageOf({ Attack::None }) == 0.0f);
    CHECK(DamageOf({ Attack::Melee, Build::Person, NAN }) == doctest::Approx(kMeleeBase));
    CHECK(DamageOf({ Attack::Melee, Build::Person, 1e9f }) <= kMeleeMax);
    for (std::uint8_t a = 0; a < kAttackCount; a++) {
        for (const auto b : { Build::Person, Build::Small, Build::Beast, Build::Horse, Build::Giant, Build::Mammoth, Build::Dragon }) {
            const float d = DamageOf({ static_cast<Attack>(a), b, 30.0f, true });
            CHECK(d >= 0.0f);
            CHECK(d <= kDamageMax);
        }
    }
}

TEST_CASE("vehicle hits: who takes it, CJ or his vehicle")
{
    CHECK(CoverOf(VehicleKinds::Kind::Car) == Cover::Closed);
    CHECK(CoverOf(VehicleKinds::Kind::MonsterTruck) == Cover::Closed);
    CHECK(CoverOf(VehicleKinds::Kind::Heli) == Cover::Closed);
    CHECK(CoverOf(VehicleKinds::Kind::Plane) == Cover::Closed);
    CHECK(CoverOf(VehicleKinds::Kind::Bike) == Cover::Open);
    CHECK(CoverOf(VehicleKinds::Kind::Bicycle) == Cover::Open);
    CHECK(CoverOf(VehicleKinds::Kind::Quad) == Cover::Open);
    CHECK(CoverOf(VehicleKinds::Kind::Unknown) == Cover::None);

    const auto closed = SplitOf(Cover::Closed, Attack::Arrow);
    CHECK(closed.vehicle == 1.0f);
    CHECK(closed.cj == 0.0f);
    const auto open = SplitOf(Cover::Open, Attack::Melee);
    CHECK(open.vehicle == kOpenShare);
    CHECK(open.cj == kOpenShare);
    // A blast never reaches anyone in a vehicle (San Andreas skips a ped in one): the bike takes it whole.
    const auto blast = SplitOf(Cover::Open, Attack::Explosion);
    CHECK(blast.vehicle == 1.0f);
    CHECK(blast.cj == 0.0f);
    CHECK(SplitOf(Cover::Closed, Attack::Explosion).cj == 0.0f);
    CHECK(SplitOf(Cover::None, Attack::Explosion).cj == 1.0f);  // on foot it's all his
    const auto foot = SplitOf(Cover::None, Attack::Arrow);
    CHECK(foot.vehicle == 0.0f);
    CHECK(foot.cj == 1.0f);
}

TEST_CASE("vehicle hits: the frame from the slot's rotation")
{
    // A car turned to face east: its forward is +X, its right -Y.
    const auto q = Body::FromAxes({ 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
    Protocol::BodyBone bone{};
    bone.rotation[0] = q.x;
    bone.rotation[1] = q.y;
    bone.rotation[2] = q.z;
    bone.rotation[3] = q.w;
    const auto f = FrameOf(bone, { 10.0f, 20.0f, 30.0f });
    CHECK(f.pos.x == 10.0f);
    CHECK(f.forward.x == doctest::Approx(1.0f));
    CHECK(f.forward.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(f.right.y == doctest::Approx(-1.0f));
    CHECK(f.up.z == doctest::Approx(1.0f));

    CHECK(HasBox(kCar));
    CHECK_FALSE(HasBox(Box{}));  // an older San Andreas, or a slot with no col model
}

TEST_CASE("vehicle hits: projectiles into the box")
{
    const Frame car = CarAt(0.0f, 0.0f);
    // An arrow from the west at door height goes into the left side, about where it is.
    const auto side = Enters(kCar, car, { -500.0f, 0.0f, 60.0f }, { 500.0f, 0.0f, 60.0f });
    REQUIRE(side);
    CHECK(*side == doctest::Approx(0.43f));
    // One over the roof misses, unless the box is grown to take it.
    CHECK_FALSE(Enters(kCar, car, { -500.0f, 0.0f, 100.0f }, { 500.0f, 0.0f, 100.0f }));
    CHECK(Enters(kCar, car, { -500.0f, 0.0f, 100.0f }, { 500.0f, 0.0f, 100.0f }, 10.0f));
    // Too short to get there this frame.
    CHECK_FALSE(Enters(kCar, car, { -500.0f, 0.0f, 60.0f }, { -200.0f, 0.0f, 60.0f }));
    // Starting inside is 0.
    CHECK(*Enters(kCar, car, { 0.0f, 0.0f, 60.0f }, { 500.0f, 0.0f, 60.0f }) == 0.0f);
    // Straight down onto the roof, and along an axis outside it.
    CHECK(Enters(kCar, car, { 0.0f, 100.0f, 300.0f }, { 0.0f, 100.0f, 0.0f }));
    CHECK_FALSE(Enters(kCar, car, { 100.0f, 0.0f, 300.0f }, { 100.0f, 0.0f, 0.0f }));
    CHECK_FALSE(Enters(kCar, car, { NAN, 0.0f, 60.0f }, { 500.0f, 0.0f, 60.0f }));

    // Turned east, the car is long across X: the same arrow from the west meets its back much sooner.
    const Frame east = CarAt(0.0f, 0.0f, 1.5707963f);
    const auto  back = Enters(kCar, east, { -500.0f, 0.0f, 60.0f }, { 500.0f, 0.0f, 60.0f });
    REQUIRE(back);
    CHECK(*back == doctest::Approx(0.339f).epsilon(0.01));
    // And one passing 1 m north of its middle, which would hit a north-facing car's side, misses it.
    CHECK(Enters(kCar, car, { -500.0f, 140.0f, 60.0f }, { 500.0f, 140.0f, 60.0f }));
    CHECK_FALSE(Enters(kCar, east, { -500.0f, 140.0f, 60.0f }, { 500.0f, 140.0f, 60.0f }));
}

TEST_CASE("vehicle hits: the nearest point and blows that reach")
{
    const Frame car = CarAt(0.0f, 0.0f);
    const auto  n = Nearest(kCar, car, { -200.0f, 50.0f, 60.0f });
    CHECK(n.x == doctest::Approx(-70.0f));
    CHECK(n.y == doctest::Approx(50.0f));
    CHECK(GapTo(kCar, car, { -200.0f, 50.0f, 60.0f }) == doctest::Approx(130.0f));
    CHECK(GapTo(kCar, car, { 0.0f, 0.0f, 50.0f }) == 0.0f);

    // A swordsman (reach about 1.4 m) 1 m from the left side, facing it, reaches; facing away he doesn't.
    const float east = 1.5707963f, west = -1.5707963f;
    CHECK(Reaches(kCar, car, { -140.0f, 0.0f, 80.0f }, east, 100.0f));
    CHECK_FALSE(Reaches(kCar, car, { -140.0f, 0.0f, 80.0f }, west, 100.0f));
    // Too far off, whichever way he faces.
    CHECK_FALSE(Reaches(kCar, car, { -300.0f, 0.0f, 80.0f }, east, 100.0f));
    // A giant's long reach gets there from farther.
    CHECK(Reaches(kCar, car, { -300.0f, 0.0f, 80.0f }, east, 250.0f));
    // Standing in the car (no Skyrim collision for it) counts whichever way.
    CHECK(Reaches(kCar, car, { 0.0f, 0.0f, 60.0f }, west, 100.0f));
    CHECK_FALSE(Reaches(kCar, car, { -140.0f, 0.0f, 80.0f }, east, NAN));
}

TEST_CASE("vehicle hits: one attacker's hits a gate apart count once")
{
    Gate gate;
    CHECK(gate.Lands(1, 10.0));
    CHECK_FALSE(gate.Lands(1, 10.1));  // Skyrim's own hit on the player after the box saw the arrow
    CHECK(gate.Lands(2, 10.1));        // someone else
    CHECK(gate.Lands(1, 10.0 + kGate + 0.01));
    CHECK(gate.Lands(1, 5.0));  // the clock went back (a load): counts
    for (std::uint32_t id = 100; id < 200; id++) {
        CHECK(gate.Lands(id, 20.0));  // more attackers than it keeps: the oldest go
    }
    gate.Clear();
    CHECK(gate.Lands(150, 20.05));
}

TEST_CASE("door run: the nearest pursuer goes once the car stops")
{
    DoorRun                     run;
    const std::vector<Pursuer> near{ { 7, 900.0f }, { 3, 400.0f }, { 9, 3000.0f } };
    // Moving: nobody goes.
    CHECK(run.Tick(0.5f, true, Wanted::Bust::Door, 600.0f, near).send == 0);
    // Stopped for less than kSendAfter: not yet.
    CHECK(run.Tick(0.2f, true, Wanted::Bust::Door, 0.0f, near).send == 0);
    const auto go = run.Tick(0.2f, true, Wanted::Bust::Door, 0.0f, near);
    CHECK(go.send == 3);
    CHECK(run.Runner() == 3);
    // He keeps running while he gets closer.
    for (float d = 380.0f; d > 100.0f; d -= 30.0f) {
        const auto keep = run.Tick(0.25f, true, Wanted::Bust::Door, 0.0f, std::vector<Pursuer>{ { 7, 900.0f }, { 3, d } });
        CHECK(keep.send == 0);
        CHECK(keep.recall == 0);
    }
    CHECK(run.Runner() == 3);
}

TEST_CASE("door run: out of range, a bike, no stars")
{
    DoorRun                     run;
    const std::vector<Pursuer> far{ { 9, kSendRange + 1.0f } };
    CHECK(run.Tick(1.0f, true, Wanted::Bust::Door, 0.0f, far).send == 0);
    // A bike: still someone goes (to CJ himself).
    const std::vector<Pursuer> near{ { 4, 500.0f } };
    CHECK(run.Tick(1.0f, true, Wanted::Bust::KnockOff, 0.0f, near).send == 4);
    // The stars gone: he's called back.
    const auto off = run.Tick(0.1f, false, Wanted::Bust::KnockOff, 0.0f, near);
    CHECK(off.recall == 4);
    CHECK(run.Runner() == 0);
    // An aircraft has no door to run to.
    CHECK(run.Tick(1.0f, true, Wanted::Bust::None, 0.0f, near).send == 0);
}

TEST_CASE("door run: driving off calls him back, a stuck one gives up and another goes")
{
    DoorRun                     run;
    std::vector<Pursuer>        near{ { 3, 400.0f }, { 7, 600.0f } };
    CHECK(run.Tick(0.5f, true, Wanted::Bust::Door, 0.0f, near).send == 3);
    // Creeping on for less than kMovingFor keeps him going.
    CHECK(run.Tick(0.5f, true, Wanted::Bust::Door, 300.0f, near).recall == 0);
    const auto off = run.Tick(0.5f, true, Wanted::Bust::Door, 300.0f, near);
    CHECK(off.recall == 3);
    CHECK_FALSE(off.gaveUp);

    // Stopped again, he goes again; stuck behind a fence, no closer for kStuckFor, he gives up and the next goes.
    CHECK(run.Tick(0.5f, true, Wanted::Bust::Door, 0.0f, near).send == 3);
    Orders orders;
    for (int i = 0; i < 20 && !orders.recall; i++) {
        orders = run.Tick(0.25f, true, Wanted::Bust::Door, 0.0f, near);
    }
    CHECK(orders.recall == 3);
    CHECK(orders.gaveUp);
    CHECK(orders.send == 7);
    // The one who gave up isn't sent again for a while, even when nearest.
    near = { { 3, 100.0f } };
    const auto gone = run.Tick(0.1f, true, Wanted::Bust::Door, 0.0f, near);
    CHECK(gone.recall == 7);  // the runner left the pursuit (killed, or too far off)
    CHECK(gone.send == 0);
    Orders later;
    for (int i = 0; i < 80 && !later.send; i++) {
        later = run.Tick(0.25f, true, Wanted::Bust::Door, 0.0f, near);
    }
    CHECK(later.send == 3);
}

TEST_CASE("door run: a run takes kRunMax at most")
{
    DoorRun run;
    float   d = 2000.0f;
    CHECK(run.Tick(0.5f, true, Wanted::Bust::Door, 0.0f, std::vector<Pursuer>{ { 5, d } }).send == 5);
    Orders orders;
    float  t = 0.0f;
    while (!orders.recall && t < 30.0f) {
        d -= 50.0f;  // getting closer, but slowly
        orders = run.Tick(0.5f, true, Wanted::Bust::Door, 0.0f, std::vector<Pursuer>{ { 5, (std::max)(d, 300.0f) } });
        t += 0.5f;
    }
    CHECK(orders.recall == 5);
    CHECK(orders.gaveUp);
    CHECK(t == doctest::Approx(kRunMax).epsilon(0.05));
}

TEST_CASE("door run: the spot and KeepOffsetFromActor's offset")
{
    // Facing north, a point east of the player is to its right.
    auto o = LocalOffset({ 100.0f, 0.0f }, 0.0f);
    CHECK(o.x == doctest::Approx(100.0f));
    CHECK(o.y == doctest::Approx(0.0f));
    // Facing east, a point east is ahead, one south to the right.
    o = LocalOffset({ 100.0f, 0.0f }, 1.5707963f);
    CHECK(o.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(o.y == doctest::Approx(100.0f));
    o = LocalOffset({ 0.0f, -100.0f }, 1.5707963f);
    CHECK(o.x == doctest::Approx(100.0f));

    // A car: the driver's door; a bike: beside CJ on his left.
    const auto door = RunTo(Wanted::Bust::Door, { 0.0f, 0.0f }, 0.0f, { 5.0f, 5.0f });
    CHECK(door.x == doctest::Approx(-Wanted::kDoorSide));
    const auto bike = RunTo(Wanted::Bust::KnockOff, { 0.0f, 0.0f }, 0.0f, { 5.0f, 5.0f });
    CHECK(bike.x == doctest::Approx(5.0f - kBikeSide));
    CHECK(bike.y == doctest::Approx(5.0f));
}

TEST_CASE("door run: no bust while CJ gets in or out, and only people run for the door")
{
    // Getting off the Sanchez (or into the car), nobody busts him, as San Andreas's cops don't.
    CHECK(BustNow(Wanted::Bust::KnockOff, true) == Wanted::Bust::None);
    CHECK(BustNow(Wanted::Bust::Door, true) == Wanted::Bust::None);
    CHECK(BustNow(Wanted::Bust::Door, false) == Wanted::Bust::Door);
    CHECK(BustNow(Wanted::Bust::KnockOff, false) == Wanted::Bust::KnockOff);
    // And the door run stops with it: the runner is called back as he would be for no stars.
    DoorRun                     run;
    const std::vector<Pursuer> near{ { 4, 300.0f } };
    CHECK(run.Tick(1.0f, true, BustNow(Wanted::Bust::KnockOff, false), 0.0f, near).send == 4);
    CHECK(run.Tick(0.1f, true, BustNow(Wanted::Bust::KnockOff, true), 0.0f, near).recall == 4);

    CHECK(CanRun(Build::Person, false));
    CHECK_FALSE(CanRun(Build::Person, true));  // a rider
    CHECK_FALSE(CanRun(Build::Giant, false));
    CHECK_FALSE(CanRun(Build::Mammoth, false));
    CHECK_FALSE(CanRun(Build::Dragon, false));
    CHECK_FALSE(CanRun(Build::Beast, false));
    CHECK(kRecallHold > 0.0f);
}

TEST_CASE("vehicle hits: San Andreas's proofs keep it off the vehicle and everyone in it")
{
    // The Rhino: bullet and explosion proof (CAutomobile's constructor), not fire or melee.
    const auto rhino = ProofsOf(Protocol::kBodyValid | Protocol::kBodyDriven | Protocol::kBodyBulletProof | Protocol::kBodyExplosionProof);
    CHECK(rhino.bullet);
    CHECK(rhino.explosion);
    CHECK_FALSE(rhino.fire);
    CHECK_FALSE(rhino.melee);
    CHECK(ProofAgainst(rhino, Attack::Explosion));
    CHECK(ProofAgainst(rhino, Attack::Arrow));
    CHECK(ProofAgainst(rhino, Attack::Bolt));
    CHECK_FALSE(ProofAgainst(rhino, Attack::Spell));
    CHECK_FALSE(ProofAgainst(rhino, Attack::Melee));

    const auto blast = SplitOf(Cover::Closed, Attack::Explosion, rhino);
    CHECK(blast.vehicle == 0.0f);  // the Rhino shrugs it off
    CHECK(blast.cj == 0.0f);       // and nobody inside is hurt
    const auto arrow = SplitOf(Cover::Closed, Attack::Arrow, rhino);
    CHECK(arrow.vehicle == 0.0f);
    CHECK(arrow.cj == 0.0f);
    const auto fire = SplitOf(Cover::Closed, Attack::Spell, rhino);
    CHECK(fire.vehicle == 1.0f);  // a spell still burns it
    CHECK(fire.cj == 0.0f);

    // A bulletproof bike (SMASH_N_BOOM's, a script's): an arrow reaches neither the bike nor its rider.
    const Proofs bullet{ .bullet = true };
    CHECK(SplitOf(Cover::Open, Attack::Arrow, bullet).cj == 0.0f);
    CHECK(SplitOf(Cover::Open, Attack::Arrow, bullet).vehicle == 0.0f);
    CHECK(SplitOf(Cover::Open, Attack::Melee, bullet).cj == kOpenShare);  // a sword still does

    // On foot proofs mean nothing: they're the vehicle's.
    CHECK(SplitOf(Cover::None, Attack::Explosion, rhino).cj == 1.0f);
    CHECK(ProofsOf(0).bullet == false);
}
