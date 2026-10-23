#pragma once

#include <cstdint>

// Coming back (the rules in protocol/cpp/revive.h): a unique NPC the player kills while San Andreas has it
// is noted with Skyrim's game time, and an in-game hour later, once its body's cell and its editor spot are
// both out of CJ's sight, it is resurrected with its inventory reset and moved to its editor spot. So CJ's
// rampages don't cost the mod its blacksmiths (Ammu-Nation) or the main quest its people, NOMERCY or not.
// The dead waiting are kept in the co-save, in a record of their own (Revive::kRecordType).
namespace AhShitRim::Respawns
{
    void OnDataLoaded();  // listens to Skyrim's deaths

    // Every frame on the main thread: notes the deaths the sink queued (a_cjHasPlayer: San Andreas has the
    // player now) and, once a second, brings back whoever is ready.
    void Update(bool a_cjHasPlayer, float a_delta);

    void OnGameLoad();  // deaths queued belonged to the game before

    // The co-save (SaveState).
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
