#include <ostream>  // before doctest, so it can print string_view
#include <string_view>
#include <algorithm>
#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "../../cpp/damage.h"

using namespace AhShitRim;
using namespace AhShitRim::Damage;

TEST_CASE("San Andreas's damage per bullet and blow")
{
    CHECK(SanAndreasDamage(Weapons::Pistol) == 25.0f);
    CHECK(SanAndreasDamage(Weapons::PistolSilenced) == 40.0f);
    CHECK(SanAndreasDamage(Weapons::DesertEagle, 0) == 70.0f);
    CHECK(SanAndreasDamage(Weapons::DesertEagle, 1) == 140.0f);
    CHECK(SanAndreasDamage(Weapons::Shotgun) == 10.0f);  // a pellet
    CHECK(SanAndreasDamage(Weapons::Ak47) == 30.0f);
    CHECK(SanAndreasDamage(Weapons::SniperRifle) == 125.0f);
    CHECK(SanAndreasDamage(Weapons::Minigun) == 140.0f);
    CHECK(SanAndreasDamage(Weapons::Unarmed) == 6.0f);
    CHECK(SanAndreasDamage(Weapons::Chainsaw) == 50.0f);
    CHECK(SanAndreasDamage(Weapons::Grenade) == 0.0f);  // an explosion, not a hit
    CHECK(SanAndreasDamage(Weapons::Camera) == 0.0f);
}

