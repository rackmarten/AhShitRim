#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
    struct PlayerState;
}

// Skyrim's attacks on CJ's vehicle (protocol v24; the rules in protocol/cpp/vehicle_hits.h, San Andreas's half in
// sa/src/car_hits.cpp). While CJ is in a vehicle the vehicle's shape is San Andreas's collision box, sent in its slot
// and drawn where Skyrim draws the vehicle. This
//  - counts what hits it: Skyrim's own hits on the player (TESHitEvent: blows, arrows, spells, blasts), every
//    projectile that flies into the box (an arrow that would have missed the player inside, a spell, a dragon's
//    breath), and every melee swing that reaches it (a giant at the bonnet, a wolf at the boot), each attacker's once
//    every VehicleHits::kGate,
//  - sends each as a HitVehicle effect, its damage from the table by attack and weight, for San Andreas to take off
//    the vehicle's health (smoke, fire, the blow-up) and CJ to say a line,
//  - stops arrows at the box in a closed vehicle, and tells Vitals how much of Skyrim's damage to the player CJ keeps
//    (none in a closed vehicle, half on a bike or a quad).
// Main thread only.
namespace AhShitRim::Hitboxes
{
    void OnDataLoaded();  // the hit event sink

    // CJ's vehicle as DrawVehicles draws it this frame (smoothed, its feet in Skyrim's world), nullptr when he drives none.
    void SetVehicle(const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet);

    // Every frame San Andreas has the player, after the vehicles are drawn: a_pose the player's, nullptr when stale.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState* a_pose, bool a_active);

    // Of Skyrim's own damage to the player, the share CJ keeps in the vehicle a_pose says he's in (1 on foot).
    float CjShare(const Protocol::PlayerState* a_pose);

    // Back to Skyrim's own player, or a re-anchor.
    void Clear();
}
