#pragma once

#include "balance.h"

// How hard CJ hits and is hit (the rules in protocol/cpp/balance.h): AhShitRim.ini's [Balance] CJDamage,
// DamageTaken and FollowDifficulty, read once when the game's data is loaded, and Skyrim's multiplier for the player's
// damage (fDiffMultHPByPC*) at the player's difficulty, read each time. Main thread only.
namespace AhShitRim::Balance
{
    void OnDataLoaded();

    const Settings& Current();

    // What the damage of CJ's weapon a_type (Shots) is multiplied by.
    float DealtFactor(RE::PlayerCharacter* a_player, std::uint8_t a_type);

    // What a hit of Skyrim's people on the player is multiplied by, a_share being the stars' share for guards.
    float TakenFactor(float a_share);
}
