#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
}

// A damaged vehicle in Skyrim (protocol v18; the rules in protocol/cpp/vehicle_damage.h). San Andreas damages
// its vehicles but draws none of their particles, so from each vehicle slot's health and flags this
//  - puts vanilla white smoke at its engine as the health drops under 650, and from 450 thick black smoke of
//    our own instead (protocol/cpp/smoke_model.h: Skyrim's smoke can't be turned black), drawn through
//    ModelView, which goes on through the fire,
//  - a big vanilla fire there once the engine burns (under 250: San Andreas blows it up 5 s later),
//  - Projectiles::VehicleBlast where it blows up (the fireball, Skyrim's people hurt and thrown; San Andreas
//    plays the sound and hurts CJ),
//  - and fire on the wreck while San Andreas's own fire burns it.
// Main thread only.
namespace AhShitRim::VehicleFx
{
    // Every frame San Andreas has the player, for each vehicle slot: a_pose is the vehicle as drawn (nullptr
    // while the slot holds none), a_feet its position in Skyrim's world, a_handover the handover it belongs to.
    void Show(RE::PlayerCharacter* a_player, std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet,
        std::uint32_t a_handover, float a_delta);

    // Back to Skyrim's own player, or a new handover: forgets what it saw.
    void Clear();
}
