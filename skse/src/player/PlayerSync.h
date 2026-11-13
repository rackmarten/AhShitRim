#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "../ahshitrim_protocol.h"

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

    // Seconds since CJ was last seen crouching (Skyrim's player sneaking for him), -1 when never: Shots' sneak attack
    // counts a knife blow swung from a crouch that stood him up (Damage::CrouchedBlow).
    float SinceCrouched();

    // The load test (debug): whether San Andreas has the player, F10's toggle, and a cheat typed as its letters.
    bool          DebugOwned();
    std::uint32_t DebugHandover();  // the test kit: the current handover (InputState.handover), to match San Andreas's answers to it
    // The test kit: where vehicle slot a_slot was drawn last (its position, Skyrim world units), its heading (radians, Skyrim's:
    // 0 north, clockwise) and the model drawn (ModelView's id). False when San Andreas hasn't the player or the slot draws nothing.
    bool DebugVehicle(std::uint32_t a_slot, RE::NiPoint3& a_position, float& a_heading, std::uint32_t* a_model = nullptr);
    // The test kit: CJ's feet in Skyrim's world (where his last pose puts him) and his heading. False when San Andreas hasn't the player.
    bool DebugCj(RE::NiPoint3& a_feet, float& a_heading);
    void DebugToggle();
    void DebugType(const char* a_code);

    void Restore(const Cosave::Record& a_record);
    void Revert();

    // The hit test (debug, v31): San Andreas takes the player (again, if it had it), with CJ in a new vehicle of model a_model
    // where it stands.
    void TakeOverInVehicle(std::uint16_t a_model);

    // The test kit (debug): asks San Andreas for a vehicle of model a_model in front of CJ, as typing its name does. Why
    // it can't, or nothing.
    std::string DebugSpawn(std::uint16_t a_model);

    // The test kit (debug): input held for CJ on top of the keyboard's while San Andreas has the player and no menu has
    // the input: the stick (moveX right, moveY forward, -1 to 1; San Andreas moves CJ that way from its camera's view)
    // and Protocol::kButton* bits. nullopt lets go; so does 0.3 s without another call (a test whose request went away).
    struct DebugInput
    {
        float         moveX = 0.0f;
        float         moveY = 0.0f;
        std::uint32_t buttons = 0;
    };
    void DebugHold(const std::optional<DebugInput>& a_input);

    // The test kit (debug): called for each vehicle about to be drawn this frame, with its smoothed pose (which it may
    // change, for what's drawn this frame only) and its position in Skyrim's world. An empty function takes it out.
    using DebugVehicleHook = std::function<void(std::uint32_t a_slot, Protocol::BodyState& a_pose, const RE::NiPoint3& a_feet)>;
    void SetDebugVehicleHook(DebugVehicleHook a_hook);
}
