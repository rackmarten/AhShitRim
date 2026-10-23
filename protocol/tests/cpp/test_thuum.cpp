#include <doctest/doctest.h>

#include "../../cpp/thuum.h"

#include <set>
#include <string>
#include <utility>

using namespace AhShitRim;
using namespace AhShitRim::Thuum;
using VoiceLines::Line;
using VoiceLines::ShoutPick;

namespace
{
    bool In(std::span<const Line> a_lines, Line a_line)
    {
        for (const auto& l : a_lines) {
            if (l.bank == a_line.bank && l.sound == a_line.sound) {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("the shout table finds each shout by its file and form ID, and by its key")
{
    std::set<std::string_view>                          keys;
    std::set<std::pair<std::string_view, std::uint32_t>> forms;
    for (std::size_t i = 0; i < kShouts.size(); ++i) {
        const auto& shout = kShouts[i];
        CHECK(keys.insert(shout.key).second);
        CHECK(forms.insert({ shout.file, shout.formId }).second);
        CHECK(shout.formId <= 0xFFFFFF);
        CHECK(Find(shout.file, shout.formId) == static_cast<int>(i));
        CHECK(Find(shout.key) == static_cast<int>(i));
    }
    CHECK(kShouts.size() == 27);
    CHECK(Find("Skyrim.esm", 0x013E07) == Find("unrelenting-force"));
    CHECK(Find("SKYRIM.ESM", 0x013E07) == 0);                    // any case
    CHECK(Find("Dawnguard.esm", 0x02008A62) == Find("drain-vitality"));  // the load order byte doesn't matter
    CHECK(Find("Skyrim.esm", 0x0E40C4) == -1);                   // Nord Battle Cry, a racial power
    CHECK(Find("Dawnguard.esm", 0x013E07) == -1);
    CHECK(Find("no-such-shout") == -1);
}

TEST_CASE("shoutVoice carries the shout and the words in one byte")
{
    for (int shout = -1; shout < static_cast<int>(kShouts.size()); ++shout) {
        for (int words = 1; words <= kMaxWords; ++words) {
            const auto voice = Encode(shout, words);
            CHECK(ShoutOf(voice) == shout);
            CHECK(WordsOf(voice) == words);
        }
    }
    CHECK(WordsOf(Encode(0, 0)) == 1);  // clamped
    CHECK(WordsOf(Encode(0, 7)) == 3);
    CHECK(ShoutOf(Encode(99, 2)) == -1);  // out of the table: generic
    CHECK(ShoutOf(0) == -1);
    CHECK(WordsOf(0) == 1);
}

TEST_CASE("the picked shout lines are CJ's, for shouts in the table, and the generic pool isn't empty")
{
    for (const auto& pick : VoiceLines::kShoutPicks) {
        CHECK(Find(pick.shout) >= 0);
        CHECK(pick.words >= 1);
        CHECK(pick.words <= kMaxWords);
        CHECK(VoiceLines::Valid(pick.line));
    }
    CHECK_FALSE(VoiceLines::kShoutGeneric.empty());
    for (const auto& line : VoiceLines::kShoutGeneric) {
        CHECK(VoiceLines::Valid(line));
    }
    Voice voice(VoiceLines::kShoutPicks, VoiceLines::kShoutGeneric);
    for (int shout = -1; shout < static_cast<int>(kShouts.size()); ++shout) {
        for (int words = 1; words <= kMaxWords; ++words) {
            CHECK_FALSE(voice.For(shout, words).Empty());  // no shout is silent
        }
    }
}

TEST_CASE("a shout plays its picks for the words charged, else the nearest word count it has, else the generic pool")
{
    constexpr ShoutPick picks[] = {
        { "unrelenting-force", 1, { 700, 1 } },
        { "unrelenting-force", 1, { 700, 2 } },
        { "unrelenting-force", 3, { 690, 38 } },
        { "whirlwind-sprint", 2, { 701, 5 } },
        { "no-such-shout", 1, { 700, 9 } },     // dropped
        { "disarm", 4, { 700, 9 } },            // dropped
        { "disarm", 1, { 800, 9 } },            // not CJ's: dropped
    };
    constexpr Line generic[] = { { 692, 10 }, { 692, 11 } };
    Voice      voice(picks, generic, 3);
    const int  force = Find("unrelenting-force");
    const int  sprint = Find("whirlwind-sprint");
    const int  disarm = Find("disarm");

    CHECK(voice.PoolWords(force, 1) == 1);
    CHECK(voice.PoolWords(force, 3) == 3);
    CHECK(voice.PoolWords(force, 2) == 1);  // a tie between 1 and 3: fewer words
    CHECK(voice.PoolWords(sprint, 1) == 2);
    CHECK(voice.PoolWords(sprint, 3) == 2);
    CHECK(voice.PoolWords(disarm, 1) == 0);  // no picks: generic
    CHECK(voice.PoolWords(-1, 2) == 0);

    constexpr Line forceOne[] = { { 700, 1 }, { 700, 2 } };
    for (int i = 0; i < 10; ++i) {
        CHECK(In(forceOne, voice.For(force, 1).Next()));
        CHECK(voice.For(force, 3).Next().sound == 38);
        CHECK(voice.For(sprint, 1).Next().sound == 5);
        CHECK(In(generic, voice.For(disarm, 2).Next()));
        CHECK(In(generic, voice.For(-1, 3).Next()));
    }
}

TEST_CASE("a shout's lines never repeat back to back, and every one comes up")
{
    constexpr ShoutPick picks[] = {
        { "fire-breath", 3, { 690, 1 } },
        { "fire-breath", 3, { 690, 2 } },
        { "fire-breath", 3, { 690, 3 } },
        { "fire-breath", 3, { 690, 4 } },
    };
    Voice           voice(picks, VoiceLines::kShoutGeneric, 11);
    const int       shout = Find("fire-breath");
    std::uint16_t   last = 0xFFFF;
    std::set<int>   seen;
    for (int i = 0; i < 40; ++i) {
        const auto line = voice.For(shout, 3).Next();
        CHECK(line.sound != last);
        last = line.sound;
        seen.insert(line.sound);
    }
    CHECK(seen.size() == 4);
}

TEST_CASE("the Dragonborn's voice topics are Skyrim.esm's four VoicePowers topics")
{
    CHECK(kVoiceTopics.size() == 4);
    std::set<std::uint32_t> ids(kVoiceTopics.begin(), kVoiceTopics.end());
    CHECK(ids.size() == 4);
    CHECK(ids.count(0x013E01) == 1);  // Shout01a, the first word
}
