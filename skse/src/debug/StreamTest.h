#pragma once

// A measurement, off unless AhShitRim.ini asks for it (v19): how Skyrim's cell streaming copes with the player moving
// as fast as a Hydra flies. With [Debug] StreamTest=<metres a second> it loads the most recent save from the main menu
// (StreamTestLoad=0 leaves that to you), goes to a cell (StreamTestStart, `coc` Riverwood by default), waits for it,
// then carries the player in a straight line (StreamTestHeading degrees from north, 0 by default) StreamTestHeight
// metres over where it started (30) for StreamTestSeconds (60), and logs every second and at the end: the frame times
// and stalls, and how often the land under the player and 1 s and 2 s ahead of it was loaded (its cell attached, with
// collision) and had a height. Nothing is saved. Main thread only.
namespace AhShitRim::StreamTest
{
    void OnDataLoaded();
    void OnPostLoadGame();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);

    // Whether it moves the player now (CJ isn't handed over meanwhile).
    bool Running();
}
