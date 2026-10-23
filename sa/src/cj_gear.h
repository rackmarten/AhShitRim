#pragma once

#include <cstdint>

class CPlayerPed;
class CVector;

// CJ's gear on foot for Skyrim (protocol v19): the jetpack and the parachute, both asked for by model id on the
// dev path (VehicleKinds::kJetpack, kParachute), as San Andreas's own cheats give them.
// - The jetpack is San Andreas's own (CTaskSimpleJetPack: its controls, thrust and animations). Hidden it is never
//   drawn (RenderJetPack), so its clump is put on his back here every frame as RenderJetPack puts it, the jet
//   balls turned with the thrust, and its parts go to Skyrim in the gear slot.
// - The parachute is a weapon to San Andreas; the skydive and the canopy are its mission script's, which never
//   runs, so they follow protocol/cpp/parachute.h: CJ skydives with parachute.ifp's animations, the fire key opens
//   the chute, whose skinned canopy (model PARACHUTE) plays its _O twins and goes to Skyrim in the gear slot, and
//   when he lands the parachute is used up.
// Game thread only.
namespace AhShitRim::CjGear
{
    // A dev spawn request for a_model: the jetpack (370) on (on foot), or the parachute (371) given, in his hands on foot
    // or kept for when he bails out of his vehicle.
    void Give(CPlayerPed* a_ped, std::uint16_t a_model);

    // Once a frame after CjBody::Update posed him (and before CjWeapons::Update): the skydive and the canopy, the
    // jetpack placed, and the gear slot written. a_free: CJ is his own (not Wasted, not following a Skyrim scene);
    // a skydive ends otherwise. a_spawn and a_lift as for CjBody::Update.
    void Update(CPlayerPed* a_ped, bool a_free, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame);

    // Right before CJ's animations update (CjBody's UpdateAnim hook): during a skydive his parachute.ifp animation takes
    // all of his pose, San Andreas's own fall none.
    void HoldCjAnim(CPlayerPed* a_ped);

    // A new handover or a re-anchor: a skydive in progress ends where it is (the jetpack stays on).
    void Reset(CPlayerPed* a_ped);
}
