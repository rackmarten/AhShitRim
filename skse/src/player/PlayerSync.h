#pragma once

// Hands the Skyrim player to San Andreas and back (F10), as in PerkeleRim. While San Andreas owns
// the player:
//  - keyboard and mouse go to San Andreas as an InputState (WASD, mouse look, Shift sprint, Space
//    jump, C crouch, Alt walk), and Skyrim's own movement, look, sneak, jump, attack, activate and
//    camera controls refuse; Escape, Tab, M and the console stay Skyrim's;
//  - Skyrim's camera is first person, and every frame the PlayerCharacter::Update hook moves the
//    player by CJ's offset from where the handover started, facing where San Andreas's camera looks.
// Each handover counts up InputState.handover, which puts CJ back at the test plane's spawn facing
// the player's heading. Ownership ends on F10 again, when the link goes down, the player dies or
// is moved by the game (load door, fast travel), or a game loads.
namespace AhShitRim::PlayerSync
{
    void Install();       // hooks; call at SKSE load
    void OnDataLoaded();  // input and menu sinks; call on kDataLoaded
    void OnGameLoad();    // Skyrim takes the player back; call on kPreLoadGame and kNewGame
}
