#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "../../cpp/crew.h"

using namespace AhShitRim;

TEST_CASE("crew: the crew's size is San Andreas's respect table with Thane titles as its steps")
{
    CHECK(Crew::SizeFor(0) == 0);
    CHECK(Crew::SizeFor(1) == 2);
    CHECK(Crew::SizeFor(2) == 3);
    CHECK(Crew::SizeFor(5) == 6);
    CHECK(Crew::SizeFor(6) == 7);
    CHECK(Crew::SizeFor(9) == 7);  // titles past six add nothing
    CHECK(Crew::SizeFor(-1) == 0);
}

TEST_CASE("crew: a hold counts as the player's once either side's Thane variable is set, or by the debug count")
{
    std::array<int, 9> imp{}, sons{};
    CHECK(Crew::ThaneMask(imp, sons, -1) == 0);
    imp[0] = 1;   // Whiterun under the Empire
    sons[8] = 1;  // Falkreath under the Stormcloaks
    const auto mask = Crew::ThaneMask(imp, sons, -1);
    CHECK(mask == ((1u << 0) | (1u << 8)));
    CHECK(Crew::Titles(mask) == 2);
    CHECK(Crew::ThaneMask({}, {}, 1) == 1u);  // [Debug] CrewThanes=1: Whiterun
    CHECK(Crew::Titles(Crew::ThaneMask({}, {}, 3)) == 3);
    CHECK(Crew::Titles(Crew::ThaneMask({}, {}, 20)) == 9);
    CHECK(Crew::ThaneMask(imp, sons, 0) == 0);  // forced to none
    CHECK(std::string(Crew::kThaneHolds[0].imperial) == "WhiterunImpGetOutofJail");
}

TEST_CASE("crew: they're armed better with titles, as San Andreas's gangs with respect")
{
    for (std::uint32_t roll = 0; roll < 12; roll++) {
        const auto w = Crew::WeaponFor(1, roll);
        CHECK((w == Crew::kPistol || w == Crew::kMicroSmg));
    }
    bool tec9 = false, ak = false;
    for (std::uint32_t roll = 0; roll < 12; roll++) {
        tec9 |= Crew::WeaponFor(3, roll) == Crew::kTec9;
        ak |= Crew::WeaponFor(3, roll) == Crew::kAk47;
    }
    CHECK(tec9);
    CHECK_FALSE(ak);
    CHECK(Crew::WeaponFor(5, 0) == Crew::kAk47);
    CHECK(Crew::RangeOf(Crew::kAk47) > Crew::RangeOf(Crew::kPistol));
}

