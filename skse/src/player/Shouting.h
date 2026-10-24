#pragma once

#include <cstdint>

// CJ's Thu'um (the rules in protocol/cpp/thuum.h): while San Andreas has the player, the Dragonborn's
// voice is kept out of Skyrim's shouts, and each one the player lets out goes to San Andreas with the
// shout and the words charged, where CJ says one of his picked lines for it. The shout's effect,
// charging and cooldown stay Skyrim's.
namespace AhShitRim::Shouting
{
    void OnDataLoaded();  // logs any line of the Dragonborn's voice the player still says

    // Every frame on the main thread: mutes or brings back the Dragonborn's voice as San Andreas takes
    // or gives back the player (a_cjHasPlayer), and notes the shout being charged and how far. With
    // WANRLTW on (San Andreas's infinite ammo) the shout's cooldown is gone as soon as it's seen.
    void Update(RE::PlayerCharacter* a_player, bool a_cjHasPlayer);

    // Whether the player is charging or letting out a shout this frame (a dash may follow).
    bool InProgress(RE::PlayerCharacter* a_player);

    // The shout just released, as the lineKind of CJ's line for it (Thuum::Encode); call it once per release, with
    // the cooldown it started (the words when Skyrim didn't say how far it was charged).
    std::uint8_t Released(RE::PlayerCharacter* a_player, float a_recovery);
}
