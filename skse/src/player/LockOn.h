#pragma once

// CJ's lock-on (the rules in protocol/cpp/melee.h, San Andreas's half in sa/src/cj_melee.cpp). On foot, as San
// Andreas rates targets (someone fighting the player first, then whoever is nearest in front, in sight):
//  - the right mouse button with fists or a melee weapon locks on to a target until it is out of reach, dead or let
//    go; CarHits flags its proxy and San Andreas turns CJ to face it,
//  - a punch or a swing with nobody locked on to picks the nearest hostile in reach in front the same way, for the
//    combo.
// San Andreas's triangle marks one actor at a time over its head, coloured by its health: the melee lock's target,
// or while CJ aims a gun the enemy nearest the crosshair (an indicator only: guns are free aim). Followers, children
// and riders are never locked on to. Main thread only.
namespace AhShitRim::LockOn
{
    struct Aim
    {
        bool  melee = false;      // fists or a melee weapon in hand
        bool  gun = false;        // a bullet gun aimed over the shoulder (San Andreas's aim camera, not a scope)
        bool  aim = false;        // the right mouse button
        bool  fire = false;       // the left one
        float heading = 0.0f;     // CJ's facing, Skyrim radians
    };

    // Every frame San Andreas has the player. a_cjFeet: where CJ stands; a_aim: what he holds and presses, all
    // false when he isn't on foot or something else holds him. The actor San Andreas should turn him to (a melee
    // lock or a swing's target), 0 for none.
    RE::FormID Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const Aim& a_aim, float a_seconds);

    // Back to Skyrim's own player, or a re-anchor.
    void Clear();
}
