#pragma once

#include "input.h"

// CJ talking on his own (v24), Skyrim's half (the rules in protocol/cpp/situations.h): what only Skyrim
// sees goes to San Andreas as counters in the input, and San Andreas decides whether he speaks.
// - A kill the player makes sneaking, of someone not in combat, is a stealth kill (Shots).
// - The player's gold, read a few times a second: gained outside a shop is loot, spent in barter or
//   Ammu-Nation a buy, and the shop closing after a buy is leaving it.
// - A hostile actor in combat with the player within 20 m sets kInputHostileNear: CJ sprinting then is
//   chased.
// Main thread only.
namespace AhShitRim::Chatter
{
    // The player killed someone sneaking, unseen.
    void StealthKill();

    // San Andreas takes the player (a handover): the gold read next only seeds, nothing is hostile yet.
    void Seed();

    // Every frame San Andreas has the player, before the input is published: adds what was seen to a_input.
    void Update(RE::PlayerCharacter* a_player, Input::Builder& a_input, float a_delta);
}
