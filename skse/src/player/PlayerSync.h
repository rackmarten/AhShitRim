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

namespace AhShitRim::Protocol
{
    struct BodyState;
}

namespace AhShitRim::PlayerSync
{
    void Install();       // hooks; call at SKSE load
    void OnDataLoaded();  // input and menu sinks, the key layout; call on kDataLoaded
    void OnGameLoad();    // Skyrim takes the player back; call on kPreLoadGame and kNewGame

    // The co-save (SaveState): what to write, what a loaded save held (the handover resumes by itself
    // once the game is up), and a new game or a load starting over.
    void Snapshot(Cosave::Record& a_out);

    // Whether the gamepad was the last thing used (DESIGN.md, Controller): the help box names its buttons ("LB: Open").
    bool PadInUse();

    // The load test (debug): whether San Andreas has the player, F10's toggle, and a cheat typed as its letters.
    bool DebugOwned();
    void DebugToggle();
    void DebugType(const char* a_code);
    // The hit test's stealth scenario (debug, v38): the mouse's aim and fire buttons held as if pressed.
    void DebugButtons(bool a_aim, bool a_fire);
    // The hit test's pad scenario (debug): a gamepad button (a Skyrim gamepad id code, Pad::k*) or stick as Skyrim's
    // gamepad events would set them.
    void DebugPad(std::uint32_t a_id, float a_value);
    void DebugStick(bool a_right, float a_x, float a_y);
    int  DebugPadCheats();  // console cheat sequences finished on the pad so far
    // The hit test's riders scenario (debug): a key (a DirectInput scan code) held as if pressed.
    void DebugKey(std::uint32_t a_dik, bool a_down);
    // The hit test's drive scenario (debug): CJ's pose as DrawCj drew it this frame (smoothed), his feet and San
    // Andreas's camera in the world; false on a frame he wasn't drawn.
    bool DebugDrawn(Protocol::BodyState& a_pose, RE::NiPoint3& a_feet, RE::NiPoint3& a_camera);
    // The hit test's respawns scenario (debug): / pressed, Skyrim's wait menu as the key opens it.
    void DebugWait();

    void Restore(const Cosave::Record& a_record);
    void Revert();

    // The hit test (debug, v31): San Andreas takes the player (again, if it had it), with CJ in a new vehicle of model a_model
    // where it stands.
    void TakeOverInVehicle(std::uint16_t a_model);
}
