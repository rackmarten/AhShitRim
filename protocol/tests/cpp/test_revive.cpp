#include <doctest/doctest.h>

#include "../../cpp/revive.h"

#include <limits>
#include <ostream>
#include <string>

using namespace AhShitRim::Revive;

namespace
{
    Kill Cj(std::uint32_t a_id, int a_aggression = 0, Reaction a_reaction = Reaction::Neutral)
    {
        Kill kill;
        kill.cjHasPlayer = true;
        kill.byPlayer = true;
        kill.id = a_id;
        kill.aggression = a_aggression;
        kill.reaction = a_reaction;
        return kill;
    }

    // A camera at the origin looking along +y, up +z, right +x, 90 degrees across and about 62 high.
    constexpr View kView{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, 1.0f, 0.6f };
}

TEST_CASE("revive: hostile on sight")
{
    CHECK_FALSE(HostileOnSight(0, Reaction::Neutral));  // a chicken, a cow
    CHECK_FALSE(HostileOnSight(0, Reaction::Enemy));    // unaggressive never starts it
    CHECK_FALSE(HostileOnSight(1, Reaction::Neutral));  // a guard: aggressive, but not the player's enemy
    CHECK_FALSE(HostileOnSight(1, Reaction::Friend));
    CHECK(HostileOnSight(1, Reaction::Enemy));          // a bandit, a draugr: their faction hates the player's
    CHECK(HostileOnSight(2, Reaction::Neutral));        // very aggressive: a wolf, a bear
    CHECK(HostileOnSight(3, Reaction::Ally));           // frenzied attacks anyone
}

TEST_CASE("revive: whose death counts")
{
    CHECK(Judge(Cj(0x00013BBF)) == Verdict::Back);                       // CJ killed Adrianne
    CHECK(Judge(Cj(0x000A2C94, 1, Reaction::Friend)) == Verdict::Back);  // a guard, generic
    CHECK(Judge(Cj(0x0001C188)) == Verdict::Back);                       // a chicken

    auto skyrims = Cj(0x00013BBF);
    skyrims.cjHasPlayer = false;
    CHECK(Judge(skyrims) == Verdict::NotCj);  // Skyrim's own player did
    auto dragons = Cj(0x00013BBF);
    dragons.byPlayer = false;
    CHECK(Judge(dragons) == Verdict::NotCj);  // a guard or a dragon did

    CHECK(Judge(Cj(kPlayerRef)) == Verdict::NotPlaced);
    CHECK(Judge(Cj(0xFF000ABC)) == Verdict::NotPlaced);  // made at runtime: no editor spot
    CHECK(Judge(Cj(0xFF000ABC, 1, Reaction::Enemy)) == Verdict::NotPlaced);

    CHECK(Judge(Cj(0x0001A6A1, 1, Reaction::Enemy)) == Verdict::Hostile);  // a bandit at a camp
    CHECK(Judge(Cj(0x0001A6A2, 2)) == Verdict::Hostile);                   // a wolf

    auto bounty = Cj(0x0010E3A1, 1, Reaction::Neutral);
    bounty.killTarget = true;
    CHECK(Judge(bounty) == Verdict::KillTarget);  // a bounty's giant
    auto hostileTarget = Cj(0x0010E3A2, 1, Reaction::Enemy);
    hostileTarget.killTarget = true;
    CHECK(Judge(hostileTarget) == Verdict::Hostile);  // a bandit leader: hostile already

    auto essential = Cj(0x00013BBF, 2);
    essential.spared = true;
    essential.killTarget = true;
    CHECK(Judge(essential) == Verdict::Back);  // NOMERCY's essentials come back, whatever they are
    CHECK(std::string(Why(Verdict::Hostile)).find("hostile") != std::string::npos);
}

