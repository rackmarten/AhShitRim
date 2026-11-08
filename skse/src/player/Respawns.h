#pragma once

#include <cstdint>

#include "revive.h"

// Coming back (the rules in protocol/cpp/revive.h): whoever the player kills while San Andreas has it, placed
// in the editor and not hostile on sight (its base aggression and faction reaction to the player), is noted
// with Skyrim's game time, unless a running quest's shown objective points at them (its kill target). If a
// quest holding them sets a stage or completes an objective in the next 2 s, the death moved it on and they
// stay dead. Otherwise, an in-game hour later, once neither the body nor the editor spot is in view (the
// camera's frustum, then a line-of-sight ray), they are resurrected with their inventory reset and moved to
// their editor spot. Essential ones (NOMERCY's included) always come back. The dead waiting are kept in the
// co-save, in a record of their own (Revive::kRecordType).
namespace AhShitRim::Respawns
{
    void OnDataLoaded();  // listens to Skyrim's deaths and quest stages

    // Every frame on the main thread: notes the deaths the sink queued (a_cjHasPlayer: San Andreas has the
    // player now), watches their quests and, once a second, brings back whoever is ready.
    void Update(bool a_cjHasPlayer, float a_delta);

    // What a kill of a_actor by CJ would do (Revive::Why), for the hit test's respawn run.
    const char* DryRun(RE::Actor* a_actor, RE::PlayerCharacter* a_player);

    void OnGameLoad();  // deaths queued belonged to the game before

    // The co-save (SaveState).
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
