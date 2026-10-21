#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct ShotEvent;
}

// CJ's explosives and fire in Skyrim (protocol v15; the rules in protocol/cpp/explosives.h). San Andreas
// sends each rocket, grenade, tear gas canister and Molotov CJ launches as a Projectile, and each puff of
// the flamethrower as a flame Shot; this
//  - flies them under San Andreas's gravity, ray testing each step through Skyrim's Havok world as a bullet
//    is: a rocket and a Molotov go off where they touch anything, a grenade and tear gas bounce until their
//    fuse runs out; each kind's newest one is drawn with San Andreas's model,
//  - blows a rocket or a grenade up as a vanilla fireball explosion (FireBallExp01, which does no damage of
//    its own), hurting everyone in reach through Skyrim's own hit by Damage::ToSkyrim, throwing people
//    (AIProcess::KnockExplosion) and opening locks close by (a crime where picking them would be one),
//  - bursts a Molotov into a small fireball and burning oil on the ground (TrapOilHazard01), and sets
//    whoever it catches burning (the Flames spell, cast by the player),
//  - lets tear gas out as a gas cloud (DA13PustuleGasCloud, harmless itself) that chokes everyone in it,
//  - shoots the Flames spell's flames from the nozzle for the flamethrower and burns everyone in its cone,
//  - and hurts CJ too when he is caught in his own (Vitals::Hurt).
// Main thread only.
namespace AhShitRim::Projectiles
{
    void OnDataLoaded();  // looks up the vanilla forms

    // The ring brought a Projectile message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // A flamethrower puff (a Shot of kind kShotFlame), relative to a_anchor.
    void Flame(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_anchor);

    // Every frame San Andreas has the player, before ModelView::EndFrame: the projectiles of a_handover,
    // relative to a_anchor, a_delta seconds on.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta);

    // Back to Skyrim's own player: projectiles in flight are dropped and gas clouds cleared.
    void Clear();
}
