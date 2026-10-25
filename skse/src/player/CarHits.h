#pragma once

#include <cstdint>

// CJ's vehicle against Skyrim's actors (protocol v28; the rules in protocol/cpp/run_over.h, San Andreas's half
// in sa/src/car_hits.cpp). This
//  - sends the nearest actors around the player while CJ is in a vehicle (the proxy slot): each a capsule sized
//    by its race and scale, its weight, and whether it is heavy (giants, mammoths, dragons stop a lighter vehicle), the
//    player's passengers, children and riders (their mount stands in for them) left out,
//  - and on foot (#895, protocol/cpp/melee.h) the nearest within a few metres for CJ to bump into, followers left
//    out, with his lock-on target (LockOn) flagged,
//  - plays the hits San Andreas sends back (Impact), as late as Skyrim draws the vehicle: damage by speed through
//    Skyrim's own hit with the player as the aggressor (Shots::Hurt: kill credit, crime, aggro, the race's blood;
//    no crime for anyone but a person, Police::NoteHurt) and a throw along the push (AIProcess::KnockExplosion),
//    never for a heavy one that stopped the vehicle,
//  - and makes the people and animals in the vehicle's way dive aside while CJ sounds the horn (0.1.1, PerkeleRim's:
//    RunOver::InHornPath, HornDodge): knocked down lightly and their ragdoll thrown out of its line.
// Main thread only.
namespace AhShitRim::CarHits
{
    // The ring brought an Impact message: kept until it is due.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Every frame San Andreas has the player: a_inVehicle while CJ drives or rides something, a_onFoot while he walks
    // (not in a scene, nor dying or arrested); a_target the actor he locks on to on foot, 0 for none.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, bool a_inVehicle, bool a_onFoot, RE::FormID a_target);

    // Every frame San Andreas has the player: a_held while CJ drives and the horn key is down (no menu has the input).
    // a_vehicleAt is where his vehicle is, a_velocity how fast it goes (units a second), a_heading where its nose
    // points (Skyrim radians). On the press and every second while it's held, the way is cleared (RunOver::HornPulse).
    void Horn(RE::PlayerCharacter* a_player, bool a_held, const RE::NiPoint3& a_vehicleAt, const RE::NiPoint3& a_velocity, float a_heading);

    // Back to Skyrim's own player, or a re-anchor: no proxies, hits still waiting dropped.
    void Clear();
}
