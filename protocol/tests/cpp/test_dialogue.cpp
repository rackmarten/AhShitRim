#include <doctest/doctest.h>

#include "../../cpp/dialogue.h"

#include <cmath>
#include <set>

using namespace AhShitRim::Dialogue;
using AhShitRim::Pose::Vec3;

namespace
{
    float Distance(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z); }
}

TEST_CASE("the topic table is sorted, has one row per topic and a category for each")
{
    CHECK(kTopics.size() > 100);
    std::set<std::uint32_t> seen;
    for (std::size_t i = 0; i < kTopics.size(); ++i) {
        CHECK(kTopics[i].category != Category::None);
        CHECK(static_cast<std::uint8_t>(kTopics[i].category) < kCategoryCount);
        CHECK(kTopics[i].formId < 0x01000000u);  // Skyrim.esm's own forms
        if (i > 0) {
            CHECK(kTopics[i - 1].formId < kTopics[i].formId);
        }
        seen.insert(kTopics[i].formId);
    }
    CHECK(seen.size() == kTopics.size());
}

TEST_CASE("the table looks topics up by FormID")
{
    CHECK(TopicCategory(0x0005D2FC) == Category::Agree);   // MQ104: going along to the watchtower
    CHECK(TopicCategory(0x000D50B5) == Category::Agree);   // MQ103: taking on Farengar's errand
    CHECK(TopicCategory(0x000D552D) == Category::Agree);   // MQ102: offering the Jarl more help
    CHECK(TopicCategory(0x0009CC92) == Category::Agree);   // renting a room
    CHECK(TopicCategory(0x0002EC64) == Category::Greet);   // MQ105: answering the Greybeards' summons
    CHECK(TopicCategory(0x000D231F) == Category::Refuse);  // MQ102A: never mind
    CHECK(TopicCategory(0x0007F6BB) == Category::Ask);     // a merchant's wares
    CHECK(TopicCategory(0x00000000) == Category::None);
    CHECK(TopicCategory(0x0005D2FD) == Category::None);
    CHECK(TopicCategory(0xFFFFFFFF) == Category::None);
    CHECK(TopicCategory(kTopics.front().formId) == kTopics.front().category);
    CHECK(TopicCategory(kTopics.back().formId) == kTopics.back().category);
}

TEST_CASE("the rule reads words, lower case, without asides or punctuation")
{
    CHECK(Words("Sure. I'll Do it (50 gold)") == " sure i'll do it ");
    CHECK(Words("") == " ");
    CHECK(Words("((nested) aside) Hey!") == " hey ");
    CHECK(Words("What?! Now?") == " what now ");
}

TEST_CASE("the fallback rule sorts what the player says into CJ's categories")
{
    CHECK(Classify("Hey there, stranger.") == Category::Greet);
    CHECK(Classify("Hello.") == Category::Greet);
    CHECK(Classify("Sure, I'll handle it.") == Category::Agree);
    CHECK(Classify("I'll find your ring.") == Category::Agree);
    CHECK(Classify("I'd like a drink. (5 gold)") == Category::Agree);
    CHECK(Classify("No. Not today.") == Category::Refuse);
    CHECK(Classify("Not interested, pal.") == Category::Refuse);
    CHECK(Classify("Never mind.") == Category::Refuse);
    CHECK(Classify("Where can I find a horse?") == Category::Ask);
    CHECK(Classify("Tell me more about the city") == Category::Ask);
    CHECK(Classify("Is there no way out?") == Category::Ask);  // a question, though it says no
    CHECK(Classify("Hand it over. (Intimidate)") == Category::Threaten);
    CHECK(Classify("Pay up, or else.") == Category::Threaten);
    CHECK(Classify("Get out of my way.") == Category::Threaten);
    CHECK(Classify("Just kidding, friend.") == Category::Joke);
    CHECK(Classify("Goodbye.") == Category::Goodbye);
    CHECK(Classify("I'll come by later.") == Category::Goodbye);
    CHECK(Classify("Anyway, bye") == Category::Goodbye);
    CHECK(Classify("The bridge is out.") == Category::Ask);  // anything else
    CHECK(Classify("") == Category::Ask);
    CHECK(Classify("Nobody told me.") == Category::Ask);  // "no" only as a word of its own
    CHECK(Classify("Okayish weather") == Category::Ask);   // likewise "okay"
}