TEST_CASE("a shotgun's shot fans out into its pellets, always the same way")
{
    CHECK(Pellets(Weapons::Shotgun) == 8);
    CHECK(Pellets(Weapons::Spas12) == 6);
    CHECK(Pellets(Weapons::Pistol) == 1);
    const Dir forward{ 0.0f, 1.0f, 0.0f };
    const int n = Pellets(Weapons::Sawnoff);
    const float spread = SpreadDegrees(Weapons::Sawnoff);
    const auto first = PelletDirection(forward, 0, n, spread);
    CHECK(first.y == 1.0f);
    float widest = 0.0f;
    for (int i = 1; i < n; ++i) {
        const auto d = PelletDirection(forward, i, n, spread);
        CHECK(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) == doctest::Approx(1.0f));
        const float degrees = std::acos(std::clamp(d.y, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
        CHECK(degrees > 0.1f);
        CHECK(degrees <= spread + 0.01f);
        widest = (std::max)(widest, degrees);
        const auto again = PelletDirection(forward, i, n, spread);
        CHECK(again.x == d.x);
    }
    CHECK(widest == doctest::Approx(spread).epsilon(0.01));
    // Straight up works too.
    const auto up = PelletDirection({ 0.0f, 0.0f, 1.0f }, 3, n, spread);
    CHECK(up.z > 0.99f);
    // Point blank, a full shot of the pump-action kills an early bandit.
    CHECK(HitsToKill(100.0f, Pellets(Weapons::Shotgun) * ToSkyrim(10.0f, Weapons::Shotgun, Part::Torso, 3, Class::Normal)) == 2);
    CHECK(HitsToKill(60.0f, Pellets(Weapons::Shotgun) * ToSkyrim(10.0f, Weapons::Shotgun, Part::Torso, 3, Class::Normal)) == 1);
}

TEST_CASE("the hit node gives the body part")
{
    CHECK(PartOfNode("NPC Head [Head]") == Part::Head);
    CHECK(PartOfNode("NPC Head") == Part::Head);  // a dragon's
    CHECK(PartOfNode("Canine_Head") == Part::Head);
    CHECK(PartOfNode("NPC Spine2 [Spn2]") == Part::Torso);
    CHECK(PartOfNode("NPC Neck [Neck]") == Part::Torso);
    CHECK(PartOfNode("NPC Pelvis [Pelv]") == Part::Torso);
    CHECK(PartOfNode("NPC L Thigh [LThg]") == Part::Limb);
    CHECK(PartOfNode("NPC R Forearm [RLar]") == Part::Limb);
    CHECK(PartOfNode("NPC L Hand [LHnd]") == Part::Limb);
    CHECK(PartOfNode("NPC Tail1") == Part::Limb);
    CHECK(PartOfNode("") == Part::Torso);
    CHECK(PartOfNode("CharacterBumper") == Part::Torso);
}

TEST_CASE("giants and dragons by their race")
{
    CHECK(ClassOf("NordRace", false) == Class::Normal);
    CHECK(ClassOf("DraugrRace", false) == Class::Normal);
    CHECK(ClassOf("GiantRace", false) == Class::Giant);
    CHECK(ClassOf("MammothRace", false) == Class::Giant);
    CHECK(ClassOf("DragonRace", false) == Class::Dragon);
    CHECK(ClassOf("AlduinRace", false) == Class::Dragon);
    CHECK(ClassOf("UndeadDragonRace", false) == Class::Dragon);
    CHECK(ClassOf("DragonPriestRace", false) == Class::Normal);
    CHECK(ClassOf("SomeModdedWyrm", true) == Class::Dragon);
}

TEST_CASE("damage falls off above the early levels, down to a floor")
{
    CHECK(LevelMultiplier(1) == 1.0f);
    CHECK(LevelMultiplier(kLevelFull) == 1.0f);
    CHECK(LevelMultiplier(kLevelFull + 1) < 1.0f);
    CHECK(LevelMultiplier(20) > LevelMultiplier(30));
    CHECK(LevelMultiplier(30) == doctest::Approx(1.0f / (1.0f + kLevelFalloff * 24)));
    CHECK(LevelMultiplier(500) == kLevelFloor);
}

TEST_CASE("a pistol kills an early bandit or draugr in 3 to 5 body shots")
{
    // Bleak Falls Barrow's bandits and draugr: levels 1 to 6, about 60 to 100 health.
    for (std::uint16_t level = 1; level <= 6; ++level) {
        for (float health : { 60.0f, 80.0f, 100.0f }) {
            const int shots = HitsToKill(health, ToSkyrim(25.0f, Weapons::Pistol, Part::Torso, level, Class::Normal));
            CHECK(shots >= 3);
            CHECK(shots <= 5);
        }
    }
}

TEST_CASE("headshots count, a rifle's kills")
{
    const float body = ToSkyrim(25.0f, Weapons::Pistol, Part::Torso, 3, Class::Normal);
    CHECK(ToSkyrim(25.0f, Weapons::Pistol, Part::Head, 3, Class::Normal) == doctest::Approx(body * kHeadshot));
    CHECK(ToSkyrim(25.0f, Weapons::Pistol, Part::Limb, 3, Class::Normal) == doctest::Approx(body * kLimb));
    // A sniper headshot kills a bandit chief (level 20, about 250 health) in one.
    CHECK(HitsToKill(250.0f, ToSkyrim(125.0f, Weapons::SniperRifle, Part::Head, 20, Class::Normal)) == 1);
    CHECK(HitsToKill(250.0f, ToSkyrim(75.0f, Weapons::CountryRifle, Part::Head, 20, Class::Normal)) == 1);
    // But not a dragon (level 10, about 1200 health).
    CHECK(HitsToKill(1200.0f, ToSkyrim(125.0f, Weapons::SniperRifle, Part::Head, 10, Class::Dragon)) > 1);
}

TEST_CASE("the minigun is a dragon weapon, the pistol isn't")
{
    // The first dragons: level 10, about 1200 health.
    const int pistol = HitsToKill(1200.0f, ToSkyrim(25.0f, Weapons::Pistol, Part::Torso, 10, Class::Dragon));
    const int minigun = HitsToKill(1200.0f, ToSkyrim(140.0f, Weapons::Minigun, Part::Torso, 10, Class::Dragon));
    CHECK(pistol > 200);
    CHECK(minigun < 70);  // a few seconds of fire
    // A giant (level 30, about 700 health) needs the big guns too.
    CHECK(HitsToKill(700.0f, ToSkyrim(25.0f, Weapons::Pistol, Part::Torso, 30, Class::Giant)) > 50);
}

TEST_CASE("a shot without San Andreas's damage uses the table's")
{
    CHECK(ToSkyrim(0.0f, Weapons::Pistol, Part::Torso, 1, Class::Normal) == doctest::Approx(25.0f * kScale));
    CHECK(ToSkyrim(-1.0f, Weapons::Pistol, Part::Torso, 1, Class::Normal) == doctest::Approx(25.0f * kScale));
    CHECK(ToSkyrim(100.0f, Weapons::Pistol, Part::Torso, 1, Class::Normal) == doctest::Approx(100.0f * kScale));
    CHECK(HitsToKill(100.0f, 0.0f) == 0);
    CHECK(HitsToKill(100.0f, 25.0f) == 4);
    CHECK(HitsToKill(100.0f, 30.0f) == 4);
}

TEST_CASE("a blow reaches an actor's capsule")
{
    const Vec3 feet{ 100.0f, 200.0f, 0.0f };
    float      at = -1.0f;
    // At chest height, swept down to the ground they share: the blow's radius plus the actor's from its axis.
    CHECK(BlowReaches({ 100.0f + 50.0f, 200.0f, 90.0f }, 30.0f, 0.0f, feet, 128.0f, 25.0f, &at));
    CHECK(at == doctest::Approx(90.0f));
    CHECK_FALSE(BlowReaches({ 100.0f + 60.0f, 200.0f, 90.0f }, 30.0f, 0.0f, feet, 128.0f, 25.0f));
    // Above the head (CJ standing that high), off by the capsule's round top.
    CHECK(BlowReaches({ 100.0f, 200.0f, 128.0f + 20.0f }, 30.0f, 128.0f + 20.0f, feet, 128.0f, 25.0f));
    CHECK_FALSE(BlowReaches({ 100.0f, 200.0f, 128.0f + 60.0f }, 30.0f, 128.0f + 60.0f, feet, 128.0f, 25.0f));
}

TEST_CASE("a blow swept down lands on small creatures at CJ's feet")
{
    // CJ stands at z 0 and strikes at a person's chest, 90 up; a punch reaches 28, a bat 35 (melee.dat's
    // 0.4 and 0.5 m). Small races get the 20 unit capsule radius at least.
    const float punch = 0.4f * 70.0f, bat = 0.5f * 70.0f, chest = 90.0f, ground = 0.0f;
    const float skeever = 35.0f, mudcrab = 30.0f, fox = 50.0f;
    float       at = -1.0f;

    // The bug: at chest height alone the blow passes over a skeever 40 ahead.
    CHECK_FALSE(BlowReaches({ 40.0f, 0.0f, chest }, punch, chest, {}, skeever, 20.0f));
    // Swept down, the punch lands on its back, and the bat a little farther out.
    CHECK(BlowReaches({ 40.0f, 0.0f, chest }, punch, ground, {}, skeever, 20.0f, &at));
    CHECK(at == doctest::Approx(skeever));
    CHECK(BlowReaches({ 50.0f, 0.0f, chest }, bat, ground, {}, skeever, 20.0f));
    CHECK(BlowReaches({ 45.0f, 0.0f, chest }, punch, ground, {}, mudcrab, 25.0f));
    CHECK(BlowReaches({ 0.0f, 45.0f, chest }, bat, ground, {}, fox, 20.0f));

    // Nothing past the reach out from CJ: punch 28 + 20, bat 35 + 20.
    CHECK_FALSE(BlowReaches({ 50.0f, 0.0f, chest }, punch, ground, {}, skeever, 20.0f));
    CHECK_FALSE(BlowReaches({ 60.0f, 0.0f, chest }, bat, ground, {}, skeever, 20.0f));

    // Nor down past the ground CJ stands on: a skeever two metres under his ledge.
    CHECK_FALSE(BlowReaches({ 30.0f, 0.0f, 140.0f + chest }, bat, 140.0f, {}, skeever, 20.0f));
    // A little below his feet (a slope) still is.
    CHECK(BlowReaches({ 30.0f, 0.0f, 20.0f + chest }, punch, 20.0f, {}, skeever, 20.0f));
}

TEST_CASE("a blow lands on each actor once, the chainsaw keeps cutting")
{
    Blows blows;
    CHECK(blows.Lands(7, 1, 0.0, false) == 1.0f);
    CHECK(blows.Lands(7, 1, 0.03, false) == 0.0f);  // the same blow on the next frame
    CHECK(blows.Lands(8, 1, 0.03, false) == 1.0f);  // a second actor in the same swing
    CHECK(blows.Lands(7, 2, 0.5, false) == 1.0f);   // the next blow

    Blows saw;
    CHECK(saw.Lands(7, 1, 0.0, true) == doctest::Approx(kChainsawTick));
    CHECK(saw.Lands(7, 1, kChainsawTick * 0.5, true) == 0.0f);
    CHECK(saw.Lands(7, 1, 0.25, true) == doctest::Approx(0.25f));
    CHECK(saw.Lands(7, 1, 5.0, true) == doctest::Approx(kChainsawGap));  // after a pause
    CHECK(BlowDamage(50.0f, Weapons::Chainsaw, 0.25f) == doctest::Approx(kChainsawPerSecond * 0.25f));
    CHECK(BlowDamage(15.0f, Weapons::Unarmed, 1.0f) == doctest::Approx(15.0f * kMeleeScale));
}

TEST_CASE("melee kills like San Andreas's: a few punches, a couple of sword cuts, a second or two of chainsaw")
{
    // An early bandit, about 70 health; Farengar, level 15, about 150.
    CHECK(HitsToKill(70.0f, ToSkyrim(BlowDamage(9.0f, Weapons::Unarmed, 1.0f), Weapons::Unarmed, Part::Torso, 3, Class::Normal)) <= 4);
    CHECK(HitsToKill(150.0f, ToSkyrim(BlowDamage(15.0f, Weapons::Katana, 1.0f), Weapons::Katana, Part::Torso, 15, Class::Normal)) <= 7);
    const float perSecond = ToSkyrim(BlowDamage(0.0f, Weapons::Chainsaw, 1.0f), Weapons::Chainsaw, Part::Torso, 15, Class::Normal);
    CHECK(150.0f / perSecond <= 2.0f);
    CHECK(70.0f / ToSkyrim(BlowDamage(0.0f, Weapons::Chainsaw, 1.0f), Weapons::Chainsaw, Part::Torso, 3, Class::Normal) <= 1.0f);
}

TEST_CASE("the chainsaw always staggers, the rest by chance")
{
    CHECK(StaggerChance(Weapons::Chainsaw) == 1.0f);
    CHECK(StaggerChance(Weapons::BaseballBat) > StaggerChance(Weapons::Unarmed));
    CHECK(StaggerChance(Weapons::Unarmed) > 0.0f);
    CHECK(StaggerDirection(0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(StaggerDirection(0.0f, 3.14159265f) == doctest::Approx(0.5f));
    CHECK(StaggerDirection(1.0f, 0.5f) == doctest::Approx((6.28318531f - 0.5f) / 6.28318531f));
}

TEST_CASE("a line through the head is a headshot, a low hit a leg")
{
    const Vec3 head{ 0.0f, 500.0f, 120.0f };
    CHECK(PassesWithin({ 0.0f, 0.0f, 120.0f }, { 0.0f, 1.0f, 0.0f }, head, kHeadRadius));
    CHECK(PassesWithin({ 0.0f, 0.0f, 128.0f }, { 0.0f, 1.0f, 0.0f }, head, kHeadRadius));
    CHECK_FALSE(PassesWithin({ 0.0f, 0.0f, 135.0f }, { 0.0f, 1.0f, 0.0f }, head, kHeadRadius));  // over the head
    CHECK_FALSE(PassesWithin({ 0.0f, 600.0f, 120.0f }, { 0.0f, 1.0f, 0.0f }, head, kHeadRadius));  // behind the shooter
    CHECK(HeadRadius(Class::Normal, 1.0f) == kHeadRadius);
    CHECK(HeadRadius(Class::Dragon, 1.0f) > HeadRadius(Class::Giant, 1.0f));
    CHECK(PartOfHit(true, 0.9f) == Part::Head);
    CHECK(PartOfHit(false, 0.2f) == Part::Limb);
    CHECK(PartOfHit(false, 0.7f) == Part::Torso);
}

TEST_CASE("hits are counted per weapon until they are sent")
{
    SkillHits hits;
    hits.Add(Weapons::Pistol);
    hits.Add(Weapons::Pistol);
    hits.Add(Weapons::Ak47);
    std::vector<std::pair<int, std::uint32_t>> sent;
    hits.Flush([&](std::uint8_t a_type, std::uint32_t a_count) {
        if (a_type == Weapons::Ak47) {
            return false;  // the ring was full: kept for the next time
        }
        sent.emplace_back(a_type, a_count);
        return true;
    });
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].first == Weapons::Pistol);
    CHECK(sent[0].second == 2);
    sent.clear();
    hits.Flush([&](std::uint8_t a_type, std::uint32_t a_count) {
        sent.emplace_back(a_type, a_count);
        return true;
    });
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].first == Weapons::Ak47);
}
