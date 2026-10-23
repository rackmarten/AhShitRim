#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "../../cpp/wanted.h"

using namespace AhShitRim::Wanted;
namespace VK = AhShitRim::VehicleKinds;

namespace
{
    // Ticks a sequence in 0.1 s steps until a_phase starts, or a_limit seconds pass; returns how long it took.
    float RunTo(Sequence& a_seq, Phase a_phase, bool a_arrived = false, float a_limit = 60.0f)
    {
        float t = 0.0f;
        while (a_seq.GetPhase() != a_phase && t < a_limit) {
            a_seq.Tick(0.1f, a_arrived);
            t += 0.1f;
        }
        return t;
    }

    // Ticks a pursuit unseen in 0.1 s steps for a_seconds; returns the last look.
    Look Hide(Pursuit& a_pursuit, float a_seconds, int /*a_bounty*/ = 0)
    {
        Look look;
        for (float t = 0.0f; t < a_seconds; t += 0.1f) {
            look = a_pursuit.Tick(0.1f, false, false);
        }
        return look;
    }
}

TEST_CASE("stars for heat: San Andreas's thresholds")
{
    CHECK(StarsForHeat(0.0f) == 0);
    CHECK(StarsForHeat(49.0f) == 0);
    CHECK(StarsForHeat(50.0f) == 1);
    CHECK(StarsForHeat(180.0f) == 2);
    CHECK(StarsForHeat(550.0f) == 3);
    CHECK(StarsForHeat(1200.0f) == 4);
    CHECK(StarsForHeat(2400.0f) == 5);
    CHECK(StarsForHeat(4600.0f) == 6);
    CHECK(StarsForHeat(1.0e6f) == kMaxStars);
    for (int stars = 0; stars <= kMaxStars; stars++) {
        CHECK(StarsForHeat(HeatFor(stars)) == stars);
    }
}

TEST_CASE("one assault is a star; killing guards climbs slowly, as in San Andreas")
{
    Pursuit p;
    CHECK(p.Crime(5, false) == 0);   // trespass alone: nothing yet
    Pursuit q;
    CHECK(q.Crime(40, false) == 1);  // shooting a guard once
    // Each guard killed: his assault, then his murder.
    Pursuit r;
    auto kills = [&](int a_count) {
        for (int i = 0; i < a_count; i++) {
            r.Crime(40, false);
            r.Crime(1000, false);
        }
        return r.Stars();
    };
    CHECK(kills(1) == 1);
    CHECK(kills(1) == 2);  // two
    CHECK(kills(2) == 3);  // four
    CHECK(kills(4) == 4);  // eight
    CHECK(kills(7) == 5);  // fifteen
    CHECK(kills(13) == 5); // twenty-eight
    CHECK(kills(1) == 6);  // twenty-nine
}

TEST_CASE("AEZAKMI: no stars from any crime")
{
    Pursuit p;
    CHECK(p.Crime(1000, true) == 0);
    p.Recognised(1000, true);
    CHECK(p.Stars() == 0);
    p.Crime(40, false);
    CHECK(p.Tick(0.1f, true, true).stars == 0);  // typed while wanted: gone at once
}

TEST_CASE("out of sight: a grace before the flashing, then the stars go one at a time")
{
    Pursuit p;
    for (int i = 0; i < 4; i++) {
        p.Crime(40, false);
        p.Crime(1000, false);
    }
    REQUIRE(p.Stars() == 3);
    Look look = p.Tick(0.1f, true, false);
    CHECK(look.flashing);  // they just changed
    for (int i = 0; i < 30; i++) {
        look = p.Tick(0.1f, true, false);
    }
    CHECK_FALSE(look.flashing);  // seen, settled
    look = Hide(p, kFlashAfter - 0.5f, 5000);
    CHECK(look.stars == 3);
    CHECK_FALSE(look.flashing);  // not at once
    look = Hide(p, 1.0f, 5000);
    CHECK(look.flashing);
    look = Hide(p, LoseSeconds(3) - 1.0f, 5000);
    CHECK(look.stars == 3);
    look = Hide(p, 1.5f, 5000);
    CHECK(look.stars == 2);  // one star, not all
    CHECK(look.flashing);
    look = Hide(p, LoseSeconds(2) + 0.5f, 5000);
    CHECK(look.stars == 1);
    look = Hide(p, LoseSeconds(1) + 0.5f, 5000);
    CHECK(look.stars == 0);
    CHECK_FALSE(look.flashing);
}

