#include <cmath>

#include <doctest/doctest.h>

#include "../../cpp/reactions.h"

using namespace AhShitRim;
using namespace AhShitRim::Reactions;

TEST_CASE("who reacts: civilians and guards, not followers, fighters or creatures")
{
    CHECK(RoleOf({}) == Role::Civilian);
    CHECK(RoleOf({ .guard = true }) == Role::Guard);
    CHECK(RoleOf({ .follower = true }) == Role::None);  // followers keep fighting
    CHECK(RoleOf({ .follower = true, .guard = true }) == Role::None);
    CHECK(RoleOf({ .hostile = true }) == Role::None);   // bandits fight on
    CHECK(RoleOf({ .fighting = true }) == Role::None);
    CHECK(RoleOf({ .guard = true, .fighting = true }) == Role::None);
    CHECK(RoleOf({ .dead = true }) == Role::None);
    CHECK(RoleOf({ .npc = false }) == Role::None);      // a dog or a horse keeps to its own AI
}

TEST_CASE("confidence: the foolhardy stand their ground, the brave only don't cower")
{
    for (int c = 0; c < kBrave; ++c) {
        CHECK(Reacts(c, Cause::Aimed));
        CHECK(Reacts(c, Cause::Gunfire));
        CHECK(Reacts(c, Cause::Chainsaw));
    }
    CHECK_FALSE(Reacts(kBrave, Cause::Aimed));
    CHECK(Reacts(kBrave, Cause::Gunfire));
    CHECK(Reacts(kBrave, Cause::Chainsaw));
    CHECK_FALSE(Reacts(kFoolhardy, Cause::Aimed));
    CHECK_FALSE(Reacts(kFoolhardy, Cause::Gunfire));
    CHECK_FALSE(Reacts(kFoolhardy, Cause::Chainsaw));
}

TEST_CASE("hearing range per weapon: San Andreas's 45 m for a pistol, the silenced one only seen")
{
    CHECK(NoiseOf(Weapons::Pistol).heard == doctest::Approx(45.0f * 70.0f));
    CHECK(NoiseOf(Weapons::Mp5).heard == NoiseOf(Weapons::Pistol).heard);
    CHECK(NoiseOf(Weapons::Shotgun).heard > NoiseOf(Weapons::Pistol).heard);
    CHECK(NoiseOf(Weapons::Minigun).heard > NoiseOf(Weapons::Ak47).heard);
    CHECK(NoiseOf(Weapons::SniperRifle).sound == Sound::VeryLoud);

    const auto silenced = NoiseOf(Weapons::PistolSilenced);
    CHECK(silenced.heard < NoiseOf(Weapons::Pistol).heard / 10.0f);
    CHECK(silenced.seen == doctest::Approx(45.0f * 70.0f));
    CHECK(silenced.sound == Sound::Normal);
    CHECK(Hears(silenced, 200.0f, false));             // right next to it
    CHECK_FALSE(Hears(silenced, 1000.0f, false));      // 14 m off, looking away
    CHECK(Hears(silenced, 1000.0f, true));             // the same, watching CJ fire
    CHECK_FALSE(Hears(silenced, 4000.0f, true));       // too far to see it

    CHECK(Hears(NoiseOf(Weapons::Pistol), 3000.0f, false));
    CHECK_FALSE(Hears(NoiseOf(Weapons::Pistol), 3300.0f, false));
    CHECK_FALSE(Hears(NoiseOf(Weapons::Pistol), 3300.0f, true));  // seeing doesn't make a gun heard further

    for (auto quiet : { Weapons::Unarmed, Weapons::BaseballBat, Weapons::Chainsaw, Weapons::Grenade, Weapons::Molotov, Weapons::Camera,
             Weapons::Spraycan, Weapons::Extinguisher, Weapons::Detonator }) {
        CHECK_FALSE(NoiseOf(quiet).noisy());
    }
    CHECK(kBlastHeard > NoiseOf(Weapons::Minigun).heard);
    CHECK(kChainsawCutting > kChainsawRunning);
}

TEST_CASE("only guns make people cower, further through a scope")
{
    CHECK(AimRange(Weapons::Pistol) == kAimRange);
    CHECK(AimRange(Weapons::Tec9) == kAimRange);
    CHECK(AimRange(Weapons::Minigun) == kAimRange);
    CHECK(AimRange(Weapons::SniperRifle) == kScopeRange);
    CHECK(AimRange(Weapons::RocketLauncherHs) == kScopeRange);
    CHECK(AimRange(Weapons::Unarmed) == 0.0f);
    CHECK(AimRange(Weapons::Chainsaw) == 0.0f);
    CHECK(AimRange(Weapons::Grenade) == 0.0f);
    CHECK(AimRange(Weapons::Camera) == 0.0f);
    CHECK(AimRange(Weapons::Spraycan) == 0.0f);
}

