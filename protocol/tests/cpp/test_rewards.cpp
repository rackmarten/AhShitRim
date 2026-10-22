#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../../cpp/rewards.h"
#include "../../cpp/shop.h"

using namespace AhShitRim;
using namespace AhShitRim::Rewards;

namespace
{
    // The gun's Ammu-Nation price, how heavy it is.
    std::uint32_t PriceOf(std::uint8_t a_type)
    {
        for (const auto& category : Shop::kCategories) {
            for (std::size_t i = 0; i < category.count; i++) {
                if (category.items[i].type == a_type) {
                    return category.items[i].price;
                }
            }
        }
        return 0;
    }
}

TEST_CASE("every main quest step up to the Greybeards gives a gun, heavier as the story goes")
{
    const std::vector<std::string> steps = { "Unbound", "Before the Storm", "Bleak Falls Barrow", "Dragon Rising", "The Way of the Voice" };
    REQUIRE(std::size(kQuestSteps) == steps.size());
    std::uint32_t last = 0;
    for (std::size_t i = 0; i < steps.size(); i++) {
        const auto& step = kQuestSteps[i];
        CHECK(step.name == steps[i]);
        CHECK(Weapons::Carried(step.reward.type));
        CHECK_FALSE(Weapons::IsMelee(step.reward.type));
        CHECK(step.reward.rounds > 0);
        const auto price = PriceOf(step.reward.type);
        CHECK(price > last);
        last = price;
    }
}

TEST_CASE("a quest's completing stage finds its reward, any other stage none")
{
    const auto* farengar = ForStage(0x000D0800, 190);  // MQ103: the Dragonstone handed to Farengar
    REQUIRE(farengar);
    CHECK(farengar->reward.type == Weapons::MicroUzi);
    const auto* thane = ForStage(0x0002610C, 160);  // MQ104: Dragon Rising, Balgruuf
    REQUIRE(thane);
    CHECK(thane->reward.type == Weapons::Ak47);
    CHECK_FALSE(ForStage(0x000D0800, 180));
    CHECK_FALSE(ForStage(0x00012345, 190));
}

TEST_CASE("a boss of a dungeon in the table gives a gun, found through the locations that hold it")
{
    const std::uint32_t inner[] = { 0x00018EE9, 0x00016772 };  // Bleak Falls Barrow, in Whiterun Hold
    const auto*         bleak = ForBoss(std::begin(inner), std::end(inner));
    REQUIRE(bleak);
    CHECK(std::string(bleak->name) == "Bleak Falls Barrow");
    const std::uint32_t nested[] = { 0x00099999, 0x000B712C };  // a location of its own under Embershard Mine's
    const auto*         ember = ForBoss(std::begin(nested), std::end(nested));
    REQUIRE(ember);
    CHECK(ember->reward.type == Weapons::PistolSilenced);
    const std::uint32_t elsewhere[] = { 0x00016772 };
    CHECK_FALSE(ForBoss(std::begin(elsewhere), std::end(elsewhere)));
    for (const auto& boss : kBosses) {
        CHECK(Weapons::Carried(boss.reward.type));
        CHECK(boss.reward.rounds > 0);
    }
}

TEST_CASE("the help box says what CJ got and why")
{
    CHECK(Text(kQuestSteps[1].reward) == "Shotgun and 25 rounds from Jarl Balgruuf.");
    CHECK(Text(kQuestSteps[4].reward) == "Rocket launcher and 6 rockets from the Greybeards.");
}
