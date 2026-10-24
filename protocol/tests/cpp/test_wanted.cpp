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
    CHECK(StarsForHeat(3599.0f) == 5);
    CHECK(StarsForHeat(3600.0f) == 6);
    CHECK(HeatFor(6) - HeatFor(5) == HeatFor(5) - HeatFor(4));  // the sixth star takes as much as the fifth
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
    CHECK(kills(7) == 5);  // twenty-two
    CHECK(kills(1) == 6);  // twenty-three
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
        CHECK(next.breather < now.breather);  // a shorter gap the more stars
        CHECK(now.breather > now.cooldown);
        int special = 0;
        for (const int n : now.most) {
            special += n;
        }
        CHECK(special <= now.total);
    }
    CHECK(ForceFor(kMaxStars).total == kForceCap);
    CHECK(ForceFor(2).most[static_cast<std::size_t>(Unit::Wolf)] == 0);
    CHECK(ForceFor(3).most[static_cast<std::size_t>(Unit::Wolf)] == 2);   // a couple of wolves at 3, a pack that grows
    for (int stars = 3; stars < kMaxStars; stars++) {
        CHECK(ForceFor(stars + 1).most[static_cast<std::size_t>(Unit::Wolf)] >= ForceFor(stars).most[static_cast<std::size_t>(Unit::Wolf)]);
    }
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Wolf)] == 4);
    CHECK(ForceFor(4).most[static_cast<std::size_t>(Unit::Wizard)] > 0);  // a guard wizard from 4
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Soldier)] > 0); // soldiers from 5
    CHECK(ForceFor(4).most[static_cast<std::size_t>(Unit::Dragon)] == 0);
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Dragon)] == 1); // a dragon from 5
    CHECK(ForceFor(5).most[static_cast<std::size_t>(Unit::Giant)] == 0);
    CHECK(ForceFor(6).most[static_cast<std::size_t>(Unit::Giant)] == 2);  // giants and a mammoth at 6
    CHECK(ForceFor(6).most[static_cast<std::size_t>(Unit::Mammoth)] == 1);
}

TEST_CASE("guards hit CJ a quarter with no stars, 0.4 at 1, more with each star, fully from 5")
{
    CHECK(GuardDamage(0) == 0.25f);
    CHECK(GuardDamage(1) == doctest::Approx(0.4f));
    CHECK(GuardDamage(1) >= 0.25f);  // tier 1 at least a quarter (the user's call after testing)
    CHECK(GuardDamage(3) == doctest::Approx(0.7f));
    CHECK(GuardDamage(4) < 1.0f);
    CHECK(GuardDamage(5) == 1.0f);
    CHECK(GuardDamage(6) == 1.0f);
    for (int stars = 0; stars < kMaxStars - 1; stars++) {
        CHECK(GuardDamage(stars + 1) > GuardDamage(stars));  // every star up to 5 hits harder
    }
    CHECK(GiveBack(-40.0f, 0.25f) == doctest::Approx(30.0f));  // Skyrim's damage comes negative or positive
    CHECK(GiveBack(40.0f, 0.5f) == doctest::Approx(20.0f));
    CHECK(GiveBack(40.0f, 1.0f) == 0.0f);
    CHECK(GiveBack(std::numeric_limits<float>::quiet_NaN(), 0.5f) == 0.0f);
}

TEST_CASE("the next unit: the dragon, giants, the mammoth, wolves, the captain, wizards, soldiers, then guards")
{
    const auto force = ForceFor(6);
    Counts     counts{};
    CHECK(NextUnit(force, counts) == Unit::Dragon);
    CHECK(NextUnit(force, counts, false) == Unit::Wolf);  // in a city: no dragon, giants or mammoth
    counts[static_cast<std::size_t>(Unit::Dragon)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Giant);
    counts[static_cast<std::size_t>(Unit::Giant)] = 2;
    CHECK(NextUnit(force, counts) == Unit::Mammoth);
    counts[static_cast<std::size_t>(Unit::Mammoth)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Wolf);
    counts[static_cast<std::size_t>(Unit::Wolf)] = 4;
    CHECK(NextUnit(force, counts) == Unit::Captain);
    counts[static_cast<std::size_t>(Unit::Captain)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Wizard);
    counts[static_cast<std::size_t>(Unit::Wizard)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Soldier);
    counts[static_cast<std::size_t>(Unit::Soldier)] = 1;
    CHECK(NextUnit(force, counts) == Unit::Guard);
    CHECK(TotalOf(counts) == 11);
    CHECK(OpenWorldOnly(Unit::Mammoth));
    CHECK_FALSE(OpenWorldOnly(Unit::Soldier));
    CHECK_FALSE(OpenWorldOnly(Unit::Wolf));  // wolves come into a city too
    CHECK(std::string(NameOf(Unit::Giant)) == "giant");
    CHECK(NextUnit(ForceFor(2), Counts{}) == Unit::Guard);
    CHECK(NextUnit(ForceFor(3), Counts{}) == Unit::Wolf);  // at 3 stars the wolves come first
    CHECK(std::string(NameOf(Unit::Wizard)) == "wizard");
    CHECK(std::string(NameOf(Unit::Wolf)) == "wolf");
}