TEST_CASE("two stars go in well under half a minute unseen; six take more than a minute")
{
    auto loseAll = [](int a_stars) {
        Pursuit p;
        p.Restore(HeatFor(a_stars), 0.0f);
        float t = 0.0f;
        while (p.Stars() > 0 && t < 600.0f) {
            p.Tick(0.1f, false, false);
            t += 0.1f;
        }
        return t;
    };
    CHECK(loseAll(2) < 30.0f);
    CHECK(loseAll(6) > 60.0f);
    CHECK(LoseSeconds(1) < LoseSeconds(6));
}

TEST_CASE("being seen again stops the countdown")
{
    Pursuit p;
    p.Restore(HeatFor(4), 0.0f);
    Hide(p, kFlashAfter + LoseSeconds(4) - 1.0f, 1000);
    CHECK(p.Stars() == 4);
    p.Tick(0.1f, true, false);  // a guard spots him
    CHECK(p.Unseen() == 0.0f);
    CHECK(Hide(p, kFlashAfter + LoseSeconds(4) - 1.0f, 1000).stars == 4);
}

TEST_CASE("a guard who fights him later knows him: a star, two for a murderer, no more")
{
    CHECK(StarsForBounty(0) == 0);
    CHECK(StarsForBounty(40) == 1);
    CHECK(StarsForBounty(1000) == 2);
    CHECK(StarsForBounty(50000) == 2);
    Pursuit p;
    p.Recognised(5000, false);
    CHECK(p.Stars() == 2);
    p.Restore(HeatFor(4), 0.0f);
    p.Recognised(5000, false);  // already wanted: no change
    CHECK(p.Stars() == 4);
    Pursuit clean;
    clean.Recognised(0, false);
    CHECK(clean.Stars() == 0);
}

TEST_CASE("killing the witnesses takes Skyrim's bounty, not the stars")
{
    Pursuit p;
    p.Crime(40, false);
    REQUIRE(p.Stars() == 1);
    CHECK(p.Tick(0.1f, true, false).stars == 1);  // the bounty is gone; the pursuit isn't
    p.Clear();  // the fine paid, Busted, Wasted
    CHECK(p.Stars() == 0);
}

TEST_CASE("petty crimes alone raise a star at most, however often Skyrim reports them")
{
    Pursuit p;
    for (int i = 0; i < 200; i++) {
        p.Crime(5, false);  // trespassing, step after step
    }
    CHECK(p.Stars() == 1);
    CHECK(p.Heat() == HeatFor(1));
    p.Crime(40, false);  // an assault on top still counts
    CHECK(p.Heat() == HeatFor(1) + 60.0f);
    Pursuit q;
    q.Crime(1, false);  // a piece of meat
    CHECK(q.Stars() == 0);
}

TEST_CASE("the hold's own guards join from 2 stars, further the more there are")
{
    CHECK(JoinRadius(0) == 0.0f);
    CHECK(JoinRadius(1) == 0.0f);
    CHECK(JoinRadius(2) > 0.0f);
    for (int stars = 2; stars < kMaxStars; stars++) {
        CHECK(JoinRadius(stars + 1) > JoinRadius(stars));
    }
}

TEST_CASE("the force grows with the stars, faster, never more than twelve")
{
    CHECK(ForceFor(0).total == 0);
    CHECK(ForceFor(1).total == 0);  // Skyrim's own guards only
    CHECK(ForceFor(2).total > 0);
    for (int stars = 2; stars < kMaxStars; stars++) {
        const auto now = ForceFor(stars);
        const auto next = ForceFor(stars + 1);
        CHECK(next.total > now.total);
        CHECK(next.cooldown < now.cooldown);
        int special = 0;
        for (const int n : now.most) {
            special += n;
        }
        CHECK(special <= now.total);
    }
    CHECK(ForceFor(kMaxStars).total == kForceCap);
    CHECK(ForceFor(3).most[static_cast<std::size_t>(Unit::Rider)] > 0);   // riders from 3
    CHECK(ForceFor(2).most[static_cast<std::size_t>(Unit::Rider)] == 0);
    CHECK(ForceFor(4).most[static_cast<std::size_t>(Unit::Wizard)] > 0);  // a guard wizard from 4
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Soldier)] > 0); // soldiers from 5
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Dragon)] == 0);
    CHECK(ForceFor(6).most[static_cast<std::size_t>(Unit::Dragon)] == 1); // the dragon at 6
}

