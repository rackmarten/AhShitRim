#pragma once

// A check, off unless AhShitRim.ini asks for it (v31): Skyrim's attacks on CJ's vehicle without anyone at the keys. With
// [Debug] HitTest=hits or HitTest=bust it loads the most recent save from the main menu, goes to a cell (HitTestStart,
// `coc` Riverwood by default), hands the player to San Andreas with CJ in a Sandking, and then
//  - hits: places a giant and two of Whiterun's guards 12 m ahead and sets them on the player, so their blows land on
//    the car (it smokes, burns and blows up, and CJ inside keeps his health until it does),
//  - bust: types the wanted cheat for two stars, so the guards come and one runs to the stopped car's door to bust him,
// and logs the car's and the player's health every 2 s for HitTestSeconds (90). Nothing is saved. Main thread only.
namespace AhShitRim::HitTest
{
    void OnDataLoaded();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