TEST_CASE("wolves: ice wolves from 5 stars, and they don't bust CJ")
{
    CHECK(WolfFor(3) == kWolf);
    CHECK(WolfFor(4) == kWolf);
    CHECK(WolfFor(5) == kIceWolf);
    CHECK(WolfFor(6) == kIceWolf);
    CHECK_FALSE(Arrests(Unit::Wolf));
    CHECK_FALSE(Arrests(Unit::Dragon));
    CHECK(Arrests(Unit::Guard));
    CHECK(Arrests(Unit::Captain));
}

TEST_CASE("a wolf keeps up with CJ on foot or in a car, and catches up from behind")
{
    // Close by and CJ sprinting (about the wolf's own run) or standing: Skyrim's own speed.
    CHECK(ChaseSpeedMult(300.0f, 0.0f) == 100.0f);
    CHECK(ChaseSpeedMult(kChaseNear, 520.0f) == 100.0f);
    // Left behind on foot: faster, up to half again.
    CHECK(ChaseSpeedMult(2560.0f, 520.0f) == doctest::Approx(140.0f));
    CHECK(ChaseSpeedMult(kChaseFar, 520.0f) == 150.0f);
    CHECK(ChaseSpeedMult(20000.0f, 0.0f) == 150.0f);
    // CJ in a car at 25 m/s (1750 units a second): as fast as the car beside it, faster behind it.
    CHECK(ChaseSpeedMult(500.0f, 1750.0f) == doctest::Approx(310.0f));
    CHECK(ChaseSpeedMult(kChaseFar, 1750.0f) == doctest::Approx(470.0f));
    // A fast car: kChaseMost at most.
    CHECK(ChaseSpeedMult(kChaseFar, 4000.0f) == kChaseMost);
    // Never slower the further behind, nor the faster CJ goes; whole steps only.
    for (float d = 0.0f; d < 5000.0f; d += 250.0f) {
        for (float v = 0.0f; v < 4000.0f; v += 250.0f) {
            const float m = ChaseSpeedMult(d, v);
            CHECK(ChaseSpeedMult(d + 250.0f, v) >= m);
            CHECK(ChaseSpeedMult(d, v + 250.0f) >= m);
            CHECK(std::fmod(m, kChaseStep) == 0.0f);
            CHECK(m >= 100.0f);
            CHECK(m <= kChaseMost);
        }
    }
    CHECK(ChaseSpeedMult(std::numeric_limits<float>::quiet_NaN(), 500.0f) == 100.0f);
    CHECK(ChaseSpeedMult(500.0f, std::numeric_limits<float>::infinity()) == 100.0f);
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
    CHECK(first == doctest::Approx(kFirstSpawn).epsilon(0.1));  // the first comes soon after the stars
    // All killed: the next one waits the breather.
    int   spawned = 0;
    float wait = 0.0f;
    while (spawned == 0 && wait < 60.0f) {
        spawned += d.Tick(0.1f, 3, 0) ? 1 : 0;
        wait += 0.1f;
    }
    CHECK(wait == doctest::Approx(force.breather).epsilon(0.05));
    // Then one per cooldown again, the breather only after a wipe-out.
    int   more = 1;
    float again = 0.0f;
    while (more < 3 && again < 60.0f) {
        more += d.Tick(0.1f, 3, more) ? 1 : 0;
        again += 0.1f;
    }
    CHECK(again == doctest::Approx(2.0f * force.cooldown).epsilon(0.05));
    // 1 star: nothing of ours.
    Director none;
    for (int i = 0; i < 1000; i++) {
        CHECK_FALSE(none.Tick(0.1f, 1, 0));
    }
}

