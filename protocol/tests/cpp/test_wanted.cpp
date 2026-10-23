#include <doctest/doctest.h>

#include <cmath>
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
    Look Hide(Pursuit& a_pursuit, float a_seconds, int a_bounty)
    {
        Look look;
        for (float t = 0.0f; t < a_seconds; t += 0.1f) {
            look = a_pursuit.Tick(0.1f, false, a_bounty, false);
        }
        return look;
    }
}

TEST_CASE("stars for a bounty: any is one, an assault two, a murder four, six at the top")
{
    CHECK(StarsForBounty(0) == 0);
    CHECK(StarsForBounty(5) == 1);    // trespass
    CHECK(StarsForBounty(39) == 1);
    CHECK(StarsForBounty(40) == 2);   // an assault
    CHECK(StarsForBounty(499) == 2);
    CHECK(StarsForBounty(500) == 3);
    CHECK(StarsForBounty(1000) == 4); // a murder
    CHECK(StarsForBounty(3000) == 5);
    CHECK(StarsForBounty(6000) == 6);
    CHECK(StarsForBounty(1000000) == kMaxStars);
    CHECK(StarsForBounty(-5) == 0);
}

TEST_CASE("stars for the crime just committed")
{
    CHECK(StarsForCrime(0) == 0);
    CHECK(StarsForCrime(5) == 1);
    CHECK(StarsForCrime(40) == 2);
    CHECK(StarsForCrime(1000) == 4);
}

TEST_CASE("a crime raises the stars, a violent one while wanted adds one more")
{
    Pursuit p;
    CHECK(p.Crime(5, 5, false) == 1);     // trespass
    CHECK(p.Crime(10, 5, false) == 1);    // more trespass: still one
    CHECK(p.Crime(50, 40, false) == 2);   // assaulting a guard
    CHECK(p.Crime(50, 40, false) == 3);   // and again, wanted already: one more
    CHECK(p.Crime(1050, 1000, false) == 4);  // he dies
    CHECK(p.Crime(2050, 1000, false) == 5);
    CHECK(p.Crime(3050, 1000, false) == 6);
    CHECK(p.Crime(4050, 1000, false) == 6);  // six at most
}

TEST_CASE("shooting a Whiterun guard: two stars, three as it goes on, then the riders")
{
    Pursuit p;
    CHECK(p.Crime(40, 40, false) == 2);
    CHECK(ResponseFor(p.Stars()).riders == 0);
    CHECK(p.Crime(40, 40, false) == 3);  // Skyrim counts another assault: three stars
    CHECK(ResponseFor(p.Stars()).riders == 1);
}

TEST_CASE("AEZAKMI: no stars from any crime")
{
    Pursuit p;
    CHECK(p.Crime(1000, 1000, true) == 0);
    p.Recognised(1000, true);
    CHECK(p.Stars() == 0);
    p.Crime(40, 40, false);
    CHECK(p.Tick(0.1f, true, 40, true).stars == 0);  // typed while wanted: gone at once
}

TEST_CASE("out of sight the stars flash, then go; the bounty doesn't")
{
    Pursuit p;
    p.Crime(50, 40, false);
    p.Crime(50, 40, false);  // three stars
    REQUIRE(p.Stars() == 3);
    Look look = p.Tick(0.1f, true, 50, false);
    CHECK(look.flashing);  // they just changed
    for (int i = 0; i < 30; i++) {
        look = p.Tick(0.1f, true, 50, false);
    }
    CHECK(look.stars == 3);
    CHECK_FALSE(look.flashing);  // seen, settled
    look = p.Tick(0.1f, false, 50, false);
    CHECK(look.flashing);  // out of sight
    look = Hide(p, LoseSeconds(3) - 1.0f, 50);
    CHECK(look.stars == 3);
    CHECK(look.flashing);
    look = Hide(p, 1.5f, 50);
    CHECK(look.stars == 0);
    CHECK_FALSE(look.flashing);
}

TEST_CASE("being seen again holds the stars; more stars take longer to lose")
{
    CHECK(LoseSeconds(1) < LoseSeconds(3));
    CHECK(LoseSeconds(3) < LoseSeconds(6));
    Pursuit p;
    p.Crime(1000, 1000, false);
    Hide(p, LoseSeconds(4) - 1.0f, 1000);
    p.Tick(0.1f, true, 1000, false);  // a guard spots him
    CHECK(p.Unseen() == 0.0f);
    CHECK(Hide(p, LoseSeconds(4) - 1.0f, 1000).stars == 4);
}

TEST_CASE("a guard who fights him later recognises him by the bounty")
{
    Pursuit p;
    p.Crime(1000, 1000, false);
    Hide(p, LoseSeconds(4) + 1.0f, 1000);
    REQUIRE(p.Stars() == 0);
    p.Recognised(1000, false);
    CHECK(p.Stars() == 4);
    p.Recognised(5000, false);  // already wanted: no change
    CHECK(p.Stars() == 4);
    Pursuit clean;
    clean.Recognised(0, false);
    CHECK(clean.Stars() == 0);
}

TEST_CASE("no bounty, no stars: the fine paid or Wasted forgiving it")
{
    Pursuit p;
    p.Crime(1000, 1000, false);
    CHECK(p.Tick(0.1f, true, 0, false).stars == 0);
}

TEST_CASE("more guards join as the stars rise, riders from 3, soldiers from 5")
{
    CHECK(ResponseFor(0).joinRadius == 0.0f);
    CHECK(ResponseFor(1).joinRadius == 0.0f);
    CHECK(ResponseFor(2).riders == 0);
    CHECK(ResponseFor(3).riders == 1);
    CHECK(ResponseFor(4).soldiers == 0);
    CHECK(ResponseFor(5).soldiers > 0);
    for (int stars = 1; stars < kMaxStars; stars++) {
        CHECK(ResponseFor(stars + 1).joinRadius >= ResponseFor(stars).joinRadius);
        CHECK(ResponseFor(stars + 1).riders >= ResponseFor(stars).riders);
        CHECK(ResponseFor(stars + 1).soldiers >= ResponseFor(stars).soldiers);
    }
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
        CHECK(HoldOf(hold.faction) == &hold);
    }
}

TEST_CASE("the co-save record reads back, and a broken one doesn't")
{
    Record r;
    r.stars = 4;
    r.faction = 0x000267EA;
    r.unseen = 7.5f;
    const auto bytes = Write(r);
    CHECK(bytes.size() == kSizeV1);
    Record back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back));
    CHECK(back.stars == 4);
    CHECK(back.faction == 0x000267EA);
    CHECK(back.unseen == 7.5f);
    CHECK_FALSE(Read(kVersion + 1, bytes.data(), bytes.size(), back));
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size() - 1, back));
    auto bad = bytes;
    bad[0] = 9;
    CHECK_FALSE(Read(kVersion, bad.data(), bad.size(), back));
    r.unseen = std::numeric_limits<float>::infinity();
    const auto inf = Write(r);
    CHECK_FALSE(Read(kVersion, inf.data(), inf.size(), back));
    Pursuit p;
    p.Restore(9, -3.0f);
    CHECK(p.Stars() == kMaxStars);
    CHECK(p.Unseen() == 0.0f);
}
