#pragma once

// CJ's melee lock-on (the rules in protocol/cpp/melee.h, San Andreas's half in sa/src/cj_melee.cpp). While he
// holds the right mouse button with fists or a melee weapon on foot, this picks the actor he locks on to as San
// Andreas rates them (someone fighting the player first, then whoever is nearest in front, in sight), keeps it
// until it is out of reach, dead or let go, and marks it with San Andreas's lock-on marker. CarHits flags its
// proxy, and San Andreas turns CJ to face it. Followers, children and riders are never locked on to.
// Main thread only.
namespace AhShitRim::LockOn
{
    // Every frame San Andreas has the player. a_cjFeet: where CJ stands; a_heading: his facing (Skyrim radians);
    // a_held: the button held with fists or a melee weapon, on foot and nothing else holding him. The target's
    // form id, 0 for none.
    RE::FormID Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_cjFeet, float a_heading, bool a_held);

    // Back to Skyrim's own player, or a re-anchor.
    void Clear();
}