TEST_CASE("the aim ray goes through San Andreas's crosshair")
{
    const Vec3 north{ 0.0f, 1.0f, 0.0f };
    // The middle of the screen is straight ahead.
    const auto middle = AimRay(north, {}, 70.0f, 0.5f, 0.5f);
    CHECK(middle.x == doctest::Approx(0.0f));
    CHECK(middle.y == doctest::Approx(1.0f));
    CHECK(middle.z == doctest::Approx(0.0f));

    // The free-aim crosshair sits right of and above the middle: the ray turns right and up.
    const auto aim = AimRay(north, {}, 70.0f, 0.53f, 0.4f);
    CHECK(aim.x > 0.0f);
    CHECK(aim.z > 0.0f);
    CHECK(std::sqrt(Dot(aim, aim)) == doctest::Approx(1.0f));

    // The right edge of the picture is half the field of view across.
    const auto edge = AimRay(north, {}, 90.0f, 1.0f, 0.5f);
    CHECK(std::atan2(edge.x, edge.y) == doctest::Approx(3.14159265f / 4.0f));

    // Straight down doesn't fall apart.
    const auto down = AimRay({ 0.0f, 0.0f, -1.0f }, {}, 70.0f, 0.5f, 0.5f);
    CHECK(down.z == doctest::Approx(-1.0f));
}

TEST_CASE("aimed at: the ray passes the chest, ahead of CJ and within range")
{
    const Vec3 origin{ 0.0f, 0.0f, 100.0f };
    const Vec3 ray{ 0.0f, 1.0f, 0.0f };
    // Right on, 10 m out.
    CHECK(AimedAlong(origin, ray, { 0.0f, 700.0f, 100.0f }, 50.0f, kAimRange, 20.0f) == doctest::Approx(700.0f));
    // A body and a little to the side still counts, two bodies don't.
    CHECK(AimedAlong(origin, ray, { 50.0f, 700.0f, 100.0f }, 50.0f, kAimRange, 20.0f) > 0.0f);
    CHECK(AimedAlong(origin, ray, { 150.0f, 700.0f, 100.0f }, 50.0f, kAimRange, 20.0f) < 0.0f);
    // The cone widens further out: 3 degrees at 40 m is about 2 m.
    CHECK(AimedAlong(origin, ray, { 150.0f, 2800.0f, 100.0f }, 50.0f, kAimRange, 20.0f) > 0.0f);
    // Behind CJ, past the range, or behind the camera don't.
    CHECK(AimedAlong(origin, ray, { 0.0f, 30.0f, 100.0f }, 50.0f, kAimRange, 20.0f) < 0.0f);
    CHECK(AimedAlong(origin, ray, { 0.0f, -700.0f, 100.0f }, 50.0f, kAimRange, 20.0f) < 0.0f);
    CHECK(AimedAlong(origin, ray, { 0.0f, kAimRange + 10.0f, 100.0f }, 50.0f, kAimRange, 20.0f) < 0.0f);
    CHECK(AimedAlong(origin, ray, { 0.0f, kAimRange + 10.0f, 100.0f }, 50.0f, kScopeRange, 20.0f) > 0.0f);
}

TEST_CASE("cowering lasts while aimed at, a flee wins over it and is renewed now and then")
{
    Scares s;
    CHECK(s.StartCower(1, 0.0));        // starts
    CHECK_FALSE(s.StartCower(1, 1.0));  // goes on
    CHECK(s.DoingOf(1) == Doing::Cowering);
    CHECK(s.Expired(1.0 + kCowerHold - 0.1).empty());
    const auto out = s.Expired(1.0 + kCowerHold);
    REQUIRE(out.size() == 1);
    CHECK(out[0].first == 1);
    CHECK(out[0].second == Doing::Cowering);
    CHECK(s.DoingOf(1) == Doing::Nothing);

    // Shot at while cowering: it flees, and aiming again doesn't turn it back.
    CHECK(s.StartCower(2, 0.0));
    CHECK(s.StartFlee(2, 1.0));
    CHECK(s.DoingOf(2) == Doing::Fleeing);
    CHECK_FALSE(s.StartCower(2, 2.0));
    CHECK(s.DoingOf(2) == Doing::Fleeing);
    // More shots: started over only every kFleeRenew, and each keeps it going.
    CHECK_FALSE(s.StartFlee(2, 1.0 + kFleeRenew - 0.5));
    CHECK(s.StartFlee(2, 1.0 + kFleeRenew));
    CHECK(s.Expired(1.0 + kFleeRenew + kFleeLasts - 0.1).empty());
    CHECK(s.Expired(1.0 + kFleeRenew + kFleeLasts).size() == 1);

    CHECK(s.StartFlee(3, 0.0));
    CHECK(s.StartCower(4, 0.0));
    CHECK(s.All().size() == 2);
    CHECK(s.Size() == 0);
}

TEST_CASE("a bounty for gunfire: once a minute per hold, never while the player fights")
{
    Bounties b;
    CHECK(b.Due(0x29DB0, false, 0.0));  // Whiterun's crime faction
    CHECK_FALSE(b.Due(0x29DB0, false, 10.0));
    CHECK(b.Due(0x28170, false, 10.0));  // another hold's
    CHECK(b.Due(0x29DB0, false, kBountyEvery));
    CHECK_FALSE(b.Due(0x267E3, true, 0.0));  // fighting: defending itself
    CHECK(b.Due(0x267E3, false, 1.0));
    CHECK_FALSE(b.Due(0, false, 0.0));       // no hold
    b.Clear();
    CHECK(b.Due(0x29DB0, false, kBountyEvery + 1.0));
}

TEST_CASE("shots counted across the counter's wrap")
{
    CHECK(ShotsSince(10, 13) == 3);
    CHECK(ShotsSince(0xFFFE, 1) == 3);
    CHECK(ShotsSince(7, 7) == 0);
}
