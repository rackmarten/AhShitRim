#pragma once

#include <cstdint>

namespace RE
{
    class HUDData;
}

// Quests as San Andreas's missions (v23, the rules in protocol/cpp/missions.h), whoever has the player:
// - Skyrim's HUD message for a quest starting (it sends one once a quest has a journal entry) shows the
//   quest's name as San Andreas's mission title, and the one for a quest completed shows "mission
//   passed!" and "respect +" with San Andreas's mission passed tune, one after another. Skyrim's own
//   notification is swallowed. Objectives and failed quests stay Skyrim's, and so do misc quests, but
//   for one that ends after its objective is completed (Skyrim sends no "quest completed" for those):
//   that is an odd job passed, "mission passed!" alone with the tune ([Missions] MiscPassed=0 turns it off).
// - The Helgen intro: when Ralof's "Hey, you. You're finally awake." in the cart ends (Skyrim's topic
//   info event), CJ says "Ah shit, here we go again." from San Andreas's intro voice-over, with
//   San Andreas's subtitle.
// The sounds are cues San Andreas plays by its own ids (InputState.cue); while Skyrim has the player,
// San Andreas is heard for as long as one plays.
namespace AhShitRim::Missions
{
    void OnDataLoaded();  // listens to Skyrim's topic info events

    // From the HUD menu's message hook (the UI thread): true when San Andreas shows it instead of Skyrim.
    bool OnHudMessage(const RE::HUDData& a_data);

    // Every frame (not while the game is paused): shows what is due. Returns a Protocol::kCue* for San
    // Andreas to play now, 0 for none.
    std::uint8_t Update(float a_delta);

    // A cue is playing: San Andreas has to be heard while Skyrim has the player.
    bool CuePlaying();

    void OnGameLoad();  // what was waiting belonged to the game before

    // Debug (MissionTest): CJ answers as if Ralof's line had just ended.
    void TestIntro();
}
