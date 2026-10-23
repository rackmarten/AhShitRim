#pragma once

#include <cstdint>

class CVector;

// What CJ's weapons hit is Skyrim's to say (protocol v13). San Andreas's world has no one in it, so this
// module catches each shot as San Andreas works it out and sends it to Skyrim as a Shot message:
// - every bullet CWeapon::FireInstantHit fires (each line it tests, its spread in it, hit or not; a
//   shotgun's shot is one line, which Skyrim fans out into pellets), and the first-person rifles'
//   (CWeapon::DoBulletImpact), from its start along its line, with the weapon's range,
// - the sniper rifle's shot from San Andreas's camera (CWeapon::FireSniper, which fires a moving bullet),
// - each puff of the flamethrower (CWeapon::FireAreaEffect, which San Andreas then skips for him: no fire on
//   its ground), from the nozzle along where he aims, with its range,
// - each melee blow at its hit frame (CTaskSimpleFight::FightStrike): where it lands, its radius and its
//   damage from melee.dat, counted so Skyrim lands each blow once, and the chainsaw again and again.
// Origins go relative to where the handover put CJ's feet, in Skyrim units. Game thread only.
namespace AhShitRim::CjShots
{
    // Once the other plugins (SilentPatch) have patched the game.
    void Install();

    // Every frame before San Andreas fires CJ's gun (CjWeapons::Update, after any floating origin move):
    // where this handover put his feet (San Andreas metres) and the handover, for the shots until the next
    // call (a blow lands while the world is processed, before it).
    void SetFrame(const CVector& a_spawnFeet, unsigned a_handover);

    // The frame SetFrame last gave (false before the first): CjProjectiles sends from it too.
    bool Frame(CVector& a_spawnFeet, unsigned& a_handover);

    // Counts the bullets (and shotgun shots) sent, wrapping: with infinite ammo (v20) the pose counts shots by it.
    std::uint32_t Bullets();
}
