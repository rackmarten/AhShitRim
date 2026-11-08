#include <ostream>  // before doctest, so it can print string_view
#include <string_view>

#include <doctest/doctest.h>

#include "../../cpp/stealth_kill.h"

using namespace AhShitRim;
using namespace AhShitRim::StealthKill;

namespace
{
    constexpr float kNorth = 0.0f;  // Skyrim headings
    constexpr float kEast = 0.5f * Pose::kPi;
    constexpr float kSouth = Pose::kPi;

    float M(float a_metres) { return a_metres * Pose::kUnitsPerMeter; }

    // An unaware guard 2 m north of CJ (at the origin), facing north: his back to CJ.
    Target Guard()
    {
        Target t;
        t.person = true;
        t.feet = { 0.0f, M(2.0f), 0.0f };
        t.heading = kNorth;
        return t;
    }

    const Vec3 kCj{};
}

TEST_CASE("stealth kill: people only")
{
    // Playable races and the like carry ActorTypeNPC.
    CHECK(IsPerson("NordRace", true, false));
    CHECK(IsPerson("KhajiitRace", true, false));
    CHECK(IsPerson("DremoraRace", true, false));
    // Draugr and falmer stand and fall like people without it.
    CHECK(IsPerson("DraugrRace", false, false));
    CHECK(IsPerson("DraugrMagicRace", false, false));
    CHECK(IsPerson("FalmerRace", false, false));
    // Creatures, giants, dragons and the beast forms never.
    CHECK_FALSE(IsPerson("WolfRace", false, false));
    CHECK_FALSE(IsPerson("TrollRace", false, false));
    CHECK_FALSE(IsPerson("GiantRace", true, false));
    CHECK_FALSE(IsPerson("DragonRace", false, true));
    CHECK_FALSE(IsPerson("WerewolfBeastRace", true, false));
    CHECK_FALSE(IsPerson("DLC1VampireBeastRace", true, false));
}

TEST_CASE("stealth kill: an unaware person with his back to CJ can be killed")
{
    CHECK(Check(kCj, Weapons::Knife, Guard()) == Verdict::Yes);
    CHECK(std::string_view(Why(Verdict::Yes)) == "can be killed");
}

TEST_CASE("stealth kill: only with the knife")
{
    CHECK(Check(kCj, Weapons::Katana, Guard()) == Verdict::NotKnife);
    CHECK(Check(kCj, Weapons::Unarmed, Guard()) == Verdict::NotKnife);
    CHECK(Check(kCj, Weapons::Pistol, Guard()) == Verdict::NotKnife);
}

TEST_CASE("stealth kill: each exclusion on its own")
{
    auto t = Guard();
    t.person = false;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::NotPerson);
    t = Guard();
    t.child = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Child);
    t = Guard();
    t.essential = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Essential);
    t = Guard();
    t.dead = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Dead);
    t = Guard();
    t.mounted = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Mounted);
    t = Guard();
    t.standing = false;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::NotStanding);
    t = Guard();
    t.sneaking = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Sneaking);
    t = Guard();
    t.running = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Running);
    t = Guard();
    t.fighting = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Fighting);
    t = Guard();
    t.detects = true;
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Detects);
}

TEST_CASE("stealth kill: San Andreas's 2.5 m range, both sides")
{
    auto t = Guard();
    t.feet = { 0.0f, M(2.45f), 0.0f };
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Yes);
    t.feet = { 0.0f, M(2.55f), 0.0f };
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::TooFar);
    // It's a 3D distance, as San Andreas measures: a guard on a ledge above is out of reach.
    t.feet = { 0.0f, M(1.0f), M(2.4f) };
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::TooFar);
    t.feet = { 0.0f, M(1.0f), M(0.5f) };
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Yes);
}

TEST_CASE("stealth kill: the victim has to face away from CJ")
{
    auto t = Guard();  // north of CJ
    t.heading = kNorth;
    CHECK(Behind(kCj, t.feet, t.heading));
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Yes);
    t.heading = kSouth;  // looking at CJ
    CHECK_FALSE(Behind(kCj, t.feet, t.heading));
    CHECK(Check(kCj, Weapons::Knife, t) == Verdict::Facing);
    // Side on is the edge: just past it either way flips the answer.
    t.heading = kEast - 0.05f;
    CHECK(Behind(kCj, t.feet, t.heading));
    t.heading = kEast + 0.05f;
    CHECK_FALSE(Behind(kCj, t.feet, t.heading));
    // From the side: CJ west of a guard facing east has his back.
    const Vec3 east{ M(1.5f), 0.0f, 0.0f };
    CHECK(Behind(kCj, east, kEast));
    CHECK_FALSE(Behind(kCj, east, kEast + kSouth));
}

