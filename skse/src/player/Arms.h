#pragma once

#include <cstdint>

#include "../ahshitrim_protocol.h"

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
    // a_delta is the frame's seconds.
    void Draw(const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState& a_pose, double a_nowMs, float a_delta, bool a_hide);

    // Back to Skyrim's own player: nothing of CJ's drawn.
    void Hide();

    // The co-save.
    void Snapshot(Cosave::Arms& a_out);
    void Restore(const Cosave::Arms& a_arms);
    void Revert();
}
