#pragma once

// CJ's state in Skyrim's saves (protocol v12, the record in protocol/cpp/cosave.h): whether San Andreas
// had the player, CJ's armour, the car he was in and his weapons, written to the SKSE co-save. On load the
// handover resumes by itself, the car is put back under CJ and his weapons are sent back to San Andreas.
// Later increments add skills and the wanted level to the same record.
namespace AhShitRim::SaveState
{
    // At plugin load: names the co-save and registers its callbacks.
    void Install();
}
