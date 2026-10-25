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
//   lets it, on the gas too, and the player's motorbike yaws with left/right (protocol/cpp/vehicle.h),
//   pushed in Update after San Andreas's physics step.
// Game thread only.
namespace AhShitRim::Vehicles
{
    // A spawn request: a_model (a San Andreas vehicle model id) in front of CJ. Refused (logged) while
    // CJ is in a vehicle or when nothing in front of him will do. a_airborne (AIR + a plane's name,
    // Protocol::kSpawnAirborne): a plane comes AirSpawn::kHeight above CJ instead, facing where the camera
    // looks, gear up, engine on and fast enough to fly (protocol/cpp/air_spawn.h), with CJ warped in as its
    // pilot; anything but a plane comes as usual.
    void Spawn(CPlayerPed* a_ped, int a_model, bool a_airborne = false);

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

    // PAINT (v20): CJ's vehicle in new colours (another of its model's colour sets, or any colours for a model with
    // one), mended, at full health and put out, as a Pay 'n' Spray does; its model is built and sent again.
    void Respray(CPlayerPed* a_ped);

    // LOWRIDER (v20): San Andreas's hydraulics (its upgrade 1087) on the car CJ drives (a_on), if its handling takes
    // them, or off it. Its slot says which (kBodyHydraulics). The horn raises and lowers the car, and R3 (numpad 5)
    // with the right stick (the turret keys) bounces it, as in San Andreas.
    void SetHydraulics(CPlayerPed* a_ped, bool a_on);

    // CJ out of his vehicle at once (a scene takes him), the vehicle left where it is.
    void GetOut(CPlayerPed* a_ped);

    // CJ was put back from a broken position (PositionGuard): out of his vehicle, and every vehicle
    // whose own position is broken goes. One he can't be got out of goes to a_at with him, stopped.
    void Recover(CPlayerPed* a_ped, const CVector& a_at);

    // The floating origin moved by a_shift (metres): the vehicles move back with everything else.
    void Shift(const CVector& a_shift);

    // Once a frame after the simulation and before CJ's pose is read: poses the vehicles, builds the
    // models of new ones, writes the vehicle slots, and seats CJ in his. a_spawn and a_lift as for
    // CjBody::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame);

    // The vehicle CJ is in, or nullptr.
    CVehicle* Driven(CPlayerPed* a_ped);

    // Parked vehicles (v36, protocol/cpp/parked.h): once a frame after Update. Puts a spot's vehicle out as a mission
    // vehicle in a free slot once CJ is within Parked::kSpawnRadius of it in its world (a_world, InputState.world) and
    // the ground there is in, drops one nobody used once he is past Parked::kFarRadius, and tells the spots their
    // vehicle was taken when CJ gets in, it is wrecked or it is gone. a_anchor: InputState.anchor; a_spawn and a_lift
    // as for Update. Nothing happens unless a_active (San Andreas has CJ, the ground under him is in).
    void UpdateParked(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, const float (&a_anchor)[3], std::uint8_t a_world, bool a_active);
}
