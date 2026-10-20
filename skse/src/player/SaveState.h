#pragma once

// CJ's state in Skyrim's saves (protocol v11, the record in protocol/cpp/cosave.h): whether San Andreas
// had the player, CJ's armour and the car he was in, written to the SKSE co-save. On load the
// handover resumes by itself and the car is put back under CJ. Later increments add weapons, skills
// and the wanted level to the same record.
namespace AhShitRim::SaveState
{
    // At plugin load: names the co-save and registers its callbacks.
    void Install();
}
