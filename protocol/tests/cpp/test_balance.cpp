#include <doctest/doctest.h>

#include "../../cpp/balance.h"
#include "../../cpp/health.h"
#include "../../cpp/wanted.h"

using namespace AhShitRim;
using namespace AhShitRim::Balance;

namespace
{
    const Target& Named(std::string_view a_name)
    {
        for (const auto& target : kTargets) {
            if (target.name == a_name) {
                return target;
            }
        }
        FAIL("no such target");
        return kTargets[0];
    }

    int At(int a_difficulty, std::uint8_t a_type, std::string_view a_target) { return ShotsToKill(Settings{}, a_type, Named(a_target), a_difficulty); }
}

TEST_CASE("the defaults follow the difficulty and leave Skyrim's hits on CJ as they are")
{
    const Settings s = Parse("", "", "");
    CHECK(s.cjDamage == doctest::Approx(1.0f));
    CHECK(s.damageTaken == doctest::Approx(1.0f));
    CHECK(s.followDifficulty);
}

TEST_CASE("ini factors and switches are read, and nonsense falls back")
{
    CHECK(ParseFactor("0.5", 1.0f) == doctest::Approx(0.5f));
    CHECK(ParseFactor("2", 1.0f) == doctest::Approx(2.0f));
    CHECK(ParseFactor("0", 1.0f) == doctest::Approx(0.0f));
    CHECK(ParseFactor("-1", 1.0f) == doctest::Approx(1.0f));
    CHECK(ParseFactor("half", 1.0f) == doctest::Approx(1.0f));
    CHECK(ParseFactor("0.5x", 1.0f) == doctest::Approx(1.0f));
    CHECK(ParseFactor("nan", 1.0f) == doctest::Approx(1.0f));
    CHECK(ParseFactor("1000", 1.0f) == doctest::Approx(kMaxFactor));

    CHECK(ParseSwitch("0", true) == false);
    CHECK(ParseSwitch("Off", true) == false);
    CHECK(ParseSwitch("TRUE", false) == true);
    CHECK(ParseSwitch("maybe", true) == true);
    CHECK(ParseSwitch("", false) == false);

    const Settings s = Parse("0.5", "1.5", "0");
    CHECK(s.cjDamage == doctest::Approx(0.5f));
    CHECK(s.damageTaken == doctest::Approx(1.5f));
    CHECK_FALSE(s.followDifficulty);
}

TEST_CASE("CJ deals what the player would a difficulty harder, never under 0.25; melee as it is")
{
    const Settings s;
    CHECK(Dealt(s, Weapons::Pistol, 0) == doctest::Approx(1.5f));
    CHECK(Dealt(s, Weapons::Pistol, 1) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::Pistol, kAdept) == doctest::Approx(0.75f));
    CHECK(Dealt(s, Weapons::Pistol, 3) == doctest::Approx(0.5f));
    CHECK(Dealt(s, Weapons::Pistol, kMaster) == doctest::Approx(0.25f));
    CHECK(Dealt(s, Weapons::Pistol, 5) == doctest::Approx(0.25f));  // the floor
    CHECK(Dealt(s, Weapons::Pistol, 9) == doctest::Approx(0.75f));  // out of range is Adept
    CHECK(Dealt(s, Weapons::RocketLauncher, kMaster) == doctest::Approx(0.25f));
    CHECK(Dealt(s, Weapons::Unarmed, kMaster) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::BaseballBat, 5) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::Chainsaw, kMaster) == doctest::Approx(1.0f));

    const Settings fixed = Parse("0.5", "", "off");
    CHECK(Dealt(fixed, Weapons::M4, kMaster) == doctest::Approx(0.5f));
    CHECK(Dealt(fixed, Weapons::M4, 0) == doctest::Approx(0.5f));
    CHECK(Dealt(fixed, Weapons::Unarmed, 0) == doctest::Approx(1.0f));
}

