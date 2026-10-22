#pragma once

#include <cstdint>

class CPlayerPed;
class CVector;
class CVehicle;

// San Andreas's vehicles for Skyrim to draw (protocol v6).
// - Spawning: a spawn request from Skyrim puts the vehicle across CJ's path on the first flat, clear
//   spot in front of him (protocol/cpp/vehicle.h), as a mission vehicle so San Andreas never takes it
//   away. At most Protocol::kVehicleMax at once: another removes the oldest one CJ isn't in.
// - The model: every drawn part (atomic) of the vehicle with its textures, in the vehicle's own
//   colours, one bone per part, sent once over the ring (ModelSender).
// - The pose: nothing is drawn while hidden, so each vehicle's PreRender (which turns and steers the
//   wheels, a bike's forks and handlebars) runs here every frame, a bike gets its lean, and every
//   part's frame goes to Skyrim in the vehicle's slot.
// - Air control: the player's car steers kAirTurnGain times as hard in the air as San Andreas
//   lets it, on the gas too, and the player's bike yaws with left/right (protocol/cpp/vehicle.h),
//   through hooks on CAutomobile::ProcessAI and CBike::ProcessAI.
// Game thread only.
namespace AhShitRim::Vehicles
{
    void Install();  // at ASI load

    // A spawn request: a_model (a San Andreas vehicle model id) in front of CJ. Refused (logged) while
    // CJ is in a vehicle or when nothing in front of him will do.
    void Spawn(CPlayerPed* a_ped, int a_model);

    // A new handover or a re-anchor (a load door, fast travel): CJ out of any vehicle and every vehicle
    // removed, so fast travel in a car leaves the car behind.
    void Clear(CPlayerPed* a_ped);

    // A vehicle from a loaded save (protocol v11): model and colours as they were, a_offset metres from
    // a_cj (where this handover put CJ), facing a_heading (San Andreas's), with CJ warped in as its
    // driver. Call once the ground there is in. Returns whether it is done (also when it can't be done,
    // which is logged).
    struct Restoration
    {
        int           model = 0;
        unsigned char colours[4] = {};
        float         offset[3] = {};
        float         heading = 0.0f;
    };
    bool Restore(CPlayerPed* a_ped, const CVector& a_cj, const Restoration& a_car);

    // CJ out of his vehicle at once (a scene takes him), the vehicle left where it is.
    void GetOut(CPlayerPed* a_ped);

    // The floating origin moved by a_shift (metres): the vehicles move back with everything else.
    void Shift(const CVector& a_shift);

    // Once a frame after the simulation and before CJ's pose is read: poses the vehicles, builds the
    // models of new ones, writes the vehicle slots, and seats CJ in his. a_spawn and a_lift as for
    // CjBody::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame);

    // The vehicle CJ is in, or nullptr.
    CVehicle* Driven(CPlayerPed* a_ped);
}
