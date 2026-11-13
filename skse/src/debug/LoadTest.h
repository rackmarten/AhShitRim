#pragma once

#include <optional>
#include <string>

// One cycle of the load test, the crashes that come with loads (scripts/load-test.ps1 runs hundreds of them through
// the test kit's asr.load, skse/src/debug/TestKit.cpp): the save loaded through the console's own load (whoever is in
// a fight as the load goes is logged first: whatever crashes in the load's combat clearing is one of them), the game
// left to settle, the player handed to San Andreas, then one scenario:
//   idle, flee (a blast scares the people about), chase (6 stars, the next load with the force out), asnaeb and aezakmi
//   (6 stars, cleared with the cheat), travel (a coc to Riverwood, its load screen), handback (F10 hands it back).
// Every step is logged ("load test: ...") and kept as a note for the kit's rows. With a_saveAs the game is saved under
// that name once the load has settled (after the police took out copies left in the save), before the handover;
// nothing else is ever saved. A cycle nobody has asked for notes for 3600 player updates (60 s: the kit's request went
// away) stops. Main thread only.
namespace AhShitRim::LoadTest
{
    struct Note
    {
        std::string time;            // when, "2026-11-13 02:41:52" (UTC, as the system clock has it)
        float       seconds = 0.0f;  // since Begin, game time while settled
        std::string what;
    };

    // Starts a cycle; a reason when it can't (a scenario it doesn't know, a cycle still going).
    std::string Begin(const std::string& a_save, const std::string& a_scenario, const std::string& a_saveAs = {});

    // Ends a cycle still going (nothing more is done for it).
    void Stop();

    bool Running();
    bool Done();  // the last cycle got through its scenario

    // The next note since the last call, in order. Calling it is what keeps a cycle going (see above).
    std::optional<Note> NextNote();

    // The scenarios' names, for the usage: "idle, flee, ...".
    std::string Scenarios();

    void OnPostLoadGame();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
