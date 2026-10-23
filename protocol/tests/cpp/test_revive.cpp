#include <doctest/doctest.h>

#include "../../cpp/revive.h"

#include <limits>
#include <ostream>

using namespace AhShitRim::Revive;

TEST_CASE("revive: whose death counts")
{
    CHECK(Counts(true, true, true, 0x00013BBF));         // CJ killed Adrianne
    CHECK_FALSE(Counts(false, true, true, 0x00013BBF));  // Skyrim's own player did
    CHECK_FALSE(Counts(true, false, true, 0x00013BBF));  // a guard or a dragon did
    CHECK_FALSE(Counts(true, true, false, 0x00013BBF));  // a bandit: Skyrim brings its own back
    CHECK_FALSE(Counts(true, true, true, kPlayerRef));
    CHECK_FALSE(Counts(true, true, true, 0xFF000ABC));   // made at runtime: no editor spot
}

TEST_CASE("revive: where the editor spot can be seen")
{
    CHECK(HomeInView(true, true, false, 0.0f));          // an interior that's loaded
    CHECK_FALSE(HomeInView(true, false, true, 10.0f));   // an interior that isn't, however near
    CHECK(HomeInView(false, false, true, 4000.0f));      // outdoors, a cell away
    CHECK_FALSE(HomeInView(false, false, true, kHomeClear));
    CHECK_FALSE(HomeInView(false, false, false, 10.0f)); // the player is in another worldspace or inside
}

TEST_CASE("revive: an hour later, out of sight")
{
    constexpr float died = 10.5f;
    CHECK_FALSE(Ready(died, died + kDelayDays * 0.5f, false, false));  // too soon
    CHECK(Ready(died, died + kDelayDays, false, false));
    CHECK(Ready(died, died + 3.0f, false, false));                     // days later
    CHECK_FALSE(Ready(died, died + 3.0f, true, false));                // the body is still loaded
    CHECK_FALSE(Ready(died, died + 3.0f, false, true));                // the spot is in view
    CHECK(Ready(died, died - 1.0f, false, false));                     // a clock that went back
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
