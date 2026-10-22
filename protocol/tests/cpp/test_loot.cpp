#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../../cpp/loot.h"

using namespace AhShitRim::Loot;

TEST_CASE("looting takes the gold and the quest items, nothing else")
{
    CHECK(Takes(kGold, false));
    CHECK(Takes(0x00039647, true));       // the Golden Claw, a quest object in Arvel's inventory
    CHECK_FALSE(Takes(0x00012EB7, false));  // an iron sword
    CHECK_FALSE(Takes(0x0001397D, false));  // iron arrows

    const std::vector<Stack> arvel = {
        { 0x00012EB7, 1, false, "Iron Sword" },
        { kGold, 25, false, "Gold" },
        { 0x00039647, 1, true, "Golden Claw" },
        { 0x0001397D, 12, false, "Iron Arrow" },
        { kGold, 0, false, "Gold" },  // nothing left of it
    };
    const auto haul = Pick(arvel);
    CHECK(haul.gold == 25);
    REQUIRE(haul.taken.size() == 2);
    CHECK(haul.taken[0] == 1);
    CHECK(haul.taken[1] == 2);
    REQUIRE(haul.questItems.size() == 1);
    CHECK(haul.questItems[0] == "Golden Claw");

    const auto none = Pick({ { 0x00012EB7, 1, false, "Iron Sword" } });
    CHECK(none.gold == 0);
    CHECK(none.taken.empty());
}

TEST_CASE("the help box names the quest items taken")
{
    CHECK(QuestItemsText({}).empty());
    CHECK(QuestItemsText({ "Golden Claw" }) == "Golden Claw taken.");
    CHECK(QuestItemsText({ "Golden Claw", "Arvel's Journal" }) == "Golden Claw and Arvel's Journal taken.");
    CHECK(QuestItemsText({ "A", "B", "C" }) == "A, B and C taken.");
    CHECK(Pick({ { 1, 1, true, "" } }).questItems[0] == "a quest item");
}

TEST_CASE("CJ walks over a body under his feet, not one on a ledge")
{
    CHECK(WalksOver(0, 0, 0, 30, 30, 10));
    CHECK(WalksOver(0, 0, 0, 0, 0, -90));
    CHECK_FALSE(WalksOver(0, 0, 0, 60, 60, 0));     // 85 units off
    CHECK_FALSE(WalksOver(0, 0, 0, 0, 0, 150));     // above him
    CHECK_FALSE(WalksOver(0, 0, 0, 0, 0, -150));    // below
}

TEST_CASE("San Andreas's money counts up to what CJ has")
{
    Counter counter;
    CHECK(counter.Tick(500, 0.016f, true) == 500);  // the first value as is

    // 25 gold looted: one a frame, 30 frames a second.
    float t = 0.0f;
    while (counter.Shown() != 525 && t < 5.0f) {
        const auto before = counter.Shown();
        counter.Tick(525, 0.01f, true);
        CHECK(counter.Shown() - before <= 1);
        t += 0.01f;
    }
    CHECK(counter.Shown() == 525);
    CHECK(t == doctest::Approx(25.0f / 30.0f).epsilon(0.05));

    // A purchase counts down, faster the further it has to go, never past.
    counter.Tick(525 - 1500, 1.0f / 30.0f, true);
    CHECK(counter.Shown() == 525 - 123);
    for (int i = 0; i < 200; i++) {
        counter.Tick(525 - 1500, 1.0f / 30.0f, true);
        CHECK(counter.Shown() >= 525 - 1500);
    }
    CHECK(counter.Shown() == 525 - 1500);

    // A big jump goes by 12345s, then smaller steps.
    counter.Tick(1000000, 1.0f / 30.0f, true);
    CHECK(counter.Shown() == 525 - 1500 + 12345);

    // While it isn't shown it takes the money as is.
    CHECK(counter.Tick(42, 0.016f, false) == 42);
    CHECK(counter.Tick(42, 0.016f, true) == 42);

    // A long frame doesn't count more than a second's worth.
    counter.Tick(1042, 100.0f, true);
    CHECK(counter.Shown() < 1042);
}
