#pragma once

#include <cstdint>

#include "input.h"
#include "pose.h"
#include "weapons.h"

class CPlayerPed;
class CVector;

// CJ's weapons for Skyrim (protocol v12). San Andreas keeps them: its slots, ammo, the weapon in his
// hands, its switching, aiming and firing, driven by the mouse through the pad (the wheel switches,
// the right button aims, the left one fires). This module
// - turns the mouse wheel into the shoulder presses San Andreas switches weapons on,
// - obeys the WeaponCommands Skyrim sends (a loaded save's loadout, the debug key's sets),
// - makes sure the models of the weapons he carries are loaded,
// - sends the model of the weapon in his hand (and the same again for the second hand of a twin
//   pistol) as a SkinModel of kind kSkinModelWeapon, its parts as bones, and every frame places the
//   weapon on his hand bone as San Andreas does when it draws a ped and writes the parts' transforms
//   to the weapon slots,
// - fills the pose with the loadout, the held weapon, the shots fired, where they leave and the
//   camera mode for the scopes.
// Game thread only.
namespace AhShitRim::CjWeapons
{
    // The ring brought a WeaponCommand message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Every time the input is applied, owned or not: the wheel's steps as the shoulder presses for this
    // frame. a_active when CJ is on foot with the player (otherwise the steps are only followed, the
    // radio takes them in a vehicle).
    Weapons::WheelPulse::Presses Wheel(const Protocol::InputState& a_input, bool a_active);

    // Once a frame after the simulation and after CjBody::Update posed him: applies the queued
    // commands, keeps the weapon in his hand drawn and writes the weapon slots. a_spawn and a_lift as
    // for CjBody::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame);

    // Into the pose published this frame: the loadout, the held weapon and what Skyrim draws for it.
    void Fill(CPlayerPed* a_ped, Pose::SanAndreasPose& a_pose);
}
