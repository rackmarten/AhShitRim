#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "input.h"
#include "pose.h"
#include "weapons.h"

class CPlayerPed;
class CVector;
struct RpAtomic;
struct RpClump;
struct RwObject;

// CJ's weapons for Skyrim (protocol v12). San Andreas keeps them: its slots, ammo, the weapon in his
// hands, its switching, aiming and firing, driven by the mouse through the pad (the wheel switches,
// the right button aims, the left one fires). This module
// - turns the mouse wheel into the shoulder presses San Andreas switches weapons on,
// - obeys the WeaponCommands Skyrim sends (a loaded save's loadout and skills, the debug key's sets, the hits
//   CJ landed in Skyrim, which raise his weapon skills),
// - makes sure the models of the weapons he carries are loaded,
// - sends the model of the weapon in his hand (and the same again for the second hand of a twin
//   pistol) as a SkinModel of kind kSkinModelWeapon, its parts as bones, and every frame places the
//   weapon on his hand bone as San Andreas does when it draws a ped and writes the parts' transforms
//   to the weapon slots,
// - fills the pose with the loadout, the held weapon, the shots fired, where they leave, the camera
//   mode for the scopes and the weapon skills.
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

    // A weapon model's instance (a_object: its clump or atomic, a thrown weapon's or a rocket's) as a SkinModel
    // of kind kSkinModelWeapon for Skyrim, its drawn parts as bones in a_parts' order, built as the weapon in
    // his hand is (CjProjectiles draws the projectiles with it). Empty when it has nothing to draw.
    // a_name names it for the logs and Skyrim (v19: the jetpack's), the weapon's model name when empty.
    std::vector<std::uint8_t> BuildModel(RwObject* a_object, std::uint8_t a_type, std::vector<RpAtomic*>& a_parts, std::string_view a_name = {});

    // A clump's drawn parts in the order BuildModel makes them bones (v19: a new instance of a model already sent).
    std::vector<RpAtomic*> PartsOf(RpClump* a_clump);
}
