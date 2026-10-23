// San Andreas's HUD sounds (v24), the game-free half. Skyrim decides when one plays and sends it (a FrontendSound
// message); the hidden San Andreas plays it through its own audio engine (CAudioEngine::ReportFrontendAudioEvent),
// with the sounds of the player's own install. Which event San Andreas plays where comes from gta-reversed and
// gta_sa.exe:
// - the help box beeps (AE_FRONTEND_DISPLAY_INFO, CHud::DrawHelpText at 0x58B81F) when a new message starts
//   showing. That is all a cheat does: CCheat shows "Cheat activated" or "Cheat deactivated" and the box beeps.
//   San Andreas's camera beeps the same when V cycles its view (CCamera::CamControl at 0x52B716);
// - picking up a weapon, money, health or armour (CPickup::Update), a jetpack (CPickups::GivePlayerGoodiesWithPickUpMI),
//   and taking gold off a body or out of a chest by walking over it (#830's looting, as San Andreas's money pickup);
// - buying at Ammu-Nation (AE_FRONTEND_PURCHASE_WEAPON, the shop scripts' purchase sound).
// Header-only and free of game types, so the protocol tests cover it; skse/src/hud/SaHud.cpp and the features
// send it, sa/src/audio.cpp plays it (a pickup's from sa/src/cj_pickups.cpp, with the PickupCommand it comes in).

#pragma once

#include <cstdint>
#include <string_view>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::HudSounds
{
    // San Andreas's eAudioEvents of the sounds Skyrim sends. Only these play: the other AE_FRONTEND_* events include
    // loops (the radio retune, the loading tune, the scanner's noise) that would play on until stopped.
    enum class Sound : std::uint8_t
    {
        PickupWeapon = 6,     // AE_FRONTEND_PICKUP_WEAPON
        PickupMoney = 7,      // AE_FRONTEND_PICKUP_MONEY
        PickupHealth = 8,     // AE_FRONTEND_PICKUP_HEALTH
        PickupArmour = 10,    // AE_FRONTEND_PICKUP_BODY_ARMOUR
        PickupInfo = 11,      // AE_FRONTEND_PICKUP_INFO: an info pickup, the jetpack
        DisplayInfo = 32,     // AE_FRONTEND_DISPLAY_INFO: the help box's beep
        PurchaseWeapon = 40,  // AE_FRONTEND_PURCHASE_WEAPON
    };

    // Whether a_event is one San Andreas may play from a message (anything else is dropped).
    constexpr bool Plays(std::uint8_t a_event)
    {
        switch (static_cast<Sound>(a_event)) {
        case Sound::PickupWeapon:
        case Sound::PickupMoney:
        case Sound::PickupHealth:
        case Sound::PickupArmour:
        case Sound::PickupInfo:
        case Sound::DisplayInfo:
        case Sound::PurchaseWeapon:
            return true;
        }
        return false;
    }

    constexpr const char* NameOf(Sound a_sound)
    {
        switch (a_sound) {
        case Sound::PickupWeapon: return "weapon pickup";
        case Sound::PickupMoney: return "money pickup";
        case Sound::PickupHealth: return "health pickup";
        case Sound::PickupArmour: return "armour pickup";
        case Sound::PickupInfo: return "info pickup";
        case Sound::DisplayInfo: return "help box";
        case Sound::PurchaseWeapon: return "purchase";
        }
        return "?";
    }

    // A pickup CJ took (CPickup::Update): a weapon's (the parachute is one), money's, health's and armour's own; the
    // jetpack's is the info pickup's (CPickups::GivePlayerGoodiesWithPickUpMI).
    constexpr Sound ForPickup(Protocol::PickupKind a_kind)
    {
        switch (a_kind) {
        case Protocol::PickupKind::Health: return Sound::PickupHealth;
        case Protocol::PickupKind::Armour: return Sound::PickupArmour;
        case Protocol::PickupKind::Money: return Sound::PickupMoney;
        case Protocol::PickupKind::Jetpack: return Sound::PickupInfo;
        default: return Sound::PickupWeapon;
        }
    }

    // The help box beeps when a message starts showing, as San Andreas's does: not when the one already up is shown
    // again (CHud::SetHelpMessage with the same text keeps it, without starting it over). a_up is whether a message
    // is up now, a_current its text.
    constexpr bool HelpBeeps(bool a_up, std::string_view a_current, std::string_view a_next)
    {
        return !a_next.empty() && (!a_up || a_current != a_next);
    }
}
