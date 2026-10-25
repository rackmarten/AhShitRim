#pragma once

class CPlayerPed;
class CVector;

// CJ's vehicle against Skyrim's actors (protocol v28, the rules in protocol/cpp/run_over.h). San Andreas's
// world has no one in it, so Skyrim sends the actors near CJ's vehicle as capsules (the proxy slot), and every
// frame after San Andreas's physics step the vehicle's collision box is swept along the way it moved that
// frame against each:
// - a person, a horse or a beast takes the vehicle's push and slows it by its share (a person barely),
// - a giant, a mammoth or a dragon stops it like a wall, dents it, and puts it back where it touched,
// - a hit at kMinImpact or more goes to Skyrim as an Impact (damage by speed, the throw), each actor once a
//   second at most, and CJ says a crash line, one every 4 s at most.
// - Skyrim's attacks on the vehicle (v31, protocol/cpp/vehicle_hits.h) come as HitVehicle effects: TakeHit takes them
//   off its health (not through a proof San Andreas gives it: bulletproof for arrows, meleeproof for blows,
//   fireproof for spells, explosionproof for blasts), and CJ says a crash line, sharing the 4 s with the run-over ones.
// Game thread only.
namespace AhShitRim::CarHits
{
    // Once a frame after the simulation and before Vehicles::Update; a_active while CJ has the player and nothing else holds
    // him (Wasted, a scene). a_spawn and a_lift as for Vehicles::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, bool a_active);

    // One of Skyrim's attacks (VehicleHits::Attack a_attack) hit CJ's vehicle at a_at for a_damage of its health.
    void TakeHit(CPlayerPed* a_ped, unsigned char a_attack, float a_damage, const CVector& a_at);
}
