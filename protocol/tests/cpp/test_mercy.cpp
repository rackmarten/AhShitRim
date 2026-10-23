#include <doctest/doctest.h>

#include "../../cpp/mercy.h"

#include <ostream>

using namespace AhShitRim::Mercy;

TEST_CASE("mercy: who loses their flag")
{
    CHECK(Clears(true, true, 0x00013BBF, 0x00013BB9));  // an essential actor
    CHECK_FALSE(Clears(false, true, 0x00013BBF, 0x00013BB9));  // off
    CHECK_FALSE(Clears(true, false, 0x00013BBF, 0x00013BB9));  // not essential: nothing to put back later
    CHECK_FALSE(Clears(true, true, kPlayerRef, kPlayerBase));  // the player is essential for Wasted
    CHECK_FALSE(Clears(true, true, 0, kPlayerBase));           // the player's base
}

TEST_CASE("mercy: each cleared flag is noted once")
{
    Ledger ledger;
    CHECK_FALSE(ledger.On());
    CHECK(ledger.Empty());
    ledger.SetOn(true);
    CHECK(ledger.NoteBase(0x0001A694));
    CHECK(ledger.NoteBase(0x00013BB9));
    CHECK_FALSE(ledger.NoteBase(0x0001A694));  // the scan sees it again
    CHECK(ledger.NoteActor(0x00013BBF, 0x00013BB9));
    CHECK(ledger.NoteActor(0xFF000ABC, 0x0001A694));  // a temporary reference
    CHECK_FALSE(ledger.NoteActor(0x00013BBF, 0x00013BB9));  // an alias set its flag again, and it was cleared again
    CHECK(ledger.Bases() == std::vector<std::uint32_t>{ 0x00013BB9, 0x0001A694 });
    CHECK(ledger.Actors().size() == 2);
    CHECK(ledger.Actors()[0] == Actor{ 0x00013BBF, 0x00013BB9 });
    CHECK_FALSE(ledger.Empty());
}

TEST_CASE("mercy: toggled off, exactly the noted flags come back")
{
    Ledger ledger;
    ledger.SetOn(true);
    ledger.NoteBase(0x00013BB9);
    ledger.NoteActor(0x00013BBF, 0x00013BB9);
    const auto cleared = ledger.TakeAll();
    CHECK(cleared.bases == std::vector<std::uint32_t>{ 0x00013BB9 });
    CHECK(cleared.actors == std::vector<Actor>{ { 0x00013BBF, 0x00013BB9 } });
    CHECK_FALSE(ledger.On());
    CHECK(ledger.Empty());
    // On again later: noted afresh.
    ledger.SetOn(true);
    CHECK(ledger.NoteBase(0x00013BB9));
}

TEST_CASE("mercy: a load gives back this session's bases and keeps the actors")
{
    Ledger ledger;
    ledger.SetOn(true);
    ledger.NoteBase(0x00013BB9);
    ledger.NoteActor(0x00013BBF, 0x00013BB9);
    CHECK(ledger.TakeBases() == std::vector<std::uint32_t>{ 0x00013BB9 });
    CHECK(ledger.Bases().empty());
    CHECK(ledger.Actors().size() == 1);
    CHECK(ledger.On());
}

TEST_CASE("mercy: the co-save record round trips")
{
    Ledger ledger;
    ledger.SetOn(true);
    ledger.NoteBase(0x0001A694);
    ledger.NoteBase(0x00013BB9);
    ledger.NoteActor(0x00013BBF, 0x00013BB9);
    ledger.NoteActor(0xFF000ABC, 0x0001A694);
    const auto bytes = Write(ledger);
    CHECK(bytes.size() == 1 + 4 + 2 * 4 + 4 + 2 * 8);

    Ledger back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back));
    CHECK(back.On());
    CHECK(back.Bases() == ledger.Bases());
    CHECK(back.Actors() == ledger.Actors());

    Ledger off;
    const auto none = Write(off);
    REQUIRE(Read(kVersion, none.data(), none.size(), back));
    CHECK_FALSE(back.On());
    CHECK(back.Empty());
}

TEST_CASE("mercy: a record that doesn't read is left off")
{
    Ledger ledger;
    ledger.SetOn(true);
    ledger.NoteBase(0x00013BB9);
    ledger.NoteActor(0x00013BBF, 0x00013BB9);
    const auto bytes = Write(ledger);

    Ledger back;
    CHECK_FALSE(Read(kVersion + 1, bytes.data(), bytes.size(), back));  // a later AhShitRim's
    CHECK_FALSE(back.On());
    CHECK_FALSE(Read(0, bytes.data(), bytes.size(), back));
    CHECK_FALSE(Read(kVersion, bytes.data(), 0, back));
    CHECK_FALSE(Read(kVersion, bytes.data(), bytes.size() - 1, back));  // cut short
    CHECK_FALSE(back.On());
    CHECK(back.Empty());

    std::vector<std::uint8_t> huge = { 1, 0xFF, 0xFF, 0xFF, 0xFF };  // a count past any load order
    CHECK_FALSE(Read(kVersion, huge.data(), huge.size(), back));
}
