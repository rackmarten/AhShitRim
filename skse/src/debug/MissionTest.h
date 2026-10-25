#pragma once

// A check of the missions without anyone at the keyboard (debug only): with AHSHITRIM_MISSION_TEST set in
// Skyrim's environment (restart-games.ps1 passes its own on), the main menu loads the most recent save,
// and once in the game CJ's intro line plays through the real cue (as if Ralof's line had ended), then
// each console command of the variable (separated by ';', or "1" for resetquest MQ103, setstage MQ103 10
// and completequest MQ103; "skyrim" does the same with Skyrim taking the player back first) runs 10 s
// apart. AHSHITRIM_MISSION_SHOTS names a folder for a screenshot after each. Nothing is saved; the log
// says what the HUD got and San Andreas's log what it played.
namespace AhShitRim::MissionTest
{
    void OnDataLoaded();
    void Update(RE::PlayerCharacter* a_player, float a_delta);  // every frame from PlayerCharacter::Update
    bool On();
}
