#pragma once

#include <cstdint>

#include "revive.h"

// Coming back (the rules in protocol/cpp/revive.h): whoever the player kills while San Andreas has it, placed
// in the editor and not hostile on sight (its base aggression, and Skyrim's hostility unless it's lawful), is noted
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

    // What a kill of a_actor by CJ would do (Revive::Why), for the test kit's respawn checks.
    const char* DryRun(RE::Actor* a_actor, RE::PlayerCharacter* a_player);
    // The test kit: whether a_id is dead and noted to come back, and whether its hour is up but it's held in view.
    bool DebugWaiting(std::uint32_t a_id);
    // The test kit: what the verdict was read from, and Skyrim's own hostility check beside it.
    struct DebugJudged
    {
        int  baseAggression = 0;  // the base's AI data bits, which the verdict reads
        int  aggression = 0;      // the actor value (Aggression), what the AI runs on
        int  reaction = 0;        // GetFactionReaction(player): 0 neutral, 1 enemy, 2 ally, 3 friend
        bool hostile = false;     // Actor::IsHostileToActor(player)
        bool lawful = false;      // has a crime faction
    };
    DebugJudged DebugJudge(RE::Actor* a_actor, RE::PlayerCharacter* a_player);
    bool DebugHeld(std::uint32_t a_id);

    void OnGameLoad();  // deaths queued belonged to the game before

    // The co-save (SaveState).
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
