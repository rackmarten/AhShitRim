#pragma once

// Hands the Skyrim player to San Andreas and back (F10), as in PerkeleRim. While San Andreas owns
// the player:
//  - keyboard and mouse go to San Andreas as an InputState (WASD, mouse look, Shift sprint, Space
//    jump, C crouch, Alt walk), and Skyrim's own movement, look, sneak, jump, attack, activate and
//    camera controls refuse; Escape, Tab, M and the console stay Skyrim's;
//  - Skyrim's camera is first person, and every frame the PlayerCharacter::Update hook moves the
//    player by CJ's offset from where the handover started, facing where San Andreas's camera looks;
//  - CollisionSampler streams Skyrim's collision around the player, which San Andreas builds CJ's
//    ground from.
// Each handover counts up InputState.handover and sends the player's feet as InputState.anchor:
// San Andreas puts CJ there facing the player's heading, under a fresh floating origin, and drops
// the older handover's collision. Ownership ends on F10 again, when the link goes down, the player dies or
// is moved by the game (load door, fast travel), or a game loads.
namespace AhShitRim::PlayerSync
{
    void Install();       // hooks; call at SKSE load
    void OnDataLoaded();  // input and menu sinks; call on kDataLoaded
    void OnGameLoad();    // Skyrim takes the player back; call on kPreLoadGame and kNewGame
}
