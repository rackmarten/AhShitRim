#pragma once

#include <cstdint>

class CPlayerPed;

// San Andreas's pickups in Skyrim (protocol v23; the rules in protocol/cpp/pickups.h). Skyrim keeps the pickups: where
// they float, taking one, when it comes back. This module
// - builds the model Skyrim asks for (the heart, the armour, the parachute, a weapon, the money, the jetpack: San
//   Andreas's own pickup models) as a SkinModel of kind kSkinModelWeapon and sends it, with its parts' places, as a
//   PickupEvent Model,
// - plays San Andreas's pickup sound for one CJ took (a PickupCommand Collected), puts the jetpack on him and the
//   parachute in his hands,
// - and hands Skyrim the pickups San Andreas makes itself (the jetpack CJ takes off, CTaskSimpleJetPack::DropJetPack):
//   each goes to Skyrim as a PickupEvent Dropped and comes out of San Andreas's own list, where nothing draws it.
// Game thread only.
namespace AhShitRim::CjPickups
{
    // The ring brought a PickupCommand message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Once a frame after CjWeapons::Update. a_owned: whether Skyrim has handed CJ the player.
    void Update(CPlayerPed* a_ped, bool a_owned);
}