TEST_CASE("stealth kill: the stab's full test")
{
    // Awareness and facing are no longer asked: he's grabbed.
    auto t = Guard();
    t.detects = true;
    t.fighting = true;
    t.heading = kSouth;
    t.feet = { 0.0f, M(3.4f), 0.0f };
    CHECK(CheckStab(kCj, Weapons::Knife, t) == Verdict::Yes);
    t.feet = { 0.0f, M(3.6f), 0.0f };
    CHECK(CheckStab(kCj, Weapons::Knife, t) == Verdict::TooFar);
    // Who can't be killed at all still can't.
    t = Guard();
    t.essential = true;
    CHECK(CheckStab(kCj, Weapons::Knife, t) == Verdict::Essential);
    t = Guard();
    t.dead = true;
    CHECK(CheckStab(kCj, Weapons::Knife, t) == Verdict::Dead);
    t = Guard();
    t.child = true;
    CHECK(CheckStab(kCj, Weapons::Knife, t) == Verdict::Child);
    CHECK(CheckStab(kCj, Weapons::Katana, Guard()) == Verdict::NotKnife);
}

TEST_CASE("stealth kill: CJ raises the knife")
{
    CHECK(Raises(true, Weapons::Knife, true, 1.0f, false));
    CHECK_FALSE(Raises(false, Weapons::Knife, true, 1.0f, false));   // Skyrim didn't flag him
    CHECK_FALSE(Raises(true, Weapons::Katana, true, 1.0f, false));   // the flag lags a weapon switch
    CHECK_FALSE(Raises(true, Weapons::Knife, false, 1.0f, false));   // the right button let go
    CHECK_FALSE(Raises(true, Weapons::Knife, true, 1.9f, false));    // too fast
    CHECK(Raises(true, Weapons::Knife, true, 1.89f, false));
    CHECK_FALSE(Raises(true, Weapons::Knife, true, 0.0f, true));     // sprinting
    // Up in an eighth of a second, and down again.
    float blend = 0.0f;
    blend = Raise(blend, true, 1.0f / 16.0f);
    CHECK(blend == doctest::Approx(0.5f));
    CHECK_FALSE(Stabs(blend, true));  // not past half yet
    blend = Raise(blend, true, 1.0f / 16.0f);
    CHECK(blend == doctest::Approx(1.0f));
    CHECK(Raise(blend, true, 1.0f) == 1.0f);
    CHECK(Stabs(blend, true));
    CHECK_FALSE(Stabs(blend, false));
    CHECK(Raise(blend, false, 1.0f) == 0.0f);
    CHECK(Raise(0.3f, true, -1.0f) == doctest::Approx(0.3f));
}

TEST_CASE("stealth kill: CJ is held a metre behind his victim")
{
    // 2 m off: slides in at 2.5 m/s, 5 cm a 1/50 s step, toward it.
    auto step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.02f);
    CHECK(step.x == doctest::Approx(0.0f));
    CHECK(step.y == doctest::Approx(0.05f));
    CHECK(step.z == 0.0f);
    // Nearly there: only what's left.
    step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 1.03f, 0.0f, 0.0f }, 0.02f);
    CHECK(step.x == doctest::Approx(0.03f));
    // Too close: backs off.
    step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f }, 0.02f);
    CHECK(step.y == doctest::Approx(-0.05f));
    // Within the slack, on top of it, or no time: stays.
    step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.01f, 0.0f }, 0.02f);
    CHECK(step.y == 0.0f);
    step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 0.02f);
    CHECK(step.x == 0.0f);
    CHECK(step.y == 0.0f);
    step = HoldStep({ 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, 0.0f);
    CHECK(step.y == 0.0f);
}

TEST_CASE("stealth kill: the stab lands as San Andreas's victim would start dying")
{
    CHECK(StabSeconds(2.4f, 1.3f) == doctest::Approx(1.3f));
    CHECK(StabSeconds(1.0f, 1.3f) == doctest::Approx(1.0f));  // never after CJ's own anim
    CHECK(StabSeconds(2.0f, 0.0f) == doctest::Approx(2.0f * kStabShare));
    CHECK(StabSeconds(0.0f, 1.3f) == doctest::Approx(1.3f));
    CHECK(StabSeconds(0.0f, -1.0f) == doctest::Approx(kStabDefault));
    CHECK(KillSeconds(2.4f) == doctest::Approx(2.4f));
    CHECK(KillSeconds(30.0f) == doctest::Approx(kKillMax));
    CHECK(KillSeconds(0.0f) > kStabDefault);
}
