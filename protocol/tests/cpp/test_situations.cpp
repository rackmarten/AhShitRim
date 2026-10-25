#include <doctest/doctest.h>

#include "../../cpp/situations.h"

#include <ostream>
#include <set>
#include <string>

using namespace AhShitRim;
using namespace AhShitRim::Situations;

namespace
{
    constexpr Trigger kAll[] = { Trigger::Fight, Trigger::Shoot, Trigger::PullGun, Trigger::StealthKill, Trigger::Singing, Trigger::Cash,
        Trigger::ShopBuy, Trigger::ShopLeave, Trigger::Chased, Trigger::Crash };
}

TEST_CASE("every trigger has a picker column of its own, a speech context and a chance")
{
    std::set<std::string_view> keys;
    for (const auto& info : kTriggers) {
        CHECK(keys.insert(info.key).second);
        CHECK_FALSE(info.name.empty());
        CHECK(info.context > 0);
        CHECK(info.context < 360);  // eGlobalSpeechContext, below the pain contexts' end
        CHECK(info.chance > 0.0f);
        CHECK(info.chance <= 1.0f);
        CHECK(info.repeat >= 0.0f);
    }
    CHECK(keys.size() == kTriggerCount);
    CHECK(InfoOf(Trigger::Fight).chance == doctest::Approx(1.0f / 3.0f));  // about one blow in three
    CHECK(InfoOf(Trigger::Fight).context == 89);    // CTX_GLOBAL_FIGHT
    CHECK(InfoOf(Trigger::Shoot).context == 182);   // CTX_GLOBAL_SHOOT
    CHECK(InfoOf(Trigger::Singing).context == 336);  // CTX_GLOBAL_SINGING
    CHECK(InfoOf(Trigger::Cash).context == 172);    // CTX_GLOBAL_PICKUP_CASH
    // No global context reaches CJ's stealth kill and shop lines: the insult's carries them.
    CHECK(InfoOf(Trigger::StealthKill).context == 110);
    CHECK(InfoOf(Trigger::ShopBuy).context == 110);
    CHECK(InfoOf(Trigger::ShopLeave).context == 110);
}

TEST_CASE("the per-trigger tables: CJ's lines, and lines wherever San Andreas has no pick of his")
{
    for (const auto trigger : kAll) {
        for (const auto& line : LinesOf(trigger)) {
            CHECK(VoiceLines::Valid(line));
        }
        // San Andreas has no CJ line for these contexts: an empty table would leave him silent.
        if (!InfoOf(trigger).defaults.empty()) {
            CHECK_FALSE(LinesOf(trigger).empty());
        }
    }
    CHECK(InfoOf(Trigger::StealthKill).defaults == "STEALTH_KILL");
    CHECK(InfoOf(Trigger::ShopBuy).defaults == "SHOP_BUY");
    CHECK(InfoOf(Trigger::ShopLeave).defaults == "SHOP_LEAVE");
    CHECK(InfoOf(Trigger::Fight).defaults.empty());
}

TEST_CASE("a trigger waits 4 s after any line, and never talks over one")
{
    Chatter chatter(0.0f);
    CHECK_FALSE(chatter.Speak(Trigger::StealthKill, 3.9f, 0.0f));  // the start counts as a line
    CHECK(chatter.Speak(Trigger::StealthKill, 4.0f, 0.0f));
    CHECK_FALSE(chatter.Speak(Trigger::Cash, 7.0f, 0.0f));  // the stealth kill line just now
    CHECK(chatter.Speak(Trigger::Cash, 8.0f, 0.0f));
    chatter.Heard(10.0f);  // a swear key
    CHECK_FALSE(chatter.Speak(Trigger::ShopBuy, 13.0f, 0.0f));
    CHECK_FALSE(chatter.Speak(Trigger::ShopBuy, 20.0f, 0.0f, true));  // a line of his still plays
    CHECK(chatter.Speak(Trigger::ShopBuy, 20.0f, 0.0f));
    CHECK(chatter.Quiet(25.0f) == doctest::Approx(5.0f));
}

TEST_CASE("each trigger rolls its own chance")
{
    Chatter chatter(-100.0f);
    const float third = 1.0f / 3.0f;
    CHECK_FALSE(chatter.Speak(Trigger::Fight, 0.0f, third));  // the roll falls outside one in three
    CHECK(chatter.Speak(Trigger::Fight, 0.0f, third - 0.01f));
    CHECK_FALSE(chatter.Speak(Trigger::Shoot, 10.0f, 0.5f));
    CHECK(chatter.Speak(Trigger::Shoot, 10.0f, 0.49f));
    CHECK(chatter.Speak(Trigger::ShopLeave, 20.0f, 0.99f));  // leaving a shop always speaks

    // About one blow in three, over many blows 4 s apart.
    Chatter fights(-100.0f);
    int     said = 0;
    for (int i = 0; i < 300; ++i) {
        said += fights.Speak(Trigger::Fight, i * 5.0f, (i % 30) / 30.0f) ? 1 : 0;
    }
    CHECK(said == 100);
}