TEST_CASE("a picked topic takes the table's category, else the rule's")
{
    CHECK(CategoryOf(0x0005D2FC, "Goodbye.") == Category::Agree);
    CHECK(CategoryOf(0x0005D2FD, "Goodbye.") == Category::Goodbye);
    CHECK(CategoryOf(0, "No.") == Category::Refuse);
}

TEST_CASE("every category has lines of CJ's, none for nothing")
{
    for (std::uint8_t c = 1; c < kCategoryCount; ++c) {
        const auto lines = LinesOf(static_cast<Category>(c));
        CHECK_MESSAGE(!lines.empty(), NameOf(static_cast<Category>(c)));
        for (const auto& line : lines) {
            CHECK(AhShitRim::VoiceLines::Valid(line));
        }
    }
    CHECK(LinesOf(Category::None).empty());
    CHECK(std::string(NameOf(Category::Threaten)) == "threaten");
}

TEST_CASE("nobody talks to CJ in a vehicle or faster than a walk")
{
    CHECK(MayTalk(false, 0.0f));
    CHECK(MayTalk(false, 1.5f));  // walking
    CHECK(MayTalk(false, kTalkSpeedMax));
    CHECK_FALSE(MayTalk(false, 4.0f));   // jogging
    CHECK_FALSE(MayTalk(false, 30.0f));  // falling, or thrown
    CHECK_FALSE(MayTalk(true, 0.0f));    // parked: still in the vehicle
    CHECK_FALSE(MayTalk(true, 25.0f));
    CHECK_FALSE(MayTalk(false, std::nanf("")));
}

TEST_CASE("a new selection is a pick, what was there on opening isn't")
{
    PickTracker t;
    t.Open(0x1000, 0xA1, 0x55);              // the greeting plays, an old selection is left over
    CHECK_FALSE(t.Update(0x1000, 0xA1, 0x55));
    CHECK_FALSE(t.Update(0x1000, 0xA1, 0));  // the greeting ends
    CHECK(t.Update(0x2000, 0xB1, 0));        // a topic picked
    CHECK_FALSE(t.Update(0x2000, 0xB1, 0xB1));  // its answer starts
    CHECK_FALSE(t.Update(0x2000, 0xB1, 0xB1));
    CHECK_FALSE(t.Update(0x2000, 0xB1, 0));     // and ends
    CHECK(t.Update(0x2000, 0xB1, 0xB1));        // the same topic again: its answer starts again
    CHECK_FALSE(t.Update(0x2000, 0xB1, 0xB1));
    CHECK(t.Update(0x3000, 0xC1, 0xC1));        // picked and answered in the same frame
    CHECK_FALSE(t.Update(0x3000, 0xC1, 0xC1));
    CHECK_FALSE(t.Update(0x3000, 0xC1, 0));
    CHECK(t.Update(0x3000, 0xC2, 0));           // the same topic leading to another response
    CHECK_FALSE(t.Update(0, 0, 0));             // nothing selected
}

TEST_CASE("picking the topic left over from the last conversation counts once its answer starts")
{
    PickTracker t;
    t.Open(0x1000, 0xA1, 0);
    CHECK(t.Update(0x1000, 0xA1, 0xA1));
    CHECK_FALSE(t.Update(0x1000, 0xA1, 0xA1));
}

TEST_CASE("the director opens on the two-shot with a greeting, then cuts to the speaker")
{
    Director d;
    CHECK(d.Current() == Shot::None);
    CHECK(d.Pick(Category::Agree) == Category::None);  // no dialogue
    CHECK(d.Close() == Category::None);
    CHECK(d.Open() == Category::Greet);
    CHECK(d.IsOpen());
    CHECK(d.Current() == Shot::TwoShot);
    d.Tick(Director::kOpenTwoShot - 0.1f);
    CHECK(d.Current() == Shot::TwoShot);
    d.Tick(0.2f);
    CHECK(d.Current() == Shot::Speaker);
    CHECK(d.Pick(Category::Agree) == Category::Agree);  // CJ answers: back to the two-shot
    CHECK(d.Current() == Shot::TwoShot);
    CHECK(d.Pick(Category::None) == Category::None);
    d.Tick(Director::kLineTwoShot + 0.1f);
    CHECK(d.Current() == Shot::Speaker);
}