TEST_CASE("guards hit CJ gently at 1 star, half at 3, fully from 4")
{
    CHECK(GuardDamage(0) == 0.25f);
    CHECK(GuardDamage(1) == 0.25f);
    CHECK(GuardDamage(3) == 0.5f);
    CHECK(GuardDamage(4) == 1.0f);
    CHECK(GuardDamage(6) == 1.0f);
    for (int stars = 0; stars < kMaxStars; stars++) {
        CHECK(GuardDamage(stars + 1) >= GuardDamage(stars));
    }
    CHECK(GiveBack(-40.0f, 0.25f) == doctest::Approx(30.0f));  // Skyrim's damage comes negative or positive
    CHECK(GiveBack(40.0f, 0.5f) == doctest::Approx(20.0f));
    CHECK(GiveBack(40.0f, 1.0f) == 0.0f);
    CHECK(GiveBack(std::numeric_limits<float>::quiet_NaN(), 0.5f) == 0.0f);
}

TEST_CASE("the next unit: the dragon, riders, the captain, wizards, soldiers, then guards")
{
    const auto force = ForceFor(6);
    Counts     counts{};
    CHECK(NextUnit(force, counts) == Unit::Dragon);
    counts[static_cast<std::size_t>(Unit::Dragon)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Rider);
    counts[static_cast<std::size_t>(Unit::Rider)] = 3;
    CHECK(NextUnit(force, counts) == Unit::Captain);
    counts[static_cast<std::size_t>(Unit::Captain)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Wizard);
    counts[static_cast<std::size_t>(Unit::Wizard)] = 2;
    CHECK(NextUnit(force, counts) == Unit::Soldier);
    counts[static_cast<std::size_t>(Unit::Soldier)] = 4;
    CHECK(NextUnit(force, counts) == Unit::Guard);
    CHECK(TotalOf(counts) == 11);
    CHECK(NextUnit(ForceFor(2), Counts{}) == Unit::Guard);
    CHECK(std::string(NameOf(Unit::Wizard)) == "wizard");
}

TEST_CASE("the director spawns one per cooldown up to the force, and gives a breather when they're all dead")
{
    Director d;
    const auto force = ForceFor(3);
    int        alive = 0;
    float      t = 0.0f;
    float      first = -1.0f;
    while (t < 120.0f) {
        if (d.Tick(0.1f, 3, alive)) {
            if (first < 0.0f) {
                first = t;
            }
            alive++;
        }
        t += 0.1f;
    }
    CHECK(alive == force.total);  // and no more
    CHECK(first == doctest::Approx(force.cooldown).epsilon(0.05));
    // All killed: the next one waits two cooldowns.
    int   spawned = 0;
    float wait = 0.0f;
    while (spawned == 0 && wait < 60.0f) {
        spawned += d.Tick(0.1f, 3, 0) ? 1 : 0;
        wait += 0.1f;
    }
    CHECK(wait == doctest::Approx(2.0f * force.cooldown).epsilon(0.05));
    // 1 star: nothing of ours.
    Director none;
    for (int i = 0; i < 1000; i++) {
        CHECK_FALSE(none.Tick(0.1f, 1, 0));
    }
}

TEST_CASE("never in view: behind the camera is hidden, in front isn't")
{
    const Point cj{ 0.0f, 0.0f };
    const float north = 0.0f;
    const Point camera = CameraOf(cj, north);
    CHECK(camera.y == doctest::Approx(-kCameraBack));
    CHECK(InView(camera, north, { 0.0f, 2000.0f }));        // ahead
    CHECK(InView(camera, north, { 300.0f, 0.0f }));         // close by
    CHECK_FALSE(InView(camera, north, { 0.0f, -2000.0f })); // behind
    CHECK_FALSE(InView(camera, north, { 2000.0f, -500.0f }));  // off to the side, a little behind
    for (int i = 0; i < 8; i++) {
        const Point at = SpawnSpot(cj, north, i);
        const float distance = std::hypot(at.x - cj.x, at.y - cj.y);
        CHECK(distance >= kSpawnNear);
        CHECK(distance <= kSpawnFar);
        CHECK_FALSE(InView(camera, north, at));
    }
    // Facing east, behind is west.
    const Point west = SpawnSpot(cj, 1.5707963f, 0);
    CHECK(west.x < -kSpawnNear);
}

