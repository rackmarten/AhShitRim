#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include <doctest/doctest.h>

#include "../../cpp/reactions.h"

using namespace AhShitRim;
using namespace AhShitRim::Reactions;

TEST_CASE("who reacts: civilians and guards, not followers, fighters or creatures")
{
    CHECK(RoleOf({}) == Role::Civilian);
    CHECK(RoleOf({ .guard = true }) == Role::Guard);  // tells CJ off for the "magic"
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

    // What makes a bang is magic to Skyrim.
    CHECK(IsMagic(Weapons::Pistol));
    CHECK(IsMagic(Weapons::PistolSilenced));
    CHECK(IsMagic(Weapons::Flamethrower));
    CHECK(IsMagic(Weapons::RocketLauncher));
    CHECK_FALSE(IsMagic(Weapons::Unarmed));
    CHECK_FALSE(IsMagic(Weapons::Chainsaw));
    CHECK_FALSE(IsMagic(Weapons::Grenade));
    CHECK_FALSE(IsMagic(Weapons::Spraycan));
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

TEST_CASE("lines about the magic: one at a time, not the same person over and over")
{
    Comments c;
    CHECK(c.Due(1, 0.0));
    c.Said(1, 0.0);
    CHECK_FALSE(c.Due(2, kCommentGap - 0.1));  // someone else, too soon after
    CHECK(c.Due(2, kCommentGap));
    c.Said(2, kCommentGap);
    CHECK_FALSE(c.Due(1, kCommentGap * 3.0));  // the first again, too soon for them
    CHECK(c.Due(1, kCommentRepeat));
    CHECK(c.Due(3, kCommentRepeat));
}

TEST_CASE("every line has a voice, every pool has lines for its speakers")
{
    // Every voice type is in the table once, and every line has a voice file for at least one of them.
    for (std::size_t i = 0; i < std::size(kVoiceTypes); ++i) {
        CHECK(VoiceBit(kVoiceTypes[i]) == 1ull << i);
    }
    CHECK(std::size(kVoiceTypes) <= 64);
    CHECK(VoiceBit(0) == 0);
    CHECK(VoiceBit(0x0001B560) == 0);  // FemaleSoldier: no line here has her voice
    for (auto moment : { Moment::GuardHears, Moment::GuardAimedAt, Moment::Loud, Moment::CivilianAimedAt, Moment::CivilianHears }) {
        for (const auto& line : LinesFor(moment, Weapons::Flamethrower)) {
            CHECK(line.info != 0);
            CHECK(line.voices != 0);
            CHECK((line.voices & ~Voice::kAny) == 0);
        }
    }

    // The guards' voices have several lines for each moment, the loud ones included.
    for (auto voice : { Voice::MaleGuard, Voice::FemaleNord, Voice::MaleNordCommander }) {
        for (auto moment : { Moment::GuardHears, Moment::GuardAimedAt, Moment::Loud }) {
            int fits = 0;
            for (const auto& line : LinesFor(moment, Weapons::Pistol)) {
                fits += (line.voices & voice) != 0;
            }
            CHECK(fits >= 3);
        }
    }
    // A civilian of every common voice has more than one line when aimed at and when it hears a shout.
    for (auto voice : { Voice::MaleNord, Voice::FemaleCommoner, Voice::MaleDrunk, Voice::FemaleOldKindly, Voice::MaleKhajiit }) {
        int aimed = 0, loud = 0, fire = 0;
        for (const auto& line : LinesFor(Moment::CivilianAimedAt, Weapons::Pistol)) {
            aimed += (line.voices & voice) != 0;
        }
        for (const auto& line : LinesFor(Moment::Loud, Weapons::Minigun)) {
            loud += (line.voices & voice) != 0;
        }
        for (const auto& line : LinesFor(Moment::CivilianAimedAt, Weapons::Flamethrower)) {
            fire += (line.voices & voice) != 0;
        }
        CHECK(aimed >= 3);
        CHECK(loud >= 2);
        CHECK(fire > aimed);  // the flamethrower adds the fire remarks
    }
    // The guards' own lines stay theirs.
    CHECK(LinesFor(Moment::GuardAimedAt, Weapons::Flamethrower).size() == std::size(kGuardAimedAt));
}

TEST_CASE("the loud weapons are a shout")
{
    CHECK(IsLoud(Weapons::Minigun));
    CHECK(IsLoud(Weapons::RocketLauncher));
    CHECK(IsLoud(Weapons::RocketLauncherHs));
    CHECK_FALSE(IsLoud(Weapons::Pistol));
    CHECK_FALSE(IsLoud(Weapons::Ak47));
    CHECK_FALSE(IsLoud(Weapons::Flamethrower));
}

TEST_CASE("picking a line: in the speaker's voice, every one before any comes again")
{
    const std::vector<Line> lines{ { 10, 0b011 }, { 11, 0b001 }, { 12, 0b001 }, { 13, 0b010 }, { 11, 0b001 } };
    Comments                c;
    CHECK(c.Pick(1, 0, lines, 0) == 0);      // a voice no line has
    CHECK(c.Pick(1, 0b100, lines, 0) == 0);

    // Voice 0b001 has 10, 11 and 12 (11 twice in the pool, once in the round): three different ones, whatever
    // the dice say, then a new round.
    std::vector<std::uint32_t> heard;
    double                     now = 0.0;
    for (int i = 0; i < 3; ++i) {
        const auto id = c.Pick(1, 0b001, lines, 0);
        CHECK(id != 13);
        CHECK(std::find(heard.begin(), heard.end(), id) == heard.end());
        heard.push_back(id);
        c.Said(1, now, id);
        now += kCommentRepeat;
    }
    const auto again = c.Pick(1, 0b001, lines, 0);
    CHECK(again != heard.back());  // a new round, but not straight away the line just said
    c.Said(1, now, again);

    // Someone else has their own round, and avoids the line said last by anyone when it can.
    const auto other = c.Pick(2, 0b001, lines, 0);
    CHECK(other != again);
    for (std::uint32_t r = 0; r < 8; ++r) {
        CHECK(c.Pick(2, 0b001, lines, r) != again);
    }

    // With one line in their voice, it is said every time.
    const std::vector<Line> one{ Line{ 13, 0b010 } };
    CHECK(c.Pick(3, 0b010, one, 5) == 13);
    c.Said(3, now, 13);
    CHECK(c.Pick(3, 0b010, one, 5) == 13);
}

TEST_CASE("shots counted across the counter's wrap")
{
    CHECK(ShotsSince(10, 13) == 3);
    CHECK(ShotsSince(0xFFFE, 1) == 3);
    CHECK(ShotsSince(7, 7) == 0);
}