TEST_CASE("pulling a gun and being chased wait 20 s before speaking again")
{
    Chatter chatter(-100.0f);
    CHECK(chatter.Speak(Trigger::PullGun, 0.0f, 0.0f));
    CHECK_FALSE(chatter.Speak(Trigger::PullGun, 19.0f, 0.0f));
    CHECK(chatter.Speak(Trigger::Chased, 19.0f, 0.0f));  // its own wait
    CHECK(chatter.Speak(Trigger::PullGun, 24.0f, 0.0f));
    CHECK_FALSE(chatter.Speak(Trigger::Chased, 38.0f, 0.0f));
    CHECK(chatter.Speak(Trigger::Chased, 39.0f, 0.0f));
}

TEST_CASE("a burst's first hit is the one after a pause of more than 1.5 s")
{
    Burst burst;
    CHECK(burst.Hit(0.0f));
    CHECK_FALSE(burst.Hit(0.2f));
    CHECK_FALSE(burst.Hit(1.6f));  // still the same burst: 1.4 s since the last hit
    CHECK(burst.Hit(3.2f));
}

TEST_CASE("edges and Skyrim's counters fire once per change, the first value only seeds")
{
    Edge edge;
    CHECK_FALSE(edge.Update(false));
    CHECK(edge.Update(true));
    CHECK_FALSE(edge.Update(true));
    CHECK_FALSE(edge.Update(false));
    CHECK(edge.Update(true));

    Counter counter;
    CHECK_FALSE(counter.Update(7));  // a San Andreas that started after Skyrim
    CHECK_FALSE(counter.Update(7));
    CHECK(counter.Update(8));
    CHECK(counter.Update(0));  // wrapped
}

TEST_CASE("a crash is health lost at once, not a scratch, a repair or another vehicle")
{
    Crash crash;
    int   car = 0, bike = 0;
    CHECK_FALSE(crash.Update(&car, 1000.0f, 0.0f));
    CHECK_FALSE(crash.Update(&car, 997.0f, 0.1f));  // a scratch
    CHECK(crash.Update(&car, 960.0f, 0.2f));
    CHECK_FALSE(crash.Update(&car, 900.0f, 0.5f));  // the same crash, still settling
    CHECK(crash.Update(&car, 850.0f, 1.5f));
    CHECK_FALSE(crash.Update(&car, 1000.0f, 3.0f));  // repaired
    CHECK_FALSE(crash.Update(&bike, 400.0f, 4.0f));  // another vehicle starts over
    CHECK_FALSE(crash.Update(nullptr, 0.0f, 5.0f));  // on foot
}

TEST_CASE("singing starts after 90 s settled and quiet, and stops when he moves off or the radio comes on")
{
    using Action = Singing::Action;
    Singing singing;
    CHECK(singing.Update(true, 0.0f, 0.0f, false) == Action::None);
    CHECK(singing.Update(true, 89.0f, 89.0f, false) == Action::None);
    CHECK(singing.Update(true, 90.0f, 90.0f, false) == Action::Start);
    CHECK(singing.Update(true, 91.0f, 0.5f, true) == Action::None);  // he sings
    CHECK(singing.Update(false, 92.0f, 1.5f, true) == Action::Stop);  // the radio came on
    CHECK(singing.Update(false, 93.0f, 0.1f, false) == Action::None);

    // Settled again, but something was said meanwhile: 90 s from both.
    CHECK(singing.Update(true, 100.0f, 200.0f, false) == Action::None);
    CHECK(singing.Update(true, 189.0f, 50.0f, false) == Action::None);
    CHECK(singing.Update(true, 190.0f, 89.0f, false) == Action::None);
    CHECK(singing.Update(true, 191.0f, 90.0f, false) == Action::Start);

    // The song didn't come (he was speaking): no Start every frame after, but again 90 s on.
    CHECK(singing.Update(true, 191.1f, 90.1f, false) == Action::None);
    CHECK(singing.Update(true, 280.0f, 179.0f, false) == Action::None);
    CHECK(singing.Update(true, 281.0f, 180.0f, false) == Action::Start);
}

TEST_CASE("the purse: gold gained outside a shop is loot, spent in one is a buy, closing after a buy is leaving")
{
    Purse purse;
    auto  e = purse.Update(100, false);
    CHECK_FALSE(e.cash);  // the first read only seeds
    e = purse.Update(150, false);
    CHECK(e.cash);
    CHECK_FALSE(e.buy);
    e = purse.Update(150, true);  // Belethor's barter
    CHECK_FALSE(e.cash);
    e = purse.Update(170, true);  // selling isn't loot
    CHECK_FALSE(e.cash);
    CHECK_FALSE(e.buy);
    e = purse.Update(120, true);
    CHECK(e.buy);
    e = purse.Update(90, true);
    CHECK(e.buy);
    e = purse.Update(90, false);
    CHECK(e.leave);
    CHECK_FALSE(e.cash);

    // Closing a shop without buying says nothing; spending outside one isn't a buy.
    purse.Update(90, true);
    CHECK_FALSE(purse.Update(90, false).leave);
    CHECK_FALSE(purse.Update(40, false).buy);

    // A load: the next read only seeds again.
    purse.Seed();
    CHECK_FALSE(purse.Update(5000, false).cash);
}