TEST_CASE("CJ says goodbye on leaving, unless his answer was the last word")
{
    Director d;
    d.Open();
    d.Tick(Director::kGoodbyeQuiet + 1.0f);
    CHECK(d.Close() == Category::Goodbye);
    CHECK_FALSE(d.IsOpen());
    CHECK(d.Current() == Shot::None);

    d.Open();
    d.Tick(10.0f);
    d.Pick(Category::Agree);  // a topic that ends the dialogue
    d.Tick(0.5f);
    CHECK(d.Close() == Category::None);

    d.Open();
    d.Tick(0.5f);  // Tab right after opening: the greeting is still going
    CHECK(d.Close() == Category::None);
}

TEST_CASE("the side of the line the camera keeps to")
{
    const Vec3 cj{ 0, 0, 100 }, speaker{ 0, 100, 100 };  // the speaker north of CJ
    CHECK(SideOf({ -50, 50, 0 }, cj, speaker) == 1);     // west: left looking north
    CHECK(SideOf({ 50, 50, 0 }, cj, speaker) == -1);
    CHECK(SideOf({ 50, -300, 0 }, cj, speaker) == -1);   // behind CJ, still to the right
}

TEST_CASE("the two-shot stands off to the side, both faces in view")
{
    const Vec3 cj{ 0, 0, 112 }, speaker{ 0, 100, 108 };
    for (const int side : { 1, -1 }) {
        const auto shot = Frame(Shot::TwoShot, cj, speaker, side);
        CHECK(shot.fov == doctest::Approx(kTwoShotFov));
        CHECK(shot.position.x * side < -150.0f);  // left is west here
        CHECK(Distance(shot.position, cj) == doctest::Approx(Distance(shot.position, speaker)).epsilon(0.02));
        CHECK(std::hypot(shot.forward.x, shot.forward.y, shot.forward.z) == doctest::Approx(1.0f));
        CHECK(SideOf(shot.position, cj, speaker) == side);
        // Both within the middle 60% of a 60 degree view.
        for (const auto& eye : { cj, speaker }) {
            const float tx = eye.x - shot.position.x, ty = eye.y - shot.position.y;
            const float angle = std::acos((tx * shot.forward.x + ty * shot.forward.y) / std::hypot(tx, ty) / std::hypot(shot.forward.x, shot.forward.y));
            CHECK(angle < 0.6f * kTwoShotFov / 2.0f * AhShitRim::Pose::kPi / 180.0f);
        }
    }
}

TEST_CASE("the speaker's shot looks past CJ's shoulder at the speaker's face")
{
    const Vec3 cj{ 0, 0, 112 }, speaker{ 0, 100, 108 };
    const auto shot = Frame(Shot::Speaker, cj, speaker, 1);
    CHECK(shot.fov == doctest::Approx(kSpeakerFov));
    CHECK(shot.position.y < cj.y);   // behind CJ
    CHECK(shot.position.x < 0.0f);   // over his left shoulder, the camera's side
    CHECK(shot.forward.y > 0.95f);   // towards the speaker
    CHECK(Distance(shot.target, speaker) < 10.0f);
    // CJ and the speaker on one spot: a direction is made up, nothing breaks.
    const auto same = Frame(Shot::Speaker, cj, cj, -1);
    CHECK(std::isfinite(same.forward.x));
    CHECK(std::hypot(same.forward.x, same.forward.y, same.forward.z) == doctest::Approx(1.0f));
}

TEST_CASE("a wall pulls the camera in towards what it looks at")
{
    auto shot = Frame(Shot::TwoShot, { 0, 0, 112 }, { 0, 100, 112 }, 1);
    const float before = Distance(shot.position, shot.target);
    const auto  forward = shot.forward;
    PullIn(shot, 1.0f);  // nothing hit
    CHECK(Distance(shot.position, shot.target) == doctest::Approx(before));
    PullIn(shot, 0.5f);
    CHECK(Distance(shot.position, shot.target) == doctest::Approx(before * 0.5f - 10.0f));
    CHECK(shot.forward.x == doctest::Approx(forward.x));
    PullIn(shot, 0.0f);  // a wall right there: no nearer than kNearest
    CHECK(Distance(shot.position, shot.target) == doctest::Approx(kNearest));
    PullIn(shot, std::nanf(""));
    CHECK(Distance(shot.position, shot.target) == doctest::Approx(kNearest));
}
