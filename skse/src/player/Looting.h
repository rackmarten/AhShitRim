#pragma once

// Looting without menus (#830, the rules in protocol/cpp/loot.h). While San Andreas has the player:
// - walking over a dead actor takes its gold and quest items,
// - E on a container (unlocked) or a body does the same, with no menu; an owned container's gold is stolen,
//   and Skyrim's steal alarm goes off for it,
// - the gold counts up in San Andreas's money display, and each quest item shows in the help box,
// - Skyrim's inventory, barter and container menus close as they open (F10 hands the player back for them).
// Main thread only.
namespace AhShitRim::Looting
{
    // Every frame San Andreas has the player: a_feet are CJ's, a_onFoot while he walks (not in a vehicle, a
    // scene, Wasted or a menu).
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, bool a_onFoot, float a_delta);

    // E on a_ref: loots it if it's a body or an unlocked container; false leaves it to Skyrim's activation.
    bool Activate(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref);

    // A Skyrim menu opening while San Andreas has the player: the inventory, barter and container menus are
    // closed again.
    void OnMenuOpening(std::string_view a_menu);
}
