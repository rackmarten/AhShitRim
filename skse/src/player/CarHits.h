#pragma once

#include <cstdint>

// CJ's vehicle against Skyrim's actors (protocol v23; the rules in protocol/cpp/run_over.h, San Andreas's half
// in sa/src/car_hits.cpp). This
//  - sends the nearest actors around the player while CJ is in a vehicle (the proxy slot): each a capsule sized
//    by its race and scale, its weight, and whether it is heavy (giants, mammoths, dragons stop a car), the
//    player's passengers, children and riders (their mount stands in for them) left out,
//  - plays the hits San Andreas sends back (Impact), as late as Skyrim draws the vehicle: damage by speed through
//    Skyrim's own hit with the player as the aggressor (Shots::Hurt: kill credit, crime, aggro, the race's blood)
//    and a throw along the push (AIProcess::KnockExplosion), never for the heavy ones.
// Main thread only.
namespace AhShitRim::CarHits
{
    // The ring brought an Impact message: kept until it is due.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Every frame San Andreas has the player: a_inVehicle while CJ drives or rides something.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, bool a_inVehicle);

    // Back to Skyrim's own player, or a re-anchor: no proxies, hits still waiting dropped.
    void Clear();
}
