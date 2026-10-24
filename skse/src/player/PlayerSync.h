#pragma once

// Hands the Skyrim player to San Andreas and back (F10), as in PerkeleRim. While San Andreas owns
// the player:
//  - keyboard and mouse go to San Andreas as an InputState (WASD, mouse look, sprint, jump, crouch,
//    Alt walk, in the modern or classic layout of AhShitRim.ini's [Input] KeyLayout), and Skyrim's
//    own movement, look, sneak, jump, attack, activate and camera controls refuse; Escape, Tab and the
//    console stay Skyrim's (not M, the map: a letter of the cheat codes), and E activates what CJ faces (Activation);
//  - Skyrim's camera is first person, and every frame the PlayerCharacter::Update hook moves the
//    player by CJ's offset from the anchor, facing where San Andreas's camera looks;
//  - CollisionSampler streams Skyrim's collision around the player, which San Andreas builds CJ's
//    ground from.
// Each handover counts up InputState.handover and sends the player's feet as InputState.anchor:
// San Andreas puts CJ there facing the player's heading, under a fresh floating origin, and drops
// the older anchor's collision. When the game moves the player (a load door, fast travel, a script)
// the handover goes on, re-anchored where the game put the player (protocol/cpp/anchor.h).
// Ownership ends on F10 again, when the link goes down, the player dies, or a game loads.
namespace AhShitRim::Cosave
{
    struct Record;
}

namespace AhShitRim::PlayerSync
{
    void Install();       // hooks; call at SKSE load
    void OnDataLoaded();  // input and menu sinks, the key layout; call on kDataLoaded
    void OnGameLoad();    // Skyrim takes the player back; call on kPreLoadGame and kNewGame

    // The co-save (SaveState): what to write, what a loaded save held (the handover resumes by itself
    // once the game is up), and a new game or a load starting over.
    void Snapshot(Cosave::Record& a_out);

    // The load test (debug): whether San Andreas has the player, F10's toggle, and a cheat typed as its letters.
    bool DebugOwned();
    void DebugToggle();
    void DebugType(const char* a_code);

    void Restore(const Cosave::Record& a_record);
    void Revert();
}
