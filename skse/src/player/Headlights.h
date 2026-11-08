#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
    struct InputState;
}

// The vehicles' headlights and tail lights in Skyrim (protocol v36; the rules in protocol/cpp/vehicle_lights.h).
// San Andreas switches them by its own rule on Skyrim's clock and sky, which this puts in the input; each
// vehicle slot then says where its lamps are and which burn, and this lights the scene there: a point light on
// the road ahead of the headlights and a red one just behind the tail lights, none on a wreck.
// Main thread only.
namespace AhShitRim::Headlights
{
    // Skyrim's time of day, rain or snow, and whether the player is in an interior, for San Andreas's light rule.
    void Sky(Protocol::InputState& a_input);

    // Every frame San Andreas has the player, for each vehicle slot: a_pose is the vehicle as drawn (nullptr while
    // the slot holds none), a_feet its position in Skyrim's world.
    void Show(std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet);

    // Back to Skyrim's own player, or a re-anchor: every light out.
    void Clear();
}
