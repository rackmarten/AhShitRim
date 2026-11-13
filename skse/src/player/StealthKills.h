#pragma once

#include "../ahshitrim_protocol.h"
#include "stealth_kill.h"

// CJ's knife stealth kill (v36), Skyrim's half (the rules in protocol/cpp/stealth_kill.h, San Andreas's half in
// sa/src/cj_melee.cpp):
//  - LockOn asks TargetOf whether CJ's knife lock-on target can be killed, and CarHits flags its proxy
//    (kProxyStealthKill) so San Andreas lets him raise the knife,
//  - San Andreas's grab (kShotStealthGrab) has the hidden player play Skyrim's own sneak kill move on the victim
//    (StealthKill::kKillMoves, a paired idle: only the victim's half shows), the victim pinned where CJ grabbed it
//    and the player left to the move near CJ while it runs; where it can't (uneven ground, a seated victim, Skyrim
//    doesn't start it or never shows it, or [Weapons] StealthKillMove=0) the victim staggers and is held still
//    until the stab,
//  - the stab (kShotStealthStab) checks again (StealthKill::CheckStab) and kills it through Skyrim's own hit
//    (Shots::Kill: kill credit, crime when seen, aggro), and it slumps forward into a ragdoll.
// Main thread only.
namespace AhShitRim::StealthKills
{
    void OnDataLoaded();  // reads the ini

    // While the player plays a kill move on the victim: the move eases it into place, which isn't the game moving
    // it, and PlayerSync leaves it to the move within StealthKill::kKillMoveStray of CJ. Pushed is how far the
    // move moved it since PlayerSync last looked (for the log).
    bool HoldsPlayer(RE::PlayerCharacter* a_player);
    void Pushed(const RE::NiPoint3& a_by);

    // Why a_actor isn't on its feet for a stealth kill, empty when it is.
    std::string OffItsFeet(RE::Actor* a_actor);

    // What StealthKill::Check needs to know of a_actor, as a_player stands to it.
    StealthKill::Target TargetOf(RE::Actor* a_actor, RE::PlayerCharacter* a_player);

    // A kShotStealthGrab or kShotStealthStab shot of the current handover.
    void OnShot(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot);

    // Every frame San Andreas has the player: holds the grabbed victim still.
    void Update(RE::PlayerCharacter* a_player, float a_seconds);

    // The test kit (debug): the grabs and stabs Skyrim took so far, the last victim, where it stood at the grab and
    // whether the stab killed it, and the victim held now.
    struct Debug
    {
        int          grabs = 0;
        int          stabs = 0;
        RE::FormID   victim = 0;
        RE::NiPoint3 grabbedAt;
        bool         died = false;
        RE::FormID   held = 0;
        bool         killMove = false;    // the victim in Skyrim's kill move now
        const char*  idle = "";           // the kill move's idle at the last grab, "" for the ragdoll
        float        moveSeconds = 0.0f;  // how long the last kill move ran, once it's over
    };
    Debug DebugState();

    // Back to Skyrim's own player, or a re-anchor: lets go.
    void Clear();
}
