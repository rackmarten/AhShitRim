#pragma once

#include <cstdint>
#include <functional>
#include <string>

// San Andreas's cheat codes typed while CJ has the player (protocol v20; the table, the typing and the M
// key's rule in protocol/cpp/cheats.h, NOMERCY's bookkeeping in mercy.h).
// - Every letter key pressed then goes into the cheat string; a code finished shows San Andreas's "Cheat
//   activated" (or "Cheat deactivated" for a toggle typed again) in the help box.
// - San Andreas's toggles and one-shot cheats go to it as Cheat commands, the weapon sets as GiveSet, the
//   vehicles, the jetpack and the parachute as spawn requests (through a_spawn). Its toggles are sent again
//   when the link comes back (a San Andreas that restarted has them all off), and a loaded save or a new game
//   turns them all off, as San Andreas's own cheats don't outlive a load.
// - Skyrim's halves: HESOYAM fills the player's health and CJ's armour and gives 250,000 gold, BAGUVIX keeps
//   the player from taking any damage (Vitals) while CJ has it, SZCMAWO is Wasted, and NOMERCY (ours)
//   clears every essential flag but the player's, puts back exactly those when typed again, and is kept in
//   the co-save in a record of its own (Mercy::kRecordType).
// - Ours besides (v20): MINIGUN gives the minigun, PAINT resprays and mends CJ's vehicle, LOWRIDER puts hydraulics on the car he
//   is in, or takes them off it, and every vehicle's model name from San Andreas's vehicles.ide (SANCHEZ, HYDRA) spawns it.
// Main thread only.
namespace AhShitRim::CheatCodes
{
    // What a cheat needs of the rest of the plugin.
    struct World
    {
        std::function<std::string(std::uint16_t)> spawn;    // asks for a model in front of CJ; returns why it can't (the help box says so), or nothing
        std::function<std::uint16_t()>            vehicle;  // the model of the vehicle CJ is in, 0 on foot
        std::function<bool()>                     hydraulics;  // whether it has hydraulics (its slot's kBodyHydraulics)
    };

    // Reads the vehicle names from San Andreas's data/vehicles.ide (the install in AhShitRim.ini); call on kDataLoaded.
    void OnDataLoaded();

    // A key went down while CJ has the player and no menu has the input.
    void Key(std::uint32_t a_dik, const World& a_world);

    // Every frame, owned or not: toggles sent again once the link is back up, NOMERCY's flags kept cleared.
    void Update();

    // A game loads or a new one starts: San Andreas's toggles and BAGUVIX off.
    void OnGameLoad();

    // NOMERCY's co-save record.
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