TEST_CASE("the example fights: body shots to kill at Adept and Master (docs/DESIGN.md)")
{
    using namespace Weapons;
    // The log's bandit took 5 Colt 45 body shots at Master before.
    CHECK(Damage::HitsToKill(90.0f, Damage::ToSkyrim(0.0f, Pistol, Damage::Part::Torso, 6, Damage::Class::Normal)) == 5);

    CHECK(At(kAdept, Pistol, "bandit") == 6);
    CHECK(At(kAdept, Shotgun, "bandit") == 2);
    CHECK(At(kAdept, M4, "bandit") == 5);
    CHECK(At(kMaster, Pistol, "bandit") == 18);
    CHECK(At(kMaster, Shotgun, "bandit") == 6);
    CHECK(At(kMaster, M4, "bandit") == 15);

    for (const auto* name : { "restless draugr", "large frostbite spider" }) {
        CHECK(At(kAdept, Pistol, name) == 10);
        CHECK(At(kAdept, Shotgun, name) == 4);
        CHECK(At(kAdept, M4, name) == 9);
        CHECK(At(kMaster, Pistol, name) == 30);
        CHECK(At(kMaster, Shotgun, name) == 10);
        CHECK(At(kMaster, M4, name) == 25);
    }

    CHECK(At(kAdept, Pistol, "giant frostbite spider") == 32);
    CHECK(At(kAdept, Shotgun, "giant frostbite spider") == 10);
    CHECK(At(kAdept, M4, "giant frostbite spider") == 27);
    CHECK(At(kMaster, Pistol, "giant frostbite spider") == 95);
    CHECK(At(kMaster, Shotgun, "giant frostbite spider") == 30);
    CHECK(At(kMaster, M4, "giant frostbite spider") == 79);

    CHECK(At(kAdept, Pistol, "giant") == 141);
    CHECK(At(kAdept, Shotgun, "giant") == 44);
    CHECK(At(kAdept, M4, "giant") == 117);
    CHECK(At(kMaster, Pistol, "giant") == 421);
    CHECK(At(kMaster, Shotgun, "giant") == 132);
    CHECK(At(kMaster, M4, "giant") == 351);
}

TEST_CASE("the example fights at Master: hits that kill unarmoured CJ (docs/DESIGN.md)")
{
    // Skyrim's damage at Adept, about: a bandit's iron weapon 12 a blow, a hunting bow's iron arrow 15. CJ's health
    // 100 at level 1, 150 at 10 and 200 at 20 (half the level-ups into health).
    const Settings s;
    const float    master = kToPlayer[kMaster];
    CHECK(HitsToDie(s, 12.0f, master, 100.0f) == 5);
    CHECK(HitsToDie(s, 12.0f, master, 150.0f) == 7);
    CHECK(HitsToDie(s, 12.0f, master, 200.0f) == 9);
    CHECK(HitsToDie(s, 15.0f, master, 100.0f) == 4);
    CHECK(HitsToDie(s, 15.0f, master, 150.0f) == 5);
    CHECK(HitsToDie(s, 15.0f, master, 200.0f) == 7);
    CHECK(HitsToDie(Parse("", "2", ""), 15.0f, master, 100.0f) == 2);
}

TEST_CASE("the wanted share is a layer on top, for guards and ours only")
{
    const Settings s;
    CHECK(Taken(s, 1.0f) == doctest::Approx(1.0f));  // bandits, creatures: no stars in it
    CHECK(Taken(s, Wanted::GuardDamage(1)) == doctest::Approx(0.4f));  // the tiers as protocol/cpp/wanted.h has them
    CHECK(Taken(s, Wanted::GuardDamage(3)) == doctest::Approx(0.7f));
    CHECK(Taken(s, Wanted::GuardDamage(5)) == doctest::Approx(1.0f));
    CHECK(Taken(Parse("", "2", ""), Wanted::GuardDamage(1)) == doctest::Approx(0.8f));
    CHECK(Taken(s, NAN) == doctest::Approx(1.0f));
}
