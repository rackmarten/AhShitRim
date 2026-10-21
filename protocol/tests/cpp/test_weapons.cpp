#include <ostream>  // before doctest, so it can print string_view
#include <string_view>

#include <doctest/doctest.h>

#include "../../cpp/weapons.h"

using namespace AhShitRim;
using namespace AhShitRim::Weapons;

TEST_CASE("the weapons CJ can carry")
{
    CHECK_FALSE(Carried(Unarmed));
    CHECK(Carried(BrassKnuckle));
    CHECK(Carried(Molotov));
    CHECK_FALSE(Carried(19));  // the rocket projectiles
    CHECK_FALSE(Carried(21));
    CHECK(Carried(Pistol));
    CHECK(Carried(Parachute));
    CHECK_FALSE(Carried(47));
    CHECK_FALSE(Carried(255));
}

TEST_CASE("melee weapons, gifts and fists count no ammo, the rest do")
{
    CHECK(IsMelee(Unarmed));
    CHECK(IsMelee(Chainsaw));
    CHECK(IsMelee(Cane));
    CHECK_FALSE(IsMelee(Grenade));
    CHECK_FALSE(CountsAmmo(Katana));
    CHECK_FALSE(CountsAmmo(Detonator));
    CHECK_FALSE(CountsAmmo(Parachute));
    CHECK(CountsAmmo(Grenade));
    CHECK(CountsAmmo(Pistol));
    CHECK(CountsAmmo(Camera));
}

TEST_CASE("only guns flash")
{
    CHECK(Flashes(Pistol));
    CHECK(Flashes(Tec9));
    CHECK(Flashes(SniperRifle));
    CHECK(Flashes(Minigun));
    CHECK_FALSE(Flashes(Flamethrower));
    CHECK_FALSE(Flashes(RocketLauncher));
    CHECK_FALSE(Flashes(Katana));
    CHECK_FALSE(Flashes(Spraycan));
}

TEST_CASE("every carried weapon has a model and an icon, fists the hud's fist")
{
    for (int type = 1; type <= kLastCarried; ++type) {
        if (Carried(static_cast<std::uint8_t>(type))) {
            CHECK_MESSAGE(!ModelName(static_cast<std::uint8_t>(type)).empty(), "type ", type);
        } else {
            CHECK(ModelName(static_cast<std::uint8_t>(type)).empty());
        }
    }
    CHECK(IconName(Pistol) == "colt45icon");
    CHECK(IconName(SniperRifle) == "snipericon");
    CHECK(IconName(Unarmed) == "fist");
    CHECK(CrosshairName(SniperRifle) == "SNIPERcrosshair");
    CHECK(CrosshairName(Pistol).empty());
}

TEST_CASE("San Andreas's camera modes pick the scope")
{
    CHECK(ScopeOf(kCamSniper) == Scope::Sniper);
    CHECK(ScopeOf(kCamSniperRunabout) == Scope::Sniper);
    CHECK(ScopeOf(kCamRocketLauncher) == Scope::Rocket);
    CHECK(ScopeOf(kCamRocketLauncherHs) == Scope::Rocket);
    CHECK(ScopeOf(kCamM16FirstPerson) == Scope::Rifle);
    CHECK(ScopeOf(kCamCamera) == Scope::Camera);
    CHECK(ScopeOf(kCamAimWeapon) == Scope::Aim);
    CHECK(ScopeOf(0x04) == Scope::None);  // following the ped
}

TEST_CASE("a wheel step is one press, a gap and the next")
{
    WheelPulse wheel;
    CHECK_FALSE(wheel.Step(7, 3, true).next);  // the first values only seed

    auto p = wheel.Step(8, 3, true);
    CHECK(p.next);
    CHECK_FALSE(p.prev);
    p = wheel.Step(8, 3, true);  // released
    CHECK_FALSE(p.next);
    CHECK_FALSE(p.prev);

    p = wheel.Step(8, 4, true);
    CHECK(p.prev);
    CHECK_FALSE(p.next);
}

TEST_CASE("steps that come in together go out one after another")
{
    WheelPulse wheel;
    wheel.Step(0, 0, true);
    int presses = 0;
    auto p = wheel.Step(3, 0, true);  // three steps in one frame
    presses += p.next;
    for (int frame = 0; frame < 8; ++frame) {
        p = wheel.Step(3, 0, true);
        presses += p.next;
        CHECK_FALSE(p.prev);
    }
    CHECK(presses == 3);
}

TEST_CASE("the wheel follows the counters while it is not for the weapons")
{
    WheelPulse wheel;
    wheel.Step(0, 0, true);
    CHECK_FALSE(wheel.Step(5, 2, false).next);  // the radio's steps in a vehicle
    CHECK_FALSE(wheel.Step(5, 2, true).next);   // and none of them switches a weapon once CJ is out
    CHECK(wheel.Step(6, 2, true).next);
}

TEST_CASE("the wheel counters wrap")
{
    WheelPulse wheel;
    wheel.Step(0xFFFF, 0, true);
    CHECK(wheel.Step(0, 0, true).next);
}

TEST_CASE("a counter that jumps is a restart, not a flood of steps")
{
    WheelPulse wheel;
    wheel.Step(50, 50, true);
    int presses = 0;
    presses += wheel.Step(0, 0, true).next;   // Skyrim started over
    for (int frame = 0; frame < 20; ++frame) {
        const auto p = wheel.Step(0, 0, true);
        presses += p.next + p.prev;
    }
    CHECK(presses == 0);
    CHECK(wheel.Step(1, 0, true).next);  // and it works from there
}