TEST_CASE("crew: followers walk in two ranks behind CJ, turned with him")
{
    for (int i = 0; i < Crew::kFollowerMax; i++) {
        CHECK(Crew::FormationOffset(i).y < 0.0f);  // all behind him
    }
    CHECK(Crew::FormationOffset(0).x < 0.0f);
    CHECK(Crew::FormationOffset(1).x > 0.0f);
    // CJ facing east (heading pi/2): behind him is west
    const auto at = Crew::FormationAt({ 0.0f, 0.0f, 10.0f }, Pose::kPi * 0.5f, 2);
    CHECK(at.x == doctest::Approx(-2.8f * Crew::kMeter).epsilon(0.01));
    CHECK(at.y == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(at.z == 10.0f);
    // facing north, follower 1 is to his right (east) and behind (south)
    const auto right = Crew::FormationAt({}, 0.0f, 1);
    CHECK(right.x > 0.0f);
    CHECK(right.y < 0.0f);
}

TEST_CASE("crew: a hold's 15 homies stand in groups of two to four, inside and outside a walled capital")
{
    const auto walled = Crew::ShareFor(true);
    CHECK(walled.stable + walled.gateOutside + walled.gateInside == Crew::kSpotHomies);
    CHECK(walled.gateInside == 7);
    CHECK(walled.stable + walled.gateOutside == 8);
    const auto open = Crew::ShareFor(false);
    CHECK(open.town == 15);

    const auto standing = Crew::Groups({ 1000.0f, 2000.0f, 0.0f }, 0.0f, 15);
    REQUIRE(standing.size() == 15);
    // nobody on top of anybody else
    for (std::size_t i = 0; i < standing.size(); i++) {
        for (std::size_t j = i + 1; j < standing.size(); j++) {
            CHECK(std::hypot(standing[i].at.x - standing[j].at.x, standing[i].at.y - standing[j].at.y) > 0.8f * Crew::kMeter);
        }
    }
    // all within about 20 m of the centre
    for (const auto& s : standing) {
        CHECK(std::hypot(s.at.x - 1000.0f, s.at.y - 2000.0f) < 20.0f * Crew::kMeter);
    }
    // the first group faces its own middle (the centre)
    const auto& first = standing[0];
    const float toMiddle = Pose::HeadingOf(1000.0f - first.at.x, 2000.0f - first.at.y);
    CHECK(first.heading == doctest::Approx(toMiddle));
    CHECK(Crew::Groups({}, 0.0f, 3).size() == 3);
    CHECK(Crew::Groups({}, 0.0f, 0).empty());
    // 5 isn't split 4 + 1: a group of one doesn't chat
    const auto five = Crew::Groups({}, 0.0f, 5);
    CHECK(five.size() == 5);
}

TEST_CASE("crew: a spot fills out of view and close, and empties far away")
{
    CHECK(Crew::Fills(50.0f, false, false));
    CHECK_FALSE(Crew::Fills(50.0f, true, false));    // seen: they don't pop in
    CHECK_FALSE(Crew::Fills(50.0f, false, true));    // filled already
    CHECK_FALSE(Crew::Fills(200.0f, false, false));  // too far
    CHECK(Crew::Empties(200.0f, true));
    CHECK_FALSE(Crew::Empties(120.0f, true));  // between the two: kept
    CHECK_FALSE(Crew::Empties(200.0f, false));
}

TEST_CASE("crew: G acts on the homie CJ aims at: his lock-on or crosshair, else the one on his aim line")
{
    const Crew::Vec3 cj{ 0.0f, 0.0f, 0.0f };
    std::vector<Crew::Candidate> c;
    c.push_back({ 1, { 0.3f * Crew::kMeter, 5.0f * Crew::kMeter, 0.0f }, false });  // 5 m ahead, a little right of the line
    c.push_back({ 2, { 2.0f * Crew::kMeter, 3.0f * Crew::kMeter, 0.0f }, false });  // nearer, but well off it
    c.push_back({ 3, { 0.0f, -2.0f * Crew::kMeter, 0.0f }, false });                // behind
    CHECK(Crew::AimPick(cj, 0.0f, c) == 1);
    CHECK(Crew::AimPick(cj, Pose::kPi, c) == 3);                                       // turned round
    CHECK(Crew::AimPick(cj, Pose::HeadingOf(2.0f, 3.0f), c) == 2);                    // aimed at him
    CHECK(Crew::AimPick(cj, Pose::kPi / 2.0f, c) == 0);                               // aimed east: nobody there
    // Further off the aim line may stray by kAimCone only, not kAimWidth.
    std::vector<Crew::Candidate> far{ { 4, { 1.5f * Crew::kMeter, 18.0f * Crew::kMeter, 0.0f }, false } };
    CHECK(Crew::AimPick(cj, 0.0f, far) == 4);                                          // 4.8 degrees off
    far[0].at.x = 2.5f * Crew::kMeter;
    CHECK(Crew::AimPick(cj, 0.0f, far) == 0);                                          // 7.9 degrees off
    far[0].at = { 0.0f, 25.0f * Crew::kMeter, 0.0f };
    CHECK(Crew::AimPick(cj, 0.0f, far) == 0);                                          // past kAimReach
    far[0].targeted = true;
    CHECK(Crew::AimPick(cj, Pose::kPi, far) == 4);                                     // his crosshair on him: wherever
}

TEST_CASE("crew: a homie fires in bursts of 1 to 2 s with 1 to 2 s between")
{
    Crew::Burst burst(7);
    CHECK_FALSE(burst.Update(0.1f, false));
    float firing = 0.0f, quiet = 0.0f, run = 0.0f, longest = 0.0f;
    bool  was = false;
    for (int i = 0; i < 400; i++) {  // 20 s
        const bool now = burst.Update(0.05f, true);
        (now ? firing : quiet) += 0.05f;
        run = now == was ? run + 0.05f : 0.05f;
        longest = (std::max)(longest, run);
        was = now;
    }
    CHECK(firing > 6.0f);
    CHECK(quiet > 6.0f);
    CHECK(longest <= 2.1f);
    CHECK_FALSE(burst.Update(0.05f, false));  // no target: stops at once
    CHECK(Crew::MayShoot(30.0f, Crew::kPistol, true));
    CHECK_FALSE(Crew::MayShoot(40.0f, Crew::kPistol, true));
    CHECK(Crew::MayShoot(60.0f, Crew::kAk47, true));
    CHECK_FALSE(Crew::MayShoot(10.0f, Crew::kAk47, false));
}

TEST_CASE("crew: CJ's hits on a homie have no aggressor and no crime; a homie's kills aren't CJ's")
{
    constexpr auto onHomie = Crew::RuleFor(true, false);
    CHECK_FALSE(onHomie.aggressor);
    CHECK_FALSE(onHomie.crime);
    CHECK_FALSE(onHomie.skill);
    constexpr auto byHomie = Crew::RuleFor(false, true);
    CHECK_FALSE(byHomie.crime);
    CHECK_FALSE(byHomie.skill);
    constexpr auto normal = Crew::RuleFor(false, false);
    CHECK(normal.aggressor);
    CHECK(normal.crime);
    CHECK(Crew::ShooterOf(0) == 1);
    CHECK(Crew::FromCrew(Crew::ShooterOf(23)));
    CHECK_FALSE(Crew::FromCrew(0));
    CHECK_FALSE(Crew::FromCrew(25));
    CHECK(Crew::SlotOf(Crew::ShooterOf(5)) == 5);
}

TEST_CASE("crew: each homie keeps his slot, and a freed one rests before the next takes it")
{
    Crew::Slots slots;
    CHECK(slots.Take(100) == 0);
    CHECK(slots.Take(200) == 1);
    CHECK(slots.Take(100) == 0);  // the same again
    CHECK(slots.Find(200) == 1);
    slots.Free(100);
    CHECK(slots.Find(100) == -1);
    CHECK(slots.Take(300) == 2);  // slot 0 rests
    slots.Tick(1.0f);
    CHECK(slots.Take(400) == 0);
    CHECK(slots.IdAt(0) == 400);
    for (std::uint32_t id = 1000; id < 1000 + Protocol::kCrewMax; id++) {
        slots.Take(id);
    }
    CHECK(slots.Take(5000) == -1);  // all taken
    CHECK(slots.Find(0) == -1);
}

TEST_CASE("crew: a follower left over 40 m behind for 2 s is put back, and tried again every 2 s")
{
    Crew::Straggler s;
    CHECK_FALSE(s.Update(39.0f, 5.0f));  // within reach: never
    CHECK_FALSE(s.Update(45.0f, 1.0f));
    CHECK_FALSE(s.Update(30.0f, 0.1f));  // he caught up: the wait starts again
    CHECK_FALSE(s.Update(45.0f, 1.5f));
    CHECK(s.Update(45.0f, 0.6f));        // 2.1 s that far
    CHECK_FALSE(s.Update(45.0f, 1.0f));  // no spot found for him: the next try 2 s on
    CHECK(s.Update(45.0f, 1.0f));
    CHECK_FALSE(s.Update(45.0f, 1.9f));
    s.Hold();                            // CJ got in a car: the 2 s start again once he's out
    CHECK_FALSE(s.Update(45.0f, 1.9f));
    CHECK(s.Update(45.0f, 0.2f));
}

TEST_CASE("crew: a puppet faces the way he walks, and CJ's heading or his aim when he stands")
{
    constexpr float kStep = 1.0f / 30.0f;
    const float     still = 1.0f;  // a San Andreas heading
    Crew::Facing    facing;
    Pose::Vec3      feet{ 0.0f, 0.0f, 0.0f };
    CHECK(facing.Update(feet, kStep, still, false, false) == doctest::Approx(still));  // the first call: as told

    // Walking east at 1.5 m/s: he turns to face east (San Andreas heading -pi/2) within a second, not snapping.
    const float east = Pose::SanAndreasHeading(Pose::HeadingOf(1.0f, 0.0f));
    float       h = 0.0f;
    float       first = 0.0f;
    for (int i = 0; i < 30; i++) {
        feet.x += 1.5f * kStep;
        h = facing.Update(feet, kStep, still, true, false);
        if (i == 0) {
            first = h;
        }
    }
    CHECK(std::fabs(first - still) <= Crew::Facing::kTurnRate * kStep + 1e-4f);
    CHECK(h == doctest::Approx(east).epsilon(0.02));

    // Aiming on the move, he faces his aim.
    const float aim = 0.3f;
    for (int i = 0; i < 30; i++) {
        feet.x += 1.5f * kStep;
        h = facing.Update(feet, kStep, aim, true, true);
    }
    CHECK(h == doctest::Approx(aim));

    // Standing, he turns back to CJ's heading.
    for (int i = 0; i < 30; i++) {
        h = facing.Update(feet, kStep, still, false, false);
    }
    CHECK(h == doctest::Approx(still));

    // A move of more than a few metres (a load door) is no direction: he faces as told at once.
    feet.x += 50.0f;
    CHECK(facing.Update(feet, kStep, -2.0f, true, false) == doctest::Approx(-2.0f));
}

TEST_CASE("crew: a puppet's anim update needs one animation that outlives it")
{
    constexpr float kStep = 1.0f / 30.0f;
    // The crash: his walk alone, fading out with its delete flag (a task's anim at its end), the run not started yet.
    const Crew::Blend fadingWalk{ 0.05f, -4.0f, true, true };
    CHECK_FALSE(Crew::Outlives(fadingWalk, kStep));
    CHECK_FALSE(Crew::LeavesANode(&fadingWalk, 1, kStep));
    CHECK_FALSE(Crew::LeavesANode(nullptr, 0, kStep));

    // Not faded through this step, or kept once faded, or fading in: it stays.
    CHECK(Crew::Outlives({ 0.5f, -4.0f, true, true }, kStep));
    CHECK(Crew::Outlives({ 0.05f, -4.0f, false, true }, kStep));
    CHECK(Crew::Outlives({ 0.0f, 4.0f, true, true }, kStep));
    // One whose anim block is gone gives no node.
    CHECK_FALSE(Crew::Outlives({ 1.0f, 0.0f, false, false }, kStep));

    // The fix: the run blended in beside the fading walk.
    const Crew::Blend withRun[] = { fadingWalk, { 0.0f, 4.0f, true, true } };
    CHECK(Crew::LeavesANode(withRun, 2, kStep));
}

TEST_CASE("crew: the followers shoot whoever is hostile or fights them, never a fleeing or peaceful animal")
{
    // IsFoe(hostile, fightsUs, fleeing, animal, aggressive)
    CHECK(Crew::IsFoe(true, false, false, false, true));    // a bandit
    CHECK(Crew::IsFoe(true, true, true, false, true));      // one running off is still hostile
    CHECK(Crew::IsFoe(false, true, false, false, false));   // a guard CJ hit, fighting back
    CHECK_FALSE(Crew::IsFoe(false, true, true, false, false));  // a townsman running from the shooting
    CHECK_FALSE(Crew::IsFoe(false, false, false, false, false));  // anybody else
    CHECK(Crew::IsFoe(true, true, false, true, true));      // a wolf
    CHECK(Crew::IsFoe(false, true, false, true, true));     // a bear CJ woke
    CHECK_FALSE(Crew::IsFoe(false, true, true, true, false));   // an elk running from CJ, CJ its combat target
    CHECK_FALSE(Crew::IsFoe(false, true, false, true, false));  // or turned to him
    CHECK(Crew::IsFoe(true, true, false, true, false));         // a wolf on CJ whose Aggression reads 0 (as the kit saw)
    CHECK_FALSE(Crew::IsFoe(false, true, false, true, false));  // a goat CJ shot, turned to him: not hostile, not aggressive
}

TEST_CASE("crew: a puppet's walk and run play at the rate that keeps his feet on the ground")
{
    using M = Scene::Motion;
    // San Andreas's own paces, played at rate 1.
    CHECK(Crew::Stride::Rate(106, M::Walk, Crew::Stride::kWalkGang1) == doctest::Approx(1.0f));
    CHECK(Crew::Stride::Rate(105, M::Walk, Crew::Stride::kWalkGang2) == doctest::Approx(1.0f));
    CHECK(Crew::Stride::Rate(107, M::Run, Crew::Stride::kRunGang) == doctest::Approx(1.0f));
    // A follower catching up at Skyrim's 5.7 m/s runs faster, one walking briskly walks faster.
    CHECK(Crew::Stride::Rate(105, M::Run, 5.7f) == doctest::Approx(5.7f / 4.396f));
    CHECK(Crew::Stride::Rate(105, M::Walk, 1.8f) == doctest::Approx(1.8f / 0.992f));
    // Within bounds, and standing or sitting plays as it is.
    CHECK(Crew::Stride::Rate(105, M::Run, 30.0f) == doctest::Approx(Crew::Stride::kMaxRate));
    CHECK(Crew::Stride::Rate(105, M::Walk, 0.1f) == doctest::Approx(Crew::Stride::kMinRate));
    CHECK(Crew::Stride::Rate(105, M::Idle, 3.0f) == doctest::Approx(1.0f));
    CHECK(Crew::Stride::Rate(105, M::Seated, 3.0f) == doctest::Approx(1.0f));
    // At the speeds he changes gait, neither gait is played much past twice or half its pace.
    CHECK(Crew::Stride::Rate(105, M::Walk, Crew::Stride::kRunFrom) <= 2.5f);
    CHECK(Crew::Stride::Rate(105, M::Run, Crew::Stride::kWalkBelow) >= 0.4f);
}

TEST_CASE("crew: a puppet takes to the run at about 2.4 m/s and back to the walk below 1.9, not flipping between")
{
    using M = Scene::Motion;
    CHECK(Crew::Stride::Next(M::Idle, 0.1f) == M::Idle);
    CHECK(Crew::Stride::Next(M::Idle, 1.0f) == M::Walk);
    CHECK(Crew::Stride::Next(M::Idle, 3.0f) == M::Run);
    CHECK(Crew::Stride::Next(M::Walk, 2.3f) == M::Walk);
    CHECK(Crew::Stride::Next(M::Walk, 2.4f) == M::Run);
    CHECK(Crew::Stride::Next(M::Run, 2.0f) == M::Run);  // in the margin: stays as he was
    CHECK(Crew::Stride::Next(M::Run, 1.8f) == M::Walk);
    CHECK(Crew::Stride::Next(M::Run, 0.1f) == M::Idle);
    CHECK(Crew::Stride::Next(M::Walk, 0.1f) == M::Idle);
    // A follower easing up from a run to his place goes run, walk, stand: never run straight to stand while moving.
    M gait = M::Run;
    std::string seen;
    for (float speed = 5.7f; speed > 0.0f; speed -= 0.1f) {
        const M next = Crew::Stride::Next(gait, speed);
        if (next != gait) {
            seen += Scene::NameOf(next);
            seen += ' ';
        }
        gait = next;
    }
    CHECK(seen == std::string(Scene::NameOf(M::Walk)) + " " + Scene::NameOf(M::Idle) + " ");
}
