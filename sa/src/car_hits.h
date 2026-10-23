#pragma once

class CPlayerPed;
class CVector;

// CJ's vehicle against Skyrim's actors (protocol v23, the rules in protocol/cpp/run_over.h). San Andreas's
// world has no one in it, so Skyrim sends the actors near CJ's vehicle as capsules (the proxy slot), and every
// frame after San Andreas's physics step the vehicle's collision box is swept along the way it moved that
// frame against each:
// - a person, a horse or a beast takes the vehicle's push and slows it by its share (a person barely),
// - a giant, a mammoth or a dragon stops it like a wall, dents it, and puts it back where it touched,
// - a hit at kMinImpact or more goes to Skyrim as an Impact (damage by speed, the throw), each actor once a
//   second at most, and CJ says a crash line, one every 4 s at most.
// Game thread only.
namespace AhShitRim::CarHits
{
    // Once a frame after the simulation and before Vehicles::Update; a_active while CJ has the player and nothing else holds
    // him (Wasted, a scene). a_spawn and a_lift as for Vehicles::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, bool a_active);
}
