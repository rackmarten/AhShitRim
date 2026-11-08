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

TEST_CASE("crew: G picks the homie CJ targets, else the nearest in front within reach")
{
    const Crew::Vec3 cj{ 0.0f, 0.0f, 0.0f };
    std::vector<Crew::Candidate> c;
    c.push_back({ 1, { 0.0f, 5.0f * Crew::kMeter, 0.0f }, false });   // ahead, 5 m
    c.push_back({ 2, { 0.0f, 3.0f * Crew::kMeter, 0.0f }, false });   // ahead, 3 m
    c.push_back({ 3, { 0.0f, -2.0f * Crew::kMeter, 0.0f }, false });  // behind
    CHECK(Crew::Pick(cj, 0.0f, c) == 2);
    CHECK(Crew::Pick(cj, Pose::kPi, c) == 3);  // turned round
    c.push_back({ 4, { 0.0f, 20.0f * Crew::kMeter, 0.0f }, true });   // targeted, far but in reach of a gun
    CHECK(Crew::Pick(cj, 0.0f, c) == 4);
    std::vector<Crew::Candidate> far{ { 5, { 0.0f, 12.0f * Crew::kMeter, 0.0f }, false } };
    CHECK(Crew::Pick(cj, 0.0f, far) == 0);
    std::vector<Crew::Candidate> touching{ { 6, { 0.5f * Crew::kMeter, -0.2f * Crew::kMeter, 0.0f }, false } };
    CHECK(Crew::Pick(cj, 0.0f, touching) == 6);  // right beside him, whatever the angle
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