TEST_CASE("shots come from the rounds that went")
{
    CHECK(ShotsBetween(100, 100) == 0);
    CHECK(ShotsBetween(100, 99) == 1);
    CHECK(ShotsBetween(100, 94) == 6);
    CHECK(ShotsBetween(100, 130) == 0);  // picked up ammo
    CHECK(ShotsBetween(5000, 0) == 32);  // a swap or a cheat isn't a volley
}

TEST_CASE("the muzzle is the fire offset in the weapon's frame, relative to his feet, in units")
{
    Body::RwRows identity{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 10, 20, 31 } };
    const float  offset[3] = { 0.0f, 0.5f, 0.0f };  // half a metre along the weapon's forward
    float        out[3];
    Muzzle(identity, offset, { 10.0f, 20.0f, 30.0f }, out);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(0.5f * Pose::kUnitsPerMeter));
    CHECK(out[2] == doctest::Approx(1.0f * Pose::kUnitsPerMeter));

    // Turned a quarter round: its forward is San Andreas's left (-X).
    Body::RwRows turned{ { 0, 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 10, 20, 30 } };
    Muzzle(turned, offset, { 10.0f, 20.0f, 30.0f }, out);
    CHECK(out[0] == doctest::Approx(-0.5f * Pose::kUnitsPerMeter));
    CHECK(out[1] == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("a loadout is made one San Andreas can hold")
{
    Loadout loadout{};
    loadout[2] = { Pistol, 0, 30, 100 };      // fine
    loadout[3] = { 19, 7, 5, 5 };            // a projectile type, and a stray reserved byte
    loadout[4] = { Ak47, 0, 500, 200 };      // a clip bigger than the ammo
    loadout[5] = { Minigun, 0, 10, 1000000 };  // more ammo than San Andreas holds
    CHECK(Sanitise(loadout) == 3);
    CHECK(loadout[2].type == Pistol);
    CHECK(loadout[2].reserved == 0);
    CHECK(loadout[3].type == Unarmed);
    CHECK(loadout[3].ammo == 0);
    CHECK(loadout[4].clip == 200);
    CHECK(loadout[5].ammo == kAmmoMax);
    CHECK_FALSE(Empty(loadout));
    CHECK(Holds(loadout, 2));
    CHECK_FALSE(Holds(loadout, 3));
    CHECK_FALSE(Holds(loadout, 13));
    CHECK(Sanitise(loadout) == 0);

    Loadout none{};
    CHECK(Empty(none));
}

TEST_CASE("the ammo line is San Andreas's")
{
    CHECK(AmmoStyleOf(Pistol, 17) == Protocol::kAmmoClip);
    CHECK(AmmoStyleOf(Shotgun, 1) == Protocol::kAmmoTotal);  // one-shot weapons show the total
    CHECK(AmmoStyleOf(Spraycan, 1000) == Protocol::kAmmoTotal);
    CHECK(AmmoStyleOf(Flamethrower, 500) == Protocol::kAmmoFlame);
    CHECK(AmmoStyleOf(Minigun, 500) == Protocol::kAmmoClip);

    CHECK(AmmoText(100, 17, Protocol::kAmmoClip) == "83-17");   // a clipped pistol: the rest, then the clip
    CHECK(AmmoText(50, 50, Protocol::kAmmoTotal) == "50");
    CHECK(AmmoText(520, 500, Protocol::kAmmoFlame) == "2-50");  // the flame in tenths
    CHECK(AmmoText(99999, 0, Protocol::kAmmoClip) == "9999-0"); // capped at four digits
}

TEST_CASE("the debug key walks the sets, the gear and every melee weapon, and wraps")
{
    CHECK(kDebugStepCount == 16);
    auto c = DebugCommand(0);
    CHECK(c.kind == static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveSet));
    CHECK(c.argument == 1);
    c = DebugCommand(3);
    CHECK(c.argument == 4);
    c = DebugCommand(4);
    CHECK(c.kind == static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveWeapon));
    CHECK(c.argument == GolfClub);
    CHECK(c.entries[0].ammo == 1);
    CHECK(DebugCommand(kDebugStepCount).argument == 1);  // round again

    // Every step is a set San Andreas has or a weapon it can hold, and every melee weapon comes up by itself
    // or in a set (1: knuckles and the bat, 2: the knife, 3: the chainsaw).
    bool melee[16] = {};
    melee[BrassKnuckle] = melee[BaseballBat] = melee[Knife] = true;
    for (std::size_t i = 0; i < kDebugStepCount; ++i) {
        const auto step = DebugCommand(i);
        if (step.kind == static_cast<std::uint32_t>(Protocol::WeaponCommandKind::GiveSet)) {
            CHECK((step.argument >= 1 && step.argument <= 4));
        } else {
            CHECK(Carried(static_cast<std::uint8_t>(step.argument)));
            if (step.argument < 16) {
                melee[step.argument] = true;
            }
        }
    }
    for (int type = BrassKnuckle; type <= Cane; ++type) {
        CHECK_MESSAGE(melee[type], "melee weapon ", type);
    }
}

TEST_CASE("the aim looks where the camera does: level is 0 and looking up is negative")
{
    CHECK(AimLookPitch(0.0f) == doctest::Approx(0.0f));
    CHECK(AimLookPitch(0.2f) < 0.0f);
    CHECK(AimLookPitch(-1.0f) == doctest::Approx(Pose::kPi / 2));
    CHECK(AimLookPitch(2.0f) == doctest::Approx(-Pose::kPi / 2));  // a front that isn't a unit vector is clamped
    CHECK(AimLookPitch(std::nanf("")) == 0.0f);
}
