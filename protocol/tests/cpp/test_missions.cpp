#include <doctest/doctest.h>

#include "../../cpp/missions.h"

using namespace AhShitRim::Missions;
namespace Protocol = AhShitRim::Protocol;

namespace
{
    constexpr int kMainQuest = 1;
    constexpr int kSideQuest = 8;
    constexpr int kHudQuestFailed = 16;
    constexpr int kHudObjectiveStarted = 17;
}

TEST_CASE("a quest starting shows its title, a quest completed mission passed; misc quests stay Skyrim's")
{
    CHECK(NoticeOf(kHudQuestStarted, kMainQuest, "Bleak Falls Barrow") == Notice::Title);
    CHECK(NoticeOf(kHudQuestComplete, kMainQuest, "Before the Storm") == Notice::Passed);
    CHECK(NoticeOf(kHudQuestStarted, kSideQuest, "The Golden Claw") == Notice::Title);
    CHECK(NoticeOf(kHudQuestStarted, kQuestMiscellaneous, "Miscellaneous") == Notice::None);
    CHECK(NoticeOf(kHudQuestComplete, kQuestMiscellaneous, "Miscellaneous") == Notice::None);
    CHECK(NoticeOf(kHudQuestFailed, kMainQuest, "Unbound") == Notice::None);
    CHECK(NoticeOf(kHudObjectiveStarted, kMainQuest, "Unbound") == Notice::None);
    CHECK(NoticeOf(kHudQuestStarted, kMainQuest, "") == Notice::None);
}

TEST_CASE("notices come one at a time, a title for as long as it shows")
{
    Sequence s;
    CHECK(s.Idle());
    s.Push(Notice::Title, "Bleak Falls Barrow");
    s.Push(Notice::Title, "The Golden Claw");
    auto step = s.Tick(0.016f);
    CHECK(step.notice == Notice::Title);
    CHECK(step.name == "Bleak Falls Barrow");
    CHECK(s.Tick(kTitleShown).notice == Notice::None);  // the first still fading, then the gap
    step = s.Tick(kGapSeconds + 0.01f);
    CHECK(step.notice == Notice::Title);
    CHECK(step.name == "The Golden Claw");
    CHECK(!s.Idle());
    s.Tick(kTitleShown + kGapSeconds + 0.01f);
    CHECK(s.Idle());
}

TEST_CASE("a mission passes before the next one's title, and only once its tune is over")
{
    Sequence s;
    s.Push(Notice::Title, "Bleak Falls Barrow");  // Skyrim may send the next start first
    s.Push(Notice::Passed, "Before the Storm");
    auto step = s.Tick(0.016f);
    CHECK(step.notice == Notice::Passed);
    CHECK(step.name == "Before the Storm");
    CHECK(SecondsOf(Notice::Passed) >= kTuneSeconds);
    CHECK(SecondsOf(Notice::Passed) >= kPassedSeconds);
    CHECK(s.Tick(kTuneSeconds - 0.1f).notice == Notice::None);
    step = s.Tick(1.0f);
    CHECK(step.notice == Notice::Title);
    CHECK(step.name == "Bleak Falls Barrow");
}

TEST_CASE("a passed mission waits for the title already up, but goes before those waiting")
{
    Sequence s;
    s.Push(Notice::Title, "Unbound");
    CHECK(s.Tick(0.016f).name == "Unbound");
    s.Push(Notice::Title, "Bleak Falls Barrow");
    s.Push(Notice::Passed, "Before the Storm");
    CHECK(s.Tick(1.0f).notice == Notice::None);
    CHECK(s.Tick(kTitleShown).name == "Before the Storm");
}

TEST_CASE("the same notice of the same quest twice shows once")
{
    Sequence s;
    s.Push(Notice::Title, "Bleak Falls Barrow");
    s.Push(Notice::Title, "Bleak Falls Barrow");
    s.Push(Notice::Passed, "Bleak Falls Barrow");
    s.Push(Notice::None, "Nothing");
    CHECK(s.Waiting() == 2);
    s.Clear();
    CHECK(s.Idle());
    CHECK(s.Tick(1.0f).notice == Notice::None);
}

TEST_CASE("CJ answers Ralof's first line in the cart, once until a game loads")
{
    Intro intro;
    intro.OnTopicEnd(0x00101AEB);  // another line: nothing
    CHECK(!intro.Waiting());
    CHECK(!intro.Tick(1.0f));
    intro.OnTopicEnd(kRalofFinallyAwake);
    CHECK(intro.Waiting());
    CHECK(!intro.Tick(kIntroDelay / 2.0f));
    CHECK(intro.Tick(kIntroDelay / 2.0f + 0.01f));
    CHECK(!intro.Tick(1.0f));  // once
    intro.OnTopicEnd(kRalofFinallyAwake);
    CHECK(!intro.Waiting());
    intro.Clear();  // a save from before the cart
    intro.OnTopicEnd(kRalofFinallyAwake);
    CHECK(intro.Tick(kIntroDelay + 0.01f));
}

