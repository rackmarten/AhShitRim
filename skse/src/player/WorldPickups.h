#pragma once

#include <cstdint>
#include <vector>

namespace AhShitRim::Protocol
{
    struct PlayerState;
}

// San Andreas's pickups in Skyrim's world (protocol v27; the rules and the spots in protocol/cpp/pickups.h). While
// San Andreas has the player, this
//  - draws each pickup within 100 m spinning at its spot with San Andreas's own model (the heart, the armour, the
//    parachute, a weapon, the money), set down on whatever is under the spot, asking San Andreas for each model the
//    first time one is near,
//  - gives CJ what he walks into (San Andreas's rule) and he wants: full health, full armour, the gold, a weapon
//    with San Andreas's street ammo, the parachute in his hands, the jetpack on; San Andreas plays its sound,
//  - brings a taken one back as San Andreas does (30 s, armour and the heavy weapons 6 minutes, once CJ is 10 m
//    away), the one-offs (the gold) never: the co-save keeps those,
//  - draws them at twice San Andreas's size,
//  - leaves a weapon where it is when CJ holds another in its slot (its rounds into his shotgun, SMG or rifle), offers
//    the swap in the help box and makes it on E, as San Andreas does,
//  - puts a heart in front of CJ, where the camera sees it, when he is down to a quarter of his health (at most
//    once every 3 minutes),
//  - and keeps the pickups San Andreas drops (the jetpack CJ takes off) where they fell, until he takes them again
//    or leaves the place.
// Main thread only.
namespace AhShitRim::WorldPickups
{
    // The ring brought a PickupEvent message (owned or not).
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Every frame San Andreas has the player, before ModelView::EndFrame. a_pose is null while it isn't valid; a_held: CJ
    // takes nothing (Wasted, or being re-anchored).
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, float a_delta,
        const Protocol::PlayerState* a_pose, bool a_held);

    // E while San Andreas has the player: true when CJ stands on a weapon he can swap the one in its slot for (the help
    // box offered it); the swap is made in the next Update, and Skyrim's own activation must not run.
    bool Activate();

    // Back to Skyrim's own player: the heart and San Andreas's dropped ones are gone.
    void Clear();

    // The co-save: the one-off pickups taken (Pickups::Spot ids).
    std::vector<std::uint16_t> Taken();
    void                       Restore(const std::vector<std::uint16_t>& a_taken);
    void                       Revert();
}
