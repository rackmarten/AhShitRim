#pragma once

// Skyrim's HUD messages while San Andreas has the player, by protocol/cpp/hud_messages.h's table: the
// location Skyrim names on entering or discovering it comes up as San Andreas's zone name, notifications
// that matter (what CJ can't do) in San Andreas's help box, and the junk (items added and removed, spells
// learned, "you feel...", bounty added) not at all. Quest texts and subtitles stay with Missions and
// Conversation, Skyrim's objectives as they are.
namespace AhShitRim::SkyrimMessages
{
    void OnDataLoaded();  // reads Skyrim's game settings the notifications are built from, in the game's language

    // From the HUD menu's message hook (any thread): true when Skyrim's HUD doesn't get it. Only while
    // a_cjHasPlayer; otherwise every message is Skyrim's.
    bool OnHudMessage(const RE::HUDData& a_data, bool a_cjHasPlayer);

    // Every frame on the main thread: what the hook caught goes to San Andreas's HUD. On taking the
    // player, the location the player is in comes up as the zone name, as San Andreas's does on a spawn.
    void Update(bool a_owned);

    // For the test kit (asr.hud, asr.notify): what the hook did with Skyrim's messages since the game started.
    struct DebugCounts
    {
        std::uint32_t zones = 0, helps = 0, hidden = 0;
        std::string   lastZone, lastHelp, lastHidden, lastRule;
    };
    DebugCounts DebugCounted();
}