TEST_CASE("wiped out, the next wave comes before CJ loses a star just for nobody being left to see him")
{
    for (int stars = 2; stars <= kMaxStars; stars++) {
        const auto force = ForceFor(stars);
        CAPTURE(stars);
        CHECK(force.breather + kArriveSeconds < kFlashAfter + LoseSeconds(stars));
    }
    // Played out at 2 stars: everyone dead, CJ standing where he was, the next one spawns while the stars still show.
    Pursuit pursuit;
    pursuit.Restore(HeatFor(2), 0.0f);
    Director d;
    int   alive = 1;
    float t = 0.0f;
    while (alive > 0 && t < 30.0f) {
        d.Tick(0.1f, pursuit.Stars(), alive);
        alive = 0;  // all killed at once
        t += 0.1f;
    }
    bool came = false;
    for (float s = 0.0f; s < 30.0f && !came; s += 0.1f) {
        came = d.Tick(0.1f, pursuit.Stars(), 0);
        pursuit.Tick(0.1f, false, false);
    }
    CHECK(came);
    CHECK(pursuit.Stars() == 2);
}

TEST_CASE("a spawn with no spot is tried again within a second or so, not a whole cooldown later")
{
    Director d;
    float t = 0.0f;
    while (!d.Tick(0.1f, 2, 0) && t < 30.0f) {
        t += 0.1f;
    }
    d.Retry();
    float wait = 0.0f;
    while (!d.Tick(0.1f, 2, 0) && wait < 30.0f) {
        wait += 0.1f;
    }
    CHECK(wait <= kRetrySeconds + 0.15f);
}

TEST_CASE("the director starts over once the stars are gone")
{
    Director d;
    for (int i = 0; i < 100; i++) {
        d.Tick(0.1f, 4, 2);
    }
    CHECK_FALSE(d.Tick(0.1f, 0, 2));
    float t = 0.0f;
    while (!d.Tick(0.1f, 2, 0) && t < 30.0f) {
        t += 0.1f;
    }
    CHECK(t == doctest::Approx(kFirstSpawn).epsilon(0.15));
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
    float nearest = kSpawnFar, furthest = 0.0f;
    for (int i = 0; i < 24; i++) {
        const Point at = SpawnSpot(cj, north, i);
        const float distance = std::hypot(at.x - cj.x, at.y - cj.y);
        CHECK(distance >= kSpawnNear);
        CHECK(distance <= kSpawnFar);
        CHECK_FALSE(InView(camera, north, at));
        nearest = (std::min)(nearest, distance);
        furthest = (std::max)(furthest, distance);
        for (int j = 0; j < i; j++) {  // 24 different spots before one comes again
            const Point other = SpawnSpot(cj, north, j);
            CHECK(std::hypot(at.x - other.x, at.y - other.y) > 1.0f);
        }
    }
    CHECK(furthest - nearest > 1000.0f);  // near, middling and far
    CHECK(SpawnSpot(cj, north, 24).x == doctest::Approx(SpawnSpot(cj, north, 0).x));
    // 15 to 40 m, in Skyrim units (70 a metre).
    CHECK(kSpawnNear >= 15.0f * 70.0f);
    CHECK(kSpawnFar <= 40.0f * 70.0f);
    // Facing east, behind is west.
    const Point west = SpawnSpot(cj, 1.5707963f, 0);
    CHECK(west.x < -kSpawnNear);
}

namespace
{
    // Ticks a leash a_seconds in 0.1 s frames on a_target; returns how often it pulled.
    int Pulls(Leash& a_leash, Target a_target, float a_seconds)
    {
        int pulls = 0;
        for (float t = 0.0f; t < a_seconds - 1e-4f; t += 0.1f) {
            pulls += a_leash.Tick(0.1f, a_target) ? 1 : 0;
        }
        return pulls;
    }
}

