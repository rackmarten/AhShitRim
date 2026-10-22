#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct ShotEvent;
}

// CJ's explosives and fire in Skyrim (protocol v15, satchels and the heat seeker v17; the rules in
// protocol/cpp/explosives.h). San Andreas sends each rocket, grenade, tear gas canister, Molotov and satchel
// charge CJ launches as a Projectile, the detonator as one too, and each puff of the flamethrower as a flame
// Shot; this
//  - flies them under San Andreas's gravity, ray testing each step through Skyrim's Havok world as a bullet
//    is: a rocket and a Molotov go off where they touch anything, a grenade and tear gas bounce until their
//    fuse runs out, a satchel sticks to what it lands on (an actor's nearest bone, which it then follows);
//    each is drawn with San Andreas's model,
//  - locks the heat seeker on to the dragon, horse, giant, mammoth or centurion CJ aims it at, with San
//    Andreas's marker in the HUD, and steers its rocket to it,
//  - blows every satchel up on the detonator,
//  - blows a rocket or a grenade up as a vanilla fireball explosion (FireBallExp01, which does no damage of
//    its own), hurting everyone in reach through Skyrim's own hit by Damage::ToSkyrim, throwing people
//    (AIProcess::KnockExplosion) and opening locks close by (a crime where picking them would be one),
//  - bursts a Molotov into a small fireball and burning oil on the ground (the oil trap's fire, drawn alone),
//    and sets whoever it catches or stands in it burning (the Flames spell, cast by the player),
//  - lets tear gas out as a gas cloud (DA13PustuleGasCloud, harmless itself) that chokes everyone in it,
//  - shoots the Flames spell's flames from the nozzle for the flamethrower and burns everyone in its cone,
//  - and hurts CJ too when he is caught in his own (Vitals::Hurt),
//  - tells San Andreas what happened (Effect messages) so its own sounds play: its explosion, a rocket's
//    roar until it goes off, its fire's crackle where a Molotov burns (where CJ catches fire, as he does
//    caught in the burst), and shows Skyrim's flames on CJ while he burns there.
// Main thread only.
namespace AhShitRim::Projectiles
{
    void OnDataLoaded();  // looks up the vanilla forms

    // The ring brought a Projectile message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // A flamethrower puff (a Shot of kind kShotFlame), relative to a_anchor.
    void Flame(RE::PlayerCharacter* a_player, const Protocol::ShotEvent& a_shot, const RE::NiPoint3& a_anchor);

    // Every frame San Andreas has the player, before ModelView::EndFrame: the projectiles of a_handover,
    // relative to a_anchor, a_delta seconds on. a_cjBurning: CJ's feet while San Andreas has him on fire.
    // a_cjWeapon: the weapon in his hands, a_aiming whether he aims it.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta, const RE::NiPoint3* a_cjBurning,
        std::uint8_t a_cjWeapon, bool a_aiming);

    // A vehicle blew up in San Andreas at a_at (v18): the fireball, twice a rocket's, hurting and throwing
    // Skyrim's people as a rocket does, further out, and CJ when a_hurtCj. San Andreas plays its sound.
    void VehicleBlast(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, bool a_hurtCj);

    // Back to Skyrim's own player: projectiles in flight and satchels are dropped and gas clouds cleared.
    void Clear();
}