TEST_CASE("revive: a quest's kill target")
{
    const std::vector<Objective> objectives{
        { 0x000A0001, kObjectiveDisplayed, { 3 } },  // the bounty: kill the giant (alias 3)
        { 0x000A0001, 2, { 5 } },                    // completed: the one before
        { 0x000A0002, 0, { 1 } },                    // dormant: not asked yet
        { 0x000A0003, kObjectiveDisplayed, { 7, 8 } },
    };
    CHECK(KillTarget({ { 0x000A0001, 3, true } }, objectives) == 0x000A0001);
    CHECK(KillTarget({ { 0x000A0001, 3, false } }, objectives) == 0);  // the quest isn't running
    CHECK(KillTarget({ { 0x000A0001, 5, true } }, objectives) == 0);   // its objective is done
    CHECK(KillTarget({ { 0x000A0002, 1, true } }, objectives) == 0);   // not shown yet
    CHECK(KillTarget({ { 0x000A0003, 3, true } }, objectives) == 0);   // another quest's alias 3
    CHECK(KillTarget({ { 0x000A0004, 9, true }, { 0x000A0003, 8, true } }, objectives) == 0x000A0003);
    CHECK(KillTarget({}, objectives) == 0);
}

TEST_CASE("revive: a death that moves a quest on")
{
    const QuestMark bounty{ 0x000A0001, 10, { 1, 0 } };
    const QuestMark home{ 0x000B0001, 0, {} };

    QuestWatch watch;
    watch.Start(0x00013BBF, 100.0f, { bounty, home });
    watch.Start(0x0001C188, 100.5f, {});  // held by no quest: nothing to watch
    CHECK(watch.Watching(0x00013BBF));
    CHECK_FALSE(watch.Watching(0x0001C188));

    CHECK(watch.Stage(0x000C0001, 101.0f).empty());  // a quest that doesn't hold them
    CHECK(watch.Due(101.9f).empty());
    const auto moved = watch.Stage(0x000A0001, 100.0f + kQuestWindow);  // just in time
    REQUIRE(moved.size() == 1);
    CHECK(moved[0] == 0x00013BBF);
    CHECK(watch.Empty());

    watch.Start(0x00013BBF, 200.0f, { bounty });
    CHECK(watch.Stage(0x000A0001, 200.0f + kQuestWindow + 0.1f).empty());  // too late: not this death's doing
    CHECK(watch.Watching(0x00013BBF));
    CHECK(watch.Due(200.0f + kQuestWindow).empty());
    const auto due = watch.Due(200.0f + kQuestWindow + 0.1f);
    REQUIRE(due.size() == 1);
    CHECK(due[0].id == 0x00013BBF);
    CHECK(watch.Empty());

    // Looked at again when the window closes: another stage or an objective completed is a move.
    CHECK(Moved({ bounty, home }, { bounty, home }) == 0);
    auto done = bounty;
    done.objectives[0] = 3;  // completed, still shown
    CHECK(Moved({ bounty, home }, { done, home }) == 0x000A0001);
    auto staged = home;
    staged.stage = 20;
    CHECK(Moved({ bounty, home }, { bounty, staged }) == 0x000B0001);
    auto shown = bounty;
    shown.objectives[1] = 1;  // a new objective shown, or one failed: not a completion
    CHECK(Moved({ bounty }, { shown }) == 0);
    shown.objectives[1] = 4;
    CHECK(Moved({ bounty }, { shown }) == 0);
    CHECK(Moved({ done }, { done }) == 0);   // completed before the death
    CHECK(Moved({ bounty }, {}) == 0);       // a quest that stopped, or let go of them: not a move

    watch.Start(1, 0.0f, { bounty });
    watch.Start(1, 1.0f, { home });  // died again: the new death's quests
    CHECK(watch.Stage(0x000A0001, 1.5f).empty());
    watch.Clear();
    CHECK(watch.Empty());
}