TEST_CASE("the leash: on CJ never pulled, a stray after a while, a lost one sooner, none more often than the gap")
{
    Leash leash;
    CHECK(Pulls(leash, Target::Cj, 60.0f) == 0);
    CHECK(Pulls(leash, Target::Other, kStrayFor - 0.5f) == 0);  // a short fight with someone else is left alone
    CHECK(Pulls(leash, Target::Other, 1.0f) == 1);
    CHECK(Pulls(leash, Target::Other, 60.0f) <= static_cast<int>(60.0f / kLeashGap) + 1);  // at most once a gap
    CHECK(Pulls(leash, Target::Other, 60.0f) >= static_cast<int>(60.0f / (kStrayFor + 0.2f)) - 1);

    Leash lost;
    CHECK(Pulls(lost, Target::None, kLostFor - 0.3f) == 0);
    CHECK(Pulls(lost, Target::None, 0.5f) == 1);
    CHECK(Pulls(lost, Target::None, kLeashGap - 0.5f) == 0);  // not again before the gap

    Leash back;
    Pulls(back, Target::Other, kStrayFor - 1.0f);
    Pulls(back, Target::Cj, 0.5f);  // back on CJ: the time away starts over
    CHECK(Pulls(back, Target::Other, kStrayFor - 1.0f) == 0);

    Leash pulled;
    pulled.Pulled();  // a rejoin just set it on CJ
    CHECK(Pulls(pulled, Target::None, kLeashGap - 0.5f) == 0);
    CHECK(Pulls(pulled, Target::None, 1.0f) == 1);
    CHECK(kLeashGap >= 3.0f);  // a path takes a moment to build
}

TEST_CASE("ours are one side: the hold's guards' factions, their own left, the crime faction always")
{
    const std::vector<std::uint32_t> guards = { 0x10, 0x11, 0x12 };  // say GuardFaction, IsGuardFaction, the hold's guards
    const std::uint32_t              crime = 0x267EA;
    const auto giant = Enlist({ 0x50, 0x51 }, guards, crime);  // GiantFaction, CreatureFaction
    CHECK(giant.join == std::vector<std::uint32_t>{ 0x10, 0x11, 0x12, crime });
    CHECK(giant.leave == std::vector<std::uint32_t>{ 0x50, 0x51 });

    const auto guard = Enlist({ 0x10, 0x11, 0x12, crime }, guards, crime);  // a copy of a guard: already one of them
    CHECK(guard.leave.empty());
    CHECK(guard.join.size() == 4);

    const auto noCrime = Enlist({ 0x50, 0x50 }, { 0x10, crime }, crime);  // no doubles either way
    CHECK(noCrime.join == std::vector<std::uint32_t>{ 0x10, crime });
    CHECK(noCrime.leave == std::vector<std::uint32_t>{ 0x50 });
    CHECK(Enlist({}, {}, 0).join.empty());
    CHECK(kOursAggression == 1.0f);  // the player (an enemy), not everyone
}

TEST_CASE("ours copy generic guards only, never a unique or named NPC")
{
    CHECK(Copyable(false, "Whiterun Guard", "Whiterun Guard"));
    CHECK(Copyable(false, "Windhelm Guard", "Whiterun Guard"));  // a hold's guards by another name of the same kind
    CHECK(Copyable(false, "Guard", "Whiterun Guard"));
    CHECK(Copyable(false, "Imperial Soldier", "Whiterun Guard"));
    CHECK(Copyable(false, "Stormcloak Battlemage", "Whiterun Guard"));
    CHECK_FALSE(Copyable(true, "Whiterun Guard", "Whiterun Guard"));     // flagged unique, whatever he's called
    CHECK_FALSE(Copyable(false, "Commander Caius", "Whiterun Guard"));   // a name of his own
    CHECK_FALSE(Copyable(true, "Commander Caius", "Whiterun Guard"));
    CHECK_FALSE(Copyable(false, "Irileth", "Whiterun Guard"));
    CHECK_FALSE(Copyable(false, "Guard Captain", "Whiterun Guard"));
    CHECK_FALSE(Copyable(false, "Lifeguard", "Whiterun Guard"));         // a word that only ends like one
    CHECK_FALSE(Copyable(false, "", "Whiterun Guard"));
}

TEST_CASE("a levelled list's soldier: anyone up to the player's level, else the lowest")
{
    const std::uint16_t levels[] = { 1, 10, 25, 40 };
    for (unsigned roll = 0; roll < 20; roll++) {
        const int i = PickLeveled(levels, 4, 12, roll);
        CHECK((i == 0 || i == 1));
    }
    CHECK(PickLeveled(levels, 4, 12, 0) != PickLeveled(levels, 4, 12, 1));  // the roll picks among them
    CHECK(PickLeveled(levels, 4, 50, 3) == 3);
    const std::uint16_t high[] = { 30, 20, 40 };
    CHECK(PickLeveled(high, 3, 5, 7) == 1);  // nobody that low: the lowest
    CHECK(PickLeveled(levels, 0, 10, 0) == -1);
    CHECK(PickLeveled(nullptr, 3, 10, 0) == -1);
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
