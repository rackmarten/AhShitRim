#pragma once

// CJ's lock-on (the rules in protocol/cpp/melee.h, San Andreas's half in sa/src/cj_melee.cpp). On foot, as San
// Andreas rates targets (someone fighting the player first, then whoever is nearest in front, in sight):
//  - the right mouse button with fists or a melee weapon locks on to a target until it is out of reach, dead or let
//    go; CarHits flags its proxy and San Andreas turns CJ to face it,
//  - a punch or a swing with nobody locked on to picks the nearest hostile in reach in front the same way, for the
//    combo.
// San Andreas's triangle marks one actor at a time over its head, coloured by its health: the melee lock's target,
// or while CJ aims a gun the enemy nearest the crosshair (an indicator only: guns are free aim). Followers, children
// and riders are never locked on to. With the knife, the lock also says whether its target can be stealth killed
// (v38, StealthKills). Main thread only.
namespace AhShitRim::LockOn
{
    struct Aim
    {
        bool         melee = false;   // fists or a melee weapon in hand
        bool         gun = false;     // a bullet gun aimed over the shoulder (San Andreas's aim camera, not a scope)
        bool         aim = false;     // the right mouse button
        bool         fire = false;    // the left one
        float        heading = 0.0f;  // CJ's facing, Skyrim radians
        std::uint8_t weapon = 0;      // San Andreas's weapon type in his hands
        bool         padLock = false;  // the pad's LT holds the aim: a gun locks on (protocol/cpp/gun_lock.h), not only marks
        int          shift = 0;        // the d-pad's left (-1) or right (1) pressed this frame, with padLock
        float        rightX = 0.0f;    // the right stick, which pushed hard a moment drops the gun lock
        float        rightY = 0.0f;
    };

    struct Target
    {
        RE::FormID id = 0;           // the actor San Andreas should turn him to, 0 for none
        bool       stealth = false;  // v38: a knife lock on someone who can be stealth killed (StealthKill::Check)
        bool       gunLock = false;  // the pad's gun lock holds someone: the d-pad shifts it, the right stick doesn't look
        double     mouseX = 0.0;     // counts that steer San Andreas's camera onto the gun lock's target this frame
        double     mouseY = 0.0;
    };

    // The pad's gun lock (DESIGN.md, Controller): LT held with a gun over the shoulder locks on to the best target and
    // steers the camera onto it (Target::mouseX/Y), the d-pad shifts it, the right stick pushed hard drops it.
    // Every frame San Andreas has the player. a_cjFeet: where CJ stands; a_aim: what he holds and presses, all
    // false when he isn't on foot or something else holds him. The actor San Andreas should turn him to (a melee
    // lock or a swing's target), and whether a knife lock on it is a stealth kill.
    Target Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, const Aim& a_aim, float a_seconds);

    // Back to Skyrim's own player, or a re-anchor.
    void Clear();

    // The hit test's pad scenario (debug): whom the pad's gun lock holds, 0 for nobody.
    RE::FormID DebugGunTarget();
}
