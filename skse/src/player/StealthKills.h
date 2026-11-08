#pragma once

#include "../ahshitrim_protocol.h"
#include "stealth_kill.h"

// CJ's knife stealth kill (v36), Skyrim's half (the rules in protocol/cpp/stealth_kill.h, San Andreas's half in
// sa/src/cj_melee.cpp):
//  - LockOn asks TargetOf whether CJ's knife lock-on target can be killed, and CarHits flags its proxy
//    (kProxyStealthKill) so San Andreas lets him raise the knife,
//  - San Andreas's grab (kShotStealthGrab) staggers the victim and holds it still until the stab,
//  - the stab (kShotStealthStab) checks again (StealthKill::CheckStab) and kills it through Skyrim's own hit
//    (Shots::Kill: kill credit, crime when seen, aggro), and it slumps forward into a ragdoll.
// Main thread only.
namespace AhShitRim::StealthKills
{
    // What StealthKill::Check needs to know of a_actor, as a_player stands to it.
    StealthKill::Target TargetOf(RE::Actor* a_actor, RE::PlayerCharacter* a_player);

    // A kShotStealthGrab or kShotStealthStab shot of the current handover.
    void OnShot(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot);

    // Every frame San Andreas has the player: holds the grabbed victim still.
    void Update(RE::PlayerCharacter* a_player, float a_seconds);

    // Back to Skyrim's own player, or a re-anchor: lets go.
    void Clear();
}
