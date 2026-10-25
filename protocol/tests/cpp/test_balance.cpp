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

    int AtMaster(std::uint8_t a_type, std::string_view a_target) { return ShotsToKill(Settings{}, a_type, Named(a_target), kByPlayer[kMaster]); }
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

TEST_CASE("a multiplier the game doesn't give is Skyrim.esm's for the difficulty")
{
    CHECK(OrDefault(0.0f, kByPlayer, kMaster) == doctest::Approx(0.5f));
    CHECK(OrDefault(NAN, kToPlayer, 5) == doctest::Approx(3.0f));
    CHECK(OrDefault(0.6f, kByPlayer, kMaster) == doctest::Approx(0.6f));  // a mod's own value wins
    CHECK(OrDefault(0.0f, kByPlayer, 9) == doctest::Approx(1.0f));        // out of range is Adept
    CHECK((kSuffixes[kMaster] == "VH"));
}

TEST_CASE("CJ's guns follow the difficulty: half at Master, as before at Adept; melee as it is")
{
    const Settings s;
    CHECK(Dealt(s, Weapons::Pistol, kByPlayer[kAdept]) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::Pistol, kByPlayer[kMaster]) == doctest::Approx(0.5f));
    CHECK(Dealt(s, Weapons::RocketLauncher, kByPlayer[kMaster]) == doctest::Approx(0.5f));
    CHECK(Dealt(s, Weapons::Pistol, kByPlayer[0]) == doctest::Approx(2.0f));
    CHECK(Dealt(s, Weapons::Unarmed, kByPlayer[kMaster]) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::BaseballBat, kByPlayer[5]) == doctest::Approx(1.0f));
    CHECK(Dealt(s, Weapons::Chainsaw, kByPlayer[kMaster]) == doctest::Approx(1.0f));

    const Settings fixed = Parse("0.5", "", "off");
    CHECK(Dealt(fixed, Weapons::M4, kByPlayer[kMaster]) == doctest::Approx(0.5f));
    CHECK(Dealt(fixed, Weapons::M4, kByPlayer[0]) == doctest::Approx(0.5f));
    CHECK(Dealt(fixed, Weapons::Unarmed, kByPlayer[0]) == doctest::Approx(1.0f));
}

TEST_CASE("the example fights at Master: body shots to kill (docs/DESIGN.md)")
{
    using namespace Weapons;
    // The log's bandit took 5 Colt 45 body shots before; twice that now.
    CHECK(ShotsToKill(Settings{}, Pistol, Named("bandit"), kByPlayer[kAdept]) == 5);

    CHECK(AtMaster(Pistol, "bandit") == 9);
    CHECK(AtMaster(Shotgun, "bandit") == 3);
    CHECK(AtMaster(M4, "bandit") == 8);

    CHECK(AtMaster(Pistol, "restless draugr") == 15);
    CHECK(AtMaster(Shotgun, "restless draugr") == 5);
    CHECK(AtMaster(M4, "restless draugr") == 13);

    CHECK(AtMaster(Pistol, "large frostbite spider") == 15);
    CHECK(AtMaster(Shotgun, "large frostbite spider") == 5);
    CHECK(AtMaster(M4, "large frostbite spider") == 13);

    CHECK(AtMaster(Pistol, "giant frostbite spider") == 48);
    CHECK(AtMaster(Shotgun, "giant frostbite spider") == 15);
    CHECK(AtMaster(M4, "giant frostbite spider") == 40);

    CHECK(AtMaster(Pistol, "giant") == 211);
    CHECK(AtMaster(Shotgun, "giant") == 66);
    CHECK(AtMaster(M4, "giant") == 176);
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
