#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct BodyState;
}

// San Andreas's own particles for the vehicles and the jetpack in Skyrim (protocol v26; the prims and rules in
// protocol/cpp/fx_model.h): the jetpack's flames, every vehicle's exhaust smoke and nitro flames, a
// helicopter's rotor dust, and the Rhino's and the Hunter's muzzle flashes. San Andreas runs these hidden but
// draws none of them, so from what each body slot says (BodyState.fx) Skyrim lets the sprites out, moves them
// by San Andreas's numbers and draws them with San Andreas's own sprites (models/effectsPC.txd in the player's
// install) through ModelView. Main thread only.
namespace AhShitRim::Particles
{
    // Every frame San Andreas has the player, for each vehicle slot: a_pose the vehicle as drawn (nullptr while
    // the slot holds none), a_feet its position in Skyrim's world.
    void ShowVehicle(std::uint32_t a_slot, const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet, float a_delta);

    // Every frame San Andreas has the player: the gear slot as drawn (nullptr while there is none), a_feet CJ's.
    void ShowGear(const Protocol::BodyState* a_pose, const RE::NiPoint3& a_feet, float a_delta);

    // The Rhino fired its shell from a_muzzle along a_dir (a unit vector), Skyrim world units.
    void TankFire(const RE::NiPoint3& a_muzzle, const RE::NiPoint3& a_dir);

    // The vehicle CJ drives fired its gun (the Hunter's) from a_muzzle along a_dir.
    void GunFlash(const RE::NiPoint3& a_muzzle, const RE::NiPoint3& a_dir);

    // Once a frame: moves and draws the muzzle flashes' sprites.
    void Update(float a_delta);

    // Back to Skyrim's own player, or a new handover: every sprite goes.
    void Clear();
}
