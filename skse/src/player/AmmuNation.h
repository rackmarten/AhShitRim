#pragma once

#include "../ahshitrim_protocol.h"
#include "shop.h"

// Ammu-Nation (#830): while San Andreas has the player, E on the blacksmith at Warmaiden's in Whiterun
// (Adrianne or Ulfberth), or the main blacksmith of another hold capital (protocol/cpp/shop.h), opens
// San Andreas's shop menu instead of Skyrim's dialogue: weapons by slot, their ammo and body armour,
// paid in the player's gold. CJ stands still while it is open; W/S browse, E or the sprint key chooses,
// the enter/exit key goes back and leaves. A gun goes to San Andreas through Arms::Give, the armour onto
// CJ's armour (Vitals). Main thread only.
namespace AhShitRim::AmmuNation
{
    // The help box prompt for a shopkeeper CJ faces ("E: Ammu-Nation (Adrianne Avenicci)"), "" for anyone else.
    std::string PromptFor(RE::TESObjectREFR* a_ref);

    // E on a_ref: opens the shop if it's a shopkeeper who'll sell (alive, not hostile, not fighting).
    bool Open(RE::TESObjectREFR* a_ref, Input::KeyLayout a_layout);

    bool IsOpen();
    void Close();

    // Every frame San Andreas has the player: a_controls are CJ's actions this frame (the shop has them while
    // it is open, San Andreas none), a_pose San Andreas's latest pose or nullptr.
    void Update(RE::PlayerCharacter* a_player, const Shop::Controls& a_controls, float a_delta, const Protocol::PlayerState* a_pose);
}
