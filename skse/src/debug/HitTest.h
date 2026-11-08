#pragma once

// A check, off unless AhShitRim.ini asks for it (v31): Skyrim's attacks on CJ's vehicle without anyone at the keys. With
// [Debug] HitTest=hits, bust or stealth it loads the most recent save from the main menu, goes to a cell (HitTestStart,
// `coc` Riverwood by default; with HitTestNewGame=1 it skips the save and starts a new game there, for a game version
// none of the saves is from, such as Special Edition 1.5.97), hands the player to San Andreas with CJ in a Sandking, and then
//  - hits: places a giant and two of Whiterun's guards 12 m ahead and sets them on the player, so their blows land on
//    the car (it smokes, burns and blows up, and CJ inside keeps his health until it does),
//  - bust: types the wanted cheat for two stars, so the guards come and one runs to the stopped car's door to bust him,
//  - stealth (v38): hands it over on foot instead, gives CJ the knife, places one of Whiterun's guards 1.6 m ahead with his
//    back to CJ (and a guard facing him off to the side), then holds the right mouse button and clicks the left
//    one, for the knife stealth kill; each one's StealthKill::Check verdict is logged every second and the guard's fate at the end,
//  - respawns (HitTest=respawns): hands CJ the player on foot instead, logs what a kill would do to everyone placed
//    in the editor nearby, has the player kill three that come back (and one that doesn't), presses / and waits
//    an hour in Skyrim's wait menu (1.1 game hours on the clock if Skyrim refuses it), stays 4 s (who gets up out of view, who waits in view), goes into an interior (HitTestAway, the Sleeping Giant Inn) and back out, and logs who is up and where,
//  - riders (HitTest=riders): places one of Whiterun's guards beside the player as a follower, hands CJ over in the Sandking
//    (the follower takes its passenger seat), presses F after 3 s and logs the follower's feet and the player's in the
//    vehicle's frame every frame until both have been out a second, then whether the follower got out smoothly (protocol/cpp/car_exit.h),
// and logs the car's and the player's health every 2 s for HitTestSeconds (90). Nothing is saved. Main thread only.
namespace AhShitRim::HitTest
{
    void OnDataLoaded();

    // Every player update, after Skyrim's own.
    void Update(RE::PlayerCharacter* a_player, float a_delta);
}
