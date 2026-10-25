#pragma once

// A check, off unless AhShitRim.ini asks for it: loads, over and over, to catch the crashes that come with them. With
// [Debug] LoadTest=cycles (and LoadTestSaves=StressA,StressB, the saves it alternates between) it loads the first save
// from the main menu and then runs cycle after cycle: the next save loaded through the console's own load, the game left
// to settle, the player handed to San Andreas, then one scenario before the next load:
//   idle, flee (a blast scares the people about), chase (6 stars, the next load with the force out), asnaeb and aezakmi
//   (6 stars, cleared with the cheat), travel (a coc to Riverwood, its load screen), handback (F10 hands it back).
// LoadTestScenarios=chase,asnaeb (any of those names) runs only those, in turn.
// LoadTestSaveAs=Name saves the game under that name once, after the first load has settled (a save cleaned of
// anything the police takes out after a load).
// Each cycle is counted in Documents/My Games/AhShitRim/loadtest-progress.txt before it starts, so after a crash the
// game, started again, goes on with the next one; every step goes to loadtest.csv beside it. Nothing is saved. Main
// thread only.
namespace AhShitRim::LoadTest
{
    void OnDataLoaded();
    void OnPostLoadGame();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