TEST_CASE("revive: in view of the camera")
{
    CHECK(InFrustum(kView, { 0.0f, 1000.0f, 0.0f }, 0.0f));               // straight ahead
    CHECK(InFrustum(kView, { 900.0f, 1000.0f, 0.0f }, 0.0f));             // 42 degrees right, inside 45
    CHECK_FALSE(InFrustum(kView, { 1200.0f, 1000.0f, 0.0f }, 0.0f));      // 50 degrees: off the side
    CHECK(InFrustum(kView, { 1050.0f, 1000.0f, 0.0f }, kPersonRadius));   // just off, but part of them shows
    CHECK_FALSE(InFrustum(kView, { 0.0f, 1000.0f, 700.0f }, 0.0f));       // above the top (tan 0.6)
    CHECK(InFrustum(kView, { 0.0f, 1000.0f, -500.0f }, 0.0f));
    CHECK_FALSE(InFrustum(kView, { 0.0f, -500.0f, 0.0f }, kPersonRadius));  // behind the camera
    CHECK(InFrustum(kView, { 0.0f, -40.0f, 0.0f }, kPersonRadius));         // at the lens: they'd fill the screen
    CHECK_FALSE(InFrustum(kView, { 0.0f, kViewRange + 500.0f, 0.0f }, kPersonRadius));  // too far to make out
    CHECK(InFrustum(kView, { 0.0f, kViewRange - 100.0f, 0.0f }, kPersonRadius));

    // Turned around (looking along -y), the same spots swap.
    View back = kView;
    back.forward = { 0.0f, -1.0f, 0.0f };
    back.right = { -1.0f, 0.0f, 0.0f };
    CHECK_FALSE(InFrustum(back, { 0.0f, 1000.0f, 0.0f }, kPersonRadius));
    CHECK(InFrustum(back, { 0.0f, -1000.0f, 0.0f }, kPersonRadius));

    CHECK(Seen(true, true, true));
    CHECK_FALSE(Seen(false, true, true));  // another interior, or outdoors while the player is inside
    CHECK_FALSE(Seen(true, false, true));  // looked away
    CHECK_FALSE(Seen(true, true, false));  // behind a wall
}

TEST_CASE("revive: an hour later, out of sight")
{
    constexpr float died = 10.5f;
    CHECK_FALSE(Ready(died, died + kDelayDays * 0.5f, false, false));  // too soon
    CHECK(Ready(died, died + kDelayDays, false, false));
    CHECK(Ready(died, died + 3.0f, false, false));   // days later
    CHECK_FALSE(Ready(died, died + 3.0f, true, false));  // the body is in view
    CHECK_FALSE(Ready(died, died + 3.0f, false, true));  // the spot is in view
    CHECK(Ready(died, died - 1.0f, false, false));   // a clock that went back

    CHECK_FALSE(Forget(died, died + 9.0f));
    CHECK(Forget(died, died + kForgetDays));  // Skyrim's cell reset has them
    CHECK_FALSE(Forget(died, died - 1.0f));
}

TEST_CASE("revive: the roster keeps each one once")
{
    Roster roster;
    CHECK(roster.Empty());
    roster.Note(0x00013BBF, 0x00013BB9, 2.0f);
    roster.Note(0x0001A6A1, 0x00013B9F, 1.0f);
    roster.Note(0x00013BBF, 0x00013BB9, 3.0f);  // back and killed again: the hour starts over
    REQUIRE(roster.Dead().size() == 2);
    CHECK(roster.Dead()[0] == Death{ 0x00013BBF, 0x00013BB9, 3.0f });
    CHECK(roster.Dead()[1] == Death{ 0x0001A6A1, 0x00013B9F, 1.0f });
    CHECK(roster.Remove(0x00013BBF));
    CHECK_FALSE(roster.Remove(0x00013BBF));
    CHECK(roster.Dead().size() == 1);
    roster.Clear();
    CHECK(roster.Empty());
}

TEST_CASE("revive: the co-save record round trips")
{
    Roster roster;
    roster.Note(0x00013BBF, 0x00013BB9, 2.25f);
    roster.Note(0x0001A6A1, 0x00013B9F, 1.0f);
    const auto bytes = Write(roster);
    CHECK(bytes.size() == 4 + 2 * 12);

    Roster back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back));
    CHECK(back.Dead() == roster.Dead());

    const auto empty = Write(Roster{});
    REQUIRE(Read(kVersion, empty.data(), empty.size(), back));
    CHECK(back.Empty());
}

TEST_CASE("revive: a record that doesn't read leaves nobody waiting")
{
    Roster roster;
    roster.Note(0x00013BBF, 0x00013BB9, 2.25f);
    auto bytes = Write(roster);

    Roster back;
    back.Note(1, 1, 1.0f);
    CHECK_FALSE(Read(kVersion + 1, bytes.data(), bytes.size(), back));  // newer
    CHECK(back.Empty());
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size() - 1, back));  // cut short
    CHECK_FALSE(Read(kVersion, bytes.data(), 2, back));

    const std::uint32_t huge = kListMax + 1;
    std::memcpy(bytes.data(), &huge, sizeof(huge));
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size(), back));

    roster.Clear();
    roster.Note(0x00013BBF, 0x00013BB9, std::numeric_limits<float>::quiet_NaN());
    bytes = Write(roster);
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size(), back));  // a time that isn't one
}