TEST_CASE("a game loading while CJ waits to answer drops it")
{
    Intro intro;
    intro.OnTopicEnd(kRalofFinallyAwake);
    intro.Clear();
    CHECK(!intro.Tick(1.0f));
}

TEST_CASE("a cue plays for its sound and the time to load it")
{
    CHECK(CueSeconds(Protocol::kCueMissionPassed) == doctest::Approx(kTuneSeconds + kCueLoadSeconds));
    CHECK(CueSeconds(Protocol::kCueIntroLine) == doctest::Approx(kIntroLineSeconds + kCueLoadSeconds));
    CHECK(CueSeconds(0) == 0.0f);
    CueTimer t;
    CHECK(!t.Playing());
    t.Start(Protocol::kCueIntroLine);
    t.Start(0);
    CHECK(t.Playing());
    t.Tick(kIntroLineSeconds);
    CHECK(t.Playing());
    t.Start(Protocol::kCueMissionPassed);  // the longer one wins
    t.Tick(kIntroLineSeconds + kCueLoadSeconds);
    CHECK(t.Playing());
    t.Tick(kTuneSeconds);
    CHECK(!t.Playing());
    t.Start(Protocol::kCueMissionPassed);
    t.Clear();
    CHECK(!t.Playing());
}

TEST_CASE("a misc quest's objective completed is a candidate odd job while MiscPassed is on, never a title")
{
    constexpr int kHudObjectiveCompleted = 18;
    CHECK(MiscCandidate(kHudObjectiveCompleted, kQuestMiscellaneous, true));
    CHECK(MiscCandidate(kHudQuestComplete, kQuestMiscellaneous, true));
    CHECK(!MiscCandidate(kHudObjectiveCompleted, kQuestMiscellaneous, false));  // MiscPassed=0: nothing
    CHECK(!MiscCandidate(kHudQuestComplete, kQuestMiscellaneous, false));
    CHECK(!MiscCandidate(kHudObjectiveCompleted, kMainQuest, true));  // a main quest's objective is no mission
    CHECK(!MiscCandidate(kHudQuestStarted, kQuestMiscellaneous, true));
    CHECK(!MiscCandidate(kHudObjectiveStarted, kQuestMiscellaneous, true));
    CHECK(NoticeOf(kHudQuestStarted, kQuestMiscellaneous, "A Lovely Letter") == Notice::None);  // no title, so fetch quests don't spam
}

TEST_CASE("a misc quest that ends after its objective is an odd job passed, once")
{
    MiscWatch w;
    bool      ended = false;
    const auto endedIf = [&](std::uint32_t) { return ended; };
    w.Add(0x74A83, "A Lovely Letter");  // "Deliver Faendal's letter to Camilla" completed: the quest goes on
    CHECK(w.Tick(1.0f, endedIf).empty());
    CHECK(w.Tick(kMiscWatchSeconds, endedIf).empty());  // it didn't end: dropped
    CHECK(w.Watching() == 0);
    w.Add(0x74A83, "A Lovely Letter");  // "Talk to Faendal" completed, and the script stops it
    ended = true;
    const auto steps = w.Tick(0.016f, endedIf);
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].notice == Notice::OddJob);
    CHECK(steps[0].name == "A Lovely Letter");
    w.Add(0x74A83, "A Lovely Letter");  // once
    CHECK(w.Tick(0.016f, endedIf).empty());
    w.Clear();  // a game loads
    w.Add(0x74A83, "A Lovely Letter");
    CHECK(w.Tick(0.016f, endedIf).size() == 1);
}

TEST_CASE("an odd job passes like a mission: before waiting titles, for as long as its tune")
{
    CHECK(IsPassed(Notice::OddJob));
    CHECK(SecondsOf(Notice::OddJob) == SecondsOf(Notice::Passed));
    Sequence s;
    s.Push(Notice::Title, "Bleak Falls Barrow");
    s.Push(Notice::OddJob, "A Lovely Letter");
    CHECK(s.Tick(0.016f).notice == Notice::OddJob);
    CHECK(s.Tick(kTuneSeconds - 0.1f).notice == Notice::None);
    CHECK(s.Tick(1.0f).name == "Bleak Falls Barrow");
}
