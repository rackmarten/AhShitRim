#include <doctest/doctest.h>

#include <string>

#include "../../cpp/hud_sounds.h"

using namespace AhShitRim::HudSounds;

TEST_CASE("only the HUD sounds play, as San Andreas's own events")
{
    CHECK(static_cast<int>(Sound::PickupWeapon) == 6);
    CHECK(static_cast<int>(Sound::PickupMoney) == 7);
    CHECK(static_cast<int>(Sound::PickupHealth) == 8);
    CHECK(static_cast<int>(Sound::PickupArmour) == 10);
    CHECK(static_cast<int>(Sound::DisplayInfo) == 32);
    CHECK(static_cast<int>(Sound::PurchaseWeapon) == 40);
    for (const auto sound : { Sound::PickupWeapon, Sound::PickupMoney, Sound::PickupHealth, Sound::PickupArmour, Sound::PickupInfo,
             Sound::DisplayInfo, Sound::PurchaseWeapon }) {
        CHECK(Plays(static_cast<std::uint8_t>(sound)));
        CHECK(std::string(NameOf(sound)) != "?");
    }
    CHECK_FALSE(Plays(0));    // AE_FRONTEND_START
    CHECK_FALSE(Plays(34));   // AE_FRONTEND_RADIO_RETUNE_START: a loop
    CHECK_FALSE(Plays(47));   // AE_FRONTEND_SCANNER_NOISE_START: a loop
    CHECK_FALSE(Plays(50));   // AE_FRONTEND_LOADING_TUNE_START: a loop
    CHECK_FALSE(Plays(52));   // past the frontend's
    CHECK_FALSE(Plays(255));
}

TEST_CASE("each pickup's own sound")
{
    using AhShitRim::Protocol::PickupKind;
    CHECK(ForPickup(PickupKind::Weapon) == Sound::PickupWeapon);
    CHECK(ForPickup(PickupKind::Money) == Sound::PickupMoney);
    CHECK(ForPickup(PickupKind::Health) == Sound::PickupHealth);
    CHECK(ForPickup(PickupKind::Armour) == Sound::PickupArmour);
    CHECK(ForPickup(PickupKind::Jetpack) == Sound::PickupInfo);
    CHECK(ForPickup(PickupKind::Parachute) == Sound::PickupWeapon);  // a weapon in San Andreas
}

TEST_CASE("the help box beeps when a message starts showing, not when the one up is shown again")
{
    CHECK(HelpBeeps(false, "", "Cheat activated"));
    CHECK(HelpBeeps(false, "Cheat activated", "Cheat activated"));  // gone already: shows again, beeps again
    CHECK_FALSE(HelpBeeps(true, "Cheat activated", "Cheat activated"));
    CHECK(HelpBeeps(true, "Cheat activated", "Cheat deactivated"));  // another one replaces it
    CHECK_FALSE(HelpBeeps(false, "", ""));                            // nothing to show
}
