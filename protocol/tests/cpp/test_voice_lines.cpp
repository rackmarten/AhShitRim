#include <doctest/doctest.h>

#include "../../cpp/voice_lines.h"

#include <set>

using namespace AhShitRim::VoiceLines;

TEST_CASE("the picked lines are in CJ's voice banks")
{
    for (const auto& line : kInsult) {
        CHECK(Valid(line));
    }
    for (const auto& line : kSwear) {
        CHECK(Valid(line));
    }
    CHECK(Valid({ 700, 81 }));
    CHECK_FALSE(Valid({ 689, 0 }));
    CHECK_FALSE(Valid({ 710, 0 }));
}

TEST_CASE("the deck plays every line once before any repeats, never the same one twice in a row")
{
    constexpr Line lines[] = { { 700, 1 }, { 700, 2 }, { 701, 3 }, { 690, 4 }, { 709, 5 } };
    Deck deck(lines, 7);
    CHECK_FALSE(deck.Empty());
    std::uint16_t last = 0xFFFF;
    for (int round = 0; round < 20; ++round) {
        std::set<std::uint16_t> seen;
        for (std::size_t i = 0; i < std::size(lines); ++i) {
            const auto line = deck.Next();
            CHECK(line.sound != last);
            seen.insert(line.sound);
            last = line.sound;
        }
        CHECK(seen.size() == std::size(lines));
    }
}

TEST_CASE("a deck of one plays it every time, an empty deck says so")
{
    constexpr Line one[] = { { 700, 81 } };
    Deck deck(one);
    CHECK(deck.Next().sound == 81);
    CHECK(deck.Next().sound == 81);
    CHECK(Deck{}.Empty());
}