TEST_CASE("CJ's pursuit lines come after the conversation's categories")
{
    CHECK(kLineChased >= 16);
    CHECK(kLineArrested != kLineChased);
}

TEST_CASE("which vehicles a guard busts CJ out of, and how")
{
    CHECK(BustOf(VK::Kind::Car) == Bust::Door);
    CHECK(BustOf(VK::Kind::MonsterTruck) == Bust::Door);
    CHECK(BustOf(VK::Kind::Bike) == Bust::KnockOff);
    CHECK(BustOf(VK::Kind::Bicycle) == Bust::KnockOff);
    CHECK(BustOf(VK::Kind::Quad) == Bust::KnockOff);
    CHECK(BustOf(VK::Kind::Boat) == Bust::None);
    CHECK(BustOf(VK::Kind::Heli) == Bust::None);
    CHECK(BustOf(VK::Kind::Plane) == Bust::None);
}

TEST_CASE("the driver's door is on the car's left")
{
    // Heading north: the door is to the west.
    const Point north = DoorSpot({ 0.0f, 0.0f }, 0.0f);
    CHECK(north.x == doctest::Approx(-kDoorSide));
    CHECK(north.y == doctest::Approx(kDoorAhead));
    // Heading east (a quarter turn clockwise): the door is to the north.
    const Point east = DoorSpot({ 100.0f, 200.0f }, 1.5707963f);
    CHECK(east.x == doctest::Approx(100.0f + kDoorAhead).epsilon(0.001));
    CHECK(east.y == doctest::Approx(200.0f + kDoorSide).epsilon(0.001));
}

TEST_CASE("stopped for a second or two with a guard at the door: busted; moving keeps him safe")
{
    StopWatch w;
    const float near = kReach - 10.0f;
    // Moving with a guard at the door: never.
    for (int i = 0; i < 50; i++) {
        CHECK_FALSE(w.Tick(0.1f, true, Bust::Door, kStoppedSpeed * 2.0f, near));
    }
    // Stopped, a guard there: not at once.
    CHECK_FALSE(w.Tick(0.1f, true, Bust::Door, 0.0f, near));
    bool busted = false;
    float t = 0.1f;
    while (!busted && t < 5.0f) {
        busted = w.Tick(0.1f, true, Bust::Door, 0.0f, near);
        t += 0.1f;
    }
    CHECK(busted);
    CHECK(t == doctest::Approx(kStoppedFor).epsilon(0.1));
}

TEST_CASE("the stop counts only while wanted, nearly stopped, in a vehicle that can be busted, with a guard in reach")
{
    StopWatch w;
    for (int i = 0; i < 30; i++) {
        CHECK_FALSE(w.Tick(0.1f, false, Bust::Door, 0.0f, 0.0f));  // not wanted
    }
    for (int i = 0; i < 30; i++) {
        CHECK_FALSE(w.Tick(0.1f, true, Bust::None, 0.0f, 0.0f));  // a boat
    }
    for (int i = 0; i < 30; i++) {
        CHECK_FALSE(w.Tick(0.1f, true, Bust::Door, 0.0f, kReach * 3.0f));  // the guard is too far
    }
    CHECK(w.Tick(0.1f, true, Bust::Door, 0.0f, kReach));  // he has been stopped all along: the guard arrives
    w.Reset();
    // Pulling away resets the stop.
    for (int i = 0; i < 10; i++) {
        w.Tick(0.1f, true, Bust::KnockOff, 0.0f, kReach * 3.0f);
    }
    w.Tick(0.1f, true, Bust::KnockOff, kStoppedSpeed + 1.0f, kReach * 3.0f);
    CHECK(w.Stopped() == 0.0f);
    CHECK_FALSE(w.Tick(0.1f, true, Bust::KnockOff, 0.0f, 0.0f));
    CHECK_FALSE(w.Tick(0.1f, true, Bust::KnockOff, std::numeric_limits<float>::quiet_NaN(), 0.0f));
}

