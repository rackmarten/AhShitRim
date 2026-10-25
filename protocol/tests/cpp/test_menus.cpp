#include <doctest/doctest.h>

#include "../../cpp/menus.h"

using namespace AhShitRim::Menus;

TEST_CASE("a barter is closed only once it is open and the dialogue menu is gone")
{
    Closer closer;
    CHECK_FALSE(closer.Step(false, true, 0.016f));
    // Opening: the dialogue menu is still being taken down.
    for (int i = 0; i < 20; i++) {
        CHECK_FALSE(closer.Step(true, true, 0.016f));
    }
    // Gone: a moment more, then closed, once.
    CHECK_FALSE(closer.Step(true, false, 0.1f));
    CHECK(closer.Step(true, false, 0.1f));
    CHECK_FALSE(closer.Step(true, false, 0.1f));
    // Still open half a second later: sent again.
    CHECK_FALSE(closer.Step(true, false, 0.3f));
    CHECK(closer.Step(true, false, 0.2f));
    // Closed, then opened again (the second barter): the same wait.
    CHECK_FALSE(closer.Step(false, true, 0.016f));
    CHECK_FALSE(closer.Step(true, false, 0.1f));
    CHECK(closer.Step(true, false, 0.1f));
}

TEST_CASE("an inventory menu, with no dialogue, closes after the same moment")
{
    Closer closer;
    CHECK_FALSE(closer.Step(true, false, 0.1f));
    CHECK(closer.Step(true, false, 0.1f));
}

TEST_CASE("a dialogue left without its menu gets it back")
{
    DialogueWatch watch;
    CHECK_FALSE(watch.Step(true, false, false, 5.0f));  // not armed: nothing
    watch.Arm();
    CHECK(watch.Armed());
    CHECK_FALSE(watch.Step(true, false, true, 2.0f));   // the barter still open
    CHECK_FALSE(watch.Step(true, false, false, 0.5f));
    CHECK(watch.Step(true, false, false, 0.6f));        // a second without either: shown again
    CHECK(watch.Armed());
    CHECK_FALSE(watch.Step(true, true, false, 0.1f));   // it's back
    CHECK_FALSE(watch.Armed());
}

TEST_CASE("the watch gives up after a few tries, and stops when the dialogue ends")
{
    DialogueWatch watch;
    watch.Arm();
    int shown = 0;
    for (int i = 0; i < 20; i++) {
        shown += watch.Step(true, false, false, 0.5f) ? 1 : 0;
    }
    CHECK(shown == DialogueWatch::kTries);
    CHECK_FALSE(watch.Armed());

    watch.Arm();
    CHECK_FALSE(watch.Step(false, false, false, 2.0f));  // the dialogue is over
    CHECK_FALSE(watch.Armed());
}

TEST_CASE("F10 finds a dialogue without its menu")
{
    CHECK(Orphaned(true, false, false));
    CHECK_FALSE(Orphaned(true, true, false));
    CHECK_FALSE(Orphaned(true, false, true));
    CHECK_FALSE(Orphaned(false, false, false));
}
