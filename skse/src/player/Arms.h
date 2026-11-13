#pragma once

#include <cstdint>

#include "../ahshitrim_protocol.h"
#include "body.h"

// CJ's weapons on Skyrim's side (protocol v12). San Andreas keeps them; this
//  - draws the weapon model in CJ's hand (two for a twin pistol) from the weapon slots, smoothed like CJ,
//    and the flash of each shot (MuzzleFlash),
//  - keeps the loadout and (v13) the weapon skills in the co-save: the pose carries San Andreas's weapons, the save holds them, and a
//    loaded save (or a new game) sends them back as a SetLoadout command once the link is up,
//  - gives the debug key's weapon sets ([Debug] WeaponKey in AhShitRim.ini, on until cheats exist).
// Main thread only.
namespace AhShitRim::Cosave
{
    struct Arms;
}


namespace AhShitRim::Arms
{
    void OnDataLoaded();  // reads the ini

    // Every frame, owned or not: sends a loadout that waits for the link and follows San Andreas's weapons for the save.
    void Update();

    bool DebugKeyEnabled();

    // A weapon CJ bought or earned (#830), a_rounds with it (or only the rounds, for one he has): given on top of
    // San Andreas's loadout once it has the one it was sent, or kept in the saved loadout while the link is down.
    void Give(std::uint8_t a_type, std::uint32_t a_rounds);

    // CJ's weapons as Skyrim last saw them (San Andreas's, or the save's until it has them).
    const Protocol::WeaponEntry (&Loadout())[Protocol::kWeaponSlotCount];
    void DebugKey();  // gives the next set or weapon

    // Every frame San Andreas has the player: the weapons in CJ's hands where San Andreas has them, relative
    // to a_anchor, and the flash of the shots a_pose counted; a_hide draws none (CJ looks through a scope).
    // a_delta is the frame's seconds. With a_body (CJ's skeleton) each is drawn at its moment a_bodyTime, as the
    // vehicles are (Body::Follow): on its own clock a gun is a frame off his hand, beside a moving bike.
    void Draw(const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState& a_pose, double a_nowMs, float a_delta, bool a_hide,
        const Body::Interpolator* a_body = nullptr, double a_bodyTime = 0.0);

    // Back to Skyrim's own player: nothing of CJ's drawn.
    void Hide();

    // The test kit (debug): the weapon drawn in hand a_hand this frame (0 right, 1 left), as posed, and where its
    // origin was put in Skyrim's world. False when none was.
    bool DebugWeapon(std::uint32_t a_hand, Protocol::BodyState& a_pose, RE::NiPoint3& a_feet);

    // The test kit (debug): CJ takes the weapon of type a_type he carries in his hands (his loadout sent again, holding
    // its slot). False when he hasn't it.
    bool DebugHoldWeapon(std::uint8_t a_type);

    // The co-save.
    void Snapshot(Cosave::Arms& a_out);
    void Restore(const Cosave::Arms& a_arms);
    void Revert();
}
