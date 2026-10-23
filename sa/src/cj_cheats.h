#pragma once

#include <cstdint>

#include "cheats.h"

class CPlayerPed;

// San Andreas's own cheats, typed in Skyrim (protocol v20; which ones and how typing finds them in
// protocol/cpp/cheats.h). Skyrim sends a Cheat command for each of San Andreas's toggles and one-shot cheats it
// has a part in; it is found in San Andreas's own tables by its key (CCheat::m_aCheatHashKeys) and applied as
// CCheat::ApplyCheat would: one with a function runs it (HESOYAM mends CJ's vehicle, the body cheats rebuild
// him, which sends his model again), one without is a toggle, set as Skyrim says. The weapon sets, the vehicles
// and the gear come as weapon commands and spawn requests instead. Game thread only.
namespace AhShitRim::CjCheats
{
    // A Cheat message from Skyrim, applied in the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Once a frame, after the ring is read.
    void Update(CPlayerPed* a_ped);

    // Whether San Andreas's toggle a_cheat is on (flying cars: no extra air control; infinite ammo: shots
    // counted from the bullets, since the rounds don't go down).
    bool Active(Cheats::SaCheat a_cheat);
}