TEST_CASE("Busted plays the arrest, \"busted\", the fade, the move and the wake")
{
    Sequence s;
    auto f = s.Start(Arrest::Door);
    CHECK(f.action == Action::Begin);
    CHECK(f.active);
    CHECK(f.busted);
    CHECK(f.fade == 0.0f);
    CHECK(RunTo(s, Phase::Shown) == doctest::Approx(Timing{}.dragged).epsilon(0.05));
    CHECK(RunTo(s, Phase::Fading) == doctest::Approx(Timing{}.shown).epsilon(0.05));
    CHECK(RunTo(s, Phase::Moving) == doctest::Approx(Timing{}.fadeOut).epsilon(0.05));
    f = s.Tick(0.1f, false);
    CHECK(f.fade == 1.0f);
    CHECK_FALSE(f.busted);  // under the black CJ is let go
    f = s.Tick(0.1f, true);
    CHECK(f.action == Action::Release);
    CHECK_FALSE(f.active);
    RunTo(s, Phase::Idle, true);
    CHECK_FALSE(s.Running());
}

TEST_CASE("each step's action comes once, in order")
{
    Sequence s;
    s.Start(Arrest::KnockOff);
    int busted = 0, move = 0, release = 0, done = 0;
    for (int i = 0; i < 400 && s.Running(); i++) {
        const auto f = s.Tick(0.05f, true);
        busted += f.action == Action::Busted;
        move += f.action == Action::Move;
        release += f.action == Action::Release;
        done += f.action == Action::Done;
    }
    CHECK(busted == 1);
    CHECK(move == 1);
    CHECK(release == 1);
    CHECK(done == 1);
}

TEST_CASE("the arrest is as long as its animation; the barracks never opening wakes him where he is")
{
    Sequence foot, bike;
    foot.Start(Arrest::OnFoot);
    bike.Start(Arrest::KnockOff);
    CHECK(RunTo(foot, Phase::Shown) < RunTo(bike, Phase::Shown));
    Sequence s;
    s.Start(Arrest::OnFoot);
    RunTo(s, Phase::Moving);
    CHECK(RunTo(s, Phase::Waking, false) == doctest::Approx(Timing{}.moveTimeout).epsilon(0.05));
}

TEST_CASE("the fine: the whole bounty, or all the gold if short")
{
    CHECK(FineFor(1000, 5000) == 1000);
    CHECK(FineFor(1000, 300) == 300);
    CHECK(FineFor(1000, 0) == 0);
    CHECK(FineFor(0, 300) == 0);
}

TEST_CASE("the nine holds by crime faction, each with a guard to copy")
{
    CHECK(kHolds.size() == 9);
    REQUIRE(HoldOf(0x000267EA) != nullptr);
    CHECK(std::string(HoldOf(0x000267EA)->name) == "Whiterun");
    CHECK(HoldOf(0x00028849) == nullptr);  // CrimeFactionSons isn't a hold
    for (const auto& hold : kHolds) {
        CHECK(hold.guard != 0);
        CHECK(hold.barracks != 0);
        CHECK(std::string(BarracksName(hold)) != "the barracks");
        CHECK(HoldOf(hold.faction) == &hold);
    }
}

TEST_CASE("the co-save record reads back, and a broken one doesn't")
{
    Record r;
    r.heat = 1300.0f;
    r.faction = 0x000267EA;
    r.unseen = 7.5f;
    const auto bytes = Write(r);
    CHECK(bytes.size() == kSizeV2);
    Record back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back));
    CHECK(back.heat == 1300.0f);
    CHECK(back.faction == 0x000267EA);
    CHECK(back.unseen == 7.5f);
    CHECK_FALSE(Read(kVersion + 1, bytes.data(), bytes.size(), back));
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size() - 1, back));
    r.unseen = std::numeric_limits<float>::infinity();
    const auto inf = Write(r);
    CHECK_FALSE(Read(kVersion, inf.data(), inf.size(), back));
    r.unseen = 0.0f;
    r.heat = -1.0f;
    const auto negative = Write(r);
    CHECK_FALSE(Read(kVersion, negative.data(), negative.size(), back));
    Pursuit p;
    p.Restore(1.0e9f, -3.0f);
    CHECK(p.Stars() == kMaxStars);
    CHECK(p.Unseen() == 0.0f);
}

TEST_CASE("a version 1 record's stars read as the least heat they need")
{
    std::vector<std::uint8_t> v1(kSizeV1, 0);
    v1[0] = 3;
    const std::uint32_t faction = 0x000267EA;
    std::memcpy(v1.data() + 1, &faction, 4);
    Record back;
    REQUIRE(Read(1, v1.data(), v1.size(), back));
    CHECK(back.heat == HeatFor(3));
    CHECK(back.faction == faction);
    v1[0] = 9;
    CHECK_FALSE(Read(1, v1.data(), v1.size(), back));
}
