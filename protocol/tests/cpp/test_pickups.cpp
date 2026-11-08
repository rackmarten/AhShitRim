#include <cmath>
#include <ostream>
#include <map>
#include <set>

#include <doctest/doctest.h>

#include "../../cpp/pickups.h"

using namespace AhShitRim;
using namespace AhShitRim::Pickups;

namespace
{
    constexpr float M = Pose::kUnitsPerMeter;

    const Spot& Find(std::uint16_t a_id)
    {
        const auto* spot = SpotById(a_id);
        REQUIRE(spot != nullptr);
        return *spot;
    }
}

TEST_CASE("walking into one takes it, as San Andreas's CPickup::Update does")
{
    const Vec3 feet{ 1000.0f, 2000.0f, 300.0f };
    const Vec3 pickup{ 1000.0f, 2000.0f, 300.0f + kFloat };
    CHECK(Touches(feet, pickup));
    CHECK(Touches(feet, { pickup.x + 1.3f * M, pickup.y, pickup.z }));
    CHECK_FALSE(Touches(feet, { pickup.x + 1.4f * M, pickup.y, pickup.z }));
    CHECK_FALSE(Touches(feet, { pickup.x + 1.0f * M, pickup.y + 1.0f * M, pickup.z }));  // 1.41 m across
    // Up or down: within 2 m of his middle, a metre over his feet.
    CHECK(Touches(feet, { pickup.x, pickup.y, feet.z + 2.9f * M }));
    CHECK_FALSE(Touches(feet, { pickup.x, pickup.y, feet.z + 3.1f * M }));
    CHECK(Touches(feet, { pickup.x, pickup.y, feet.z - 0.9f * M }));
    CHECK_FALSE(Touches(feet, { pickup.x, pickup.y, feet.z - 1.1f * M }));
}

TEST_CASE("what CJ won't take")
{
    Cj cj;
    cj.health = 50.0f;
    cj.healthMax = 100.0f;
    cj.armour = 40.0f;
    CHECK(Wants(Kind::Health, cj));
    CHECK(Wants(Kind::Armour, cj));
    CHECK(Wants(Kind::Jetpack, cj));
    CHECK(Wants(Kind::Weapon, cj));
    CHECK(Wants(Kind::Parachute, cj));
    CHECK(Wants(Kind::Money, cj));
    CHECK_FALSE(Wants(Kind::None, cj));

    auto full = cj;
    full.health = 100.0f;
    full.armour = 100.0f;
    full.jetpack = true;
    CHECK_FALSE(Wants(Kind::Health, full));
    CHECK_FALSE(Wants(Kind::Armour, full));
    CHECK_FALSE(Wants(Kind::Jetpack, full));
    CHECK(Wants(Kind::Weapon, full));

    auto driving = cj;
    driving.inVehicle = true;
    CHECK_FALSE(Wants(Kind::Health, driving));
    CHECK_FALSE(Wants(Kind::Weapon, driving));
}

TEST_CASE("a weapon off the street comes with San Andreas's ammo for it")
{
    CHECK(StreetAmmo(Weapons::Pistol) == 30);
    CHECK(StreetAmmo(Weapons::Shotgun) == 15);
    CHECK(StreetAmmo(Weapons::Ak47) == 80);
    CHECK(StreetAmmo(Weapons::SniperRifle) == 10);
    CHECK(StreetAmmo(Weapons::RocketLauncher) == 4);
    CHECK(StreetAmmo(Weapons::Minigun) == 500);
    CHECK(StreetAmmo(Weapons::Molotov) == 8);
    CHECK(StreetAmmo(Weapons::Parachute) == 1);
    CHECK(StreetAmmo(Weapons::Katana) == 1);
    CHECK(StreetAmmo(200) == 1);
}

TEST_CASE("a street pickup is back 30 s after it was taken, once CJ is 10 m off")
{
    const auto& pistol = Find(60);
    REQUIRE(pistol.respawn == Respawn::Street);
    Board board;
    CHECK(board.Shows(pistol.id));
    board.Take(pistol, 100.0);
    CHECK_FALSE(board.Shows(pistol.id));
    CHECK_FALSE(board.Tick(pistol, 129.0, 50.0f * M));  // too soon
    CHECK_FALSE(board.Tick(pistol, 131.0, 5.0f * M));   // he is standing on it
    CHECK_FALSE(board.Shows(pistol.id));
    CHECK(board.Tick(pistol, 131.0, 11.0f * M));
    CHECK(board.Shows(pistol.id));
    CHECK(board.Tick(pistol, 132.0, 0.0f));  // one that is out stays out
}

TEST_CASE("armour and the heavy weapons wait 6 minutes, a one-off for good")
{
    const auto& armour = Find(20);
    REQUIRE(armour.respawn == Respawn::Slow);
    Board board;
    board.Take(armour, 0.0);
    CHECK_FALSE(board.Tick(armour, 359.0, 100.0f * M));
    CHECK(board.Tick(armour, 361.0, 100.0f * M));

    const auto& minigun = Find(81);
    REQUIRE(minigun.respawn == Respawn::Slow);
    board.Take(minigun, 0.0);
    CHECK(board.Tick(minigun, 361.0, 100.0f * M));

    const auto& gold = Find(82);
    REQUIRE(gold.respawn == Respawn::Once);
    board.Take(gold, 0.0);
    CHECK_FALSE(board.Tick(gold, 1e9, 1e6f));
    CHECK_FALSE(board.Shows(gold.id));
}

TEST_CASE("the co-save keeps the one-offs taken, and only those")
{
    Board board;
    board.Take(Find(83), 10.0);
    board.Take(Find(82), 20.0);
    board.Take(Find(81), 25.0);  // the minigun: it comes back by itself
    board.Take(Find(1), 30.0);   // a parachute: it comes back by itself
    board.Take(Find(20), 40.0);  // armour too
    CHECK(board.Once() == std::vector<std::uint16_t>{ 82, 83 });

    Board loaded;
    loaded.Take(Find(60), 0.0);
    loaded.Restore({ 81, 82, 83, 1, 9999 });  // the minigun (a one-off before), a street spot and an id no spot has any more are left out
    CHECK(loaded.Shows(81));
    CHECK_FALSE(loaded.Shows(83));
    CHECK_FALSE(loaded.Shows(82));
    CHECK(loaded.Shows(1));
    CHECK(loaded.Shows(60));  // what was taken before the load is back
    CHECK(loaded.Once() == std::vector<std::uint16_t>{ 82, 83 });
}

TEST_CASE("a heart turns up under a quarter of CJ's health, at most once every 3 minutes")
{
    HeartRule rule;
    CHECK_FALSE(rule.Due(30.0f, 100.0f, 0.0, false));
    CHECK(rule.Due(24.0f, 100.0f, 0.0, false));
    CHECK_FALSE(rule.Due(24.0f, 100.0f, 0.0, true));  // one is out already
    CHECK_FALSE(rule.Due(0.0f, 100.0f, 0.0, false));  // dying: Wasted, not a heart
    CHECK_FALSE(rule.Due(10.0f, 0.0f, 0.0, false));
    rule.Spawned(10.0);
    CHECK_FALSE(rule.Due(10.0f, 100.0f, 100.0, false));
    CHECK_FALSE(rule.Due(10.0f, 100.0f, 189.0, false));
    CHECK(rule.Due(10.0f, 100.0f, 190.0, false));
    rule.Reset();
    CHECK(rule.Due(10.0f, 100.0f, 11.0, false));
}

TEST_CASE("the heart is tried in front of CJ first")
{
    const Vec3 feet{ 0.0f, 0.0f, 50.0f };
    const auto north = HeartPlaces(feet, 0.0f);
    REQUIRE(north.size() >= 5);
    CHECK(north[0].x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(north[0].y == doctest::Approx(6.0f * M));
    CHECK(north[0].z == 50.0f);
    CHECK(north[1].x > 0.0f);  // a little to his right
    CHECK(north[2].x < 0.0f);
    const auto east = HeartPlaces(feet, 1.5707964f);
    CHECK(east[0].x == doctest::Approx(6.0f * M));
    CHECK(std::abs(east[0].y) < 1.0f);
    for (const auto& p : north) {
        const float d = std::hypot(p.x, p.y);
        CHECK(d >= 3.0f * M - 1.0f);
        CHECK(d <= 8.0f * M + 1.0f);
    }
}

TEST_CASE("pickups spin a turn every 2 s, all together")
{
    CHECK(SpinAt(0.0) == doctest::Approx(0.0f));
    CHECK(SpinAt(0.5) == doctest::Approx(1.5707964f));
    CHECK(SpinAt(2.5) == doctest::Approx(1.5707964f));
    CHECK(SpinAt(1e6 + 1.0) == doctest::Approx(3.1415927f).epsilon(1e-3));
}

TEST_CASE("the spot table")
{
    std::set<std::uint16_t> ids;
    int                     parachutes = 0, armour = 0, hearts = 0, weapons = 0, heavy = 0;
    std::map<Hold, int>     armourIn, weaponsIn;
    for (const auto& spot : kSpots) {
        CHECK(ids.insert(spot.id).second);  // ids are unique
        // Nothing on top of another: at least a metre and a half apart.
        for (const auto& other : kSpots) {
            if (&other != &spot && other.world == spot.world) {
                CHECK(std::hypot(other.at.x - spot.at.x, other.at.y - spot.at.y, other.at.z - spot.at.z) > 1.5f * M);
            }
        }
        if (spot.kind == Kind::Armour) {
            armourIn[spot.hold]++;
        }
        if (spot.kind == Kind::Weapon) {
            weaponsIn[spot.hold]++;
            const auto slot = Shop::SlotOf(spot.weapon);
            if (slot == 7) {  // the heavy slot: rare, slow to come back
                heavy++;
                CHECK(spot.respawn == Respawn::Slow);
            }
        }
        hearts += spot.kind == Kind::Health;
        weapons += spot.kind == Kind::Weapon;
        CHECK(std::isfinite(spot.at.x + spot.at.y + spot.at.z));
        CHECK(spot.kind != Kind::None);
        CHECK(spot.kind != Kind::Jetpack);  // only San Andreas drops one
        CHECK(!spot.name.empty());
        CHECK(RayFrom(spot) > spot.at.z);
        CHECK(RayTo(spot) < spot.at.z);
        if (spot.kind == Kind::Weapon) {
            CHECK(Weapons::Carried(spot.weapon));
            CHECK(!Weapons::ModelName(spot.weapon).empty());
        }
        if (spot.kind == Kind::Weapon) {
            CHECK(spot.respawn != Respawn::Once);  // a weapon always comes back, as in San Andreas
        }
        if (spot.kind == Kind::Money) {
            CHECK(spot.amount > 0);
            CHECK(spot.respawn == Respawn::Once);
        }
        parachutes += spot.kind == Kind::Parachute;
        armour += spot.kind == Kind::Armour;
    }
    CHECK(parachutes >= 5);
    CHECK(armour >= 30);  // three or more per hold
    CHECK(hearts >= 10);
    CHECK(std::size(kSpots) >= 90);  // twice 0.1's 45
    // Every hold has three or more armour and a weapon or two.
    CHECK(armourIn.size() == static_cast<std::size_t>(kHolds));
    CHECK(weaponsIn.size() == static_cast<std::size_t>(kHolds));
    for (const auto& [hold, n] : armourIn) {
        CHECK(n >= 3);
    }
    // Weapons: handguns and SMGs most, the heavy ones few.
    CHECK(weapons >= 30);
    int small = 0;
    for (const auto& spot : kSpots) {
        if (spot.kind == Kind::Weapon) {
            const auto slot = Shop::SlotOf(spot.weapon);
            small += slot == 2 || slot == 4;  // handguns, SMGs
        }
    }
    CHECK(small * 2 > weapons);
    CHECK(heavy >= 2);
    CHECK(heavy * 6 < weapons);
    // On the road from Helgen to Riverwood: armour and a weapon at the Guardian Stones and at Helgen.
    int roadArmour = 0, roadWeapons = 0;
    for (const auto& spot : kSpots) {
        if (spot.name == "Helgen" || spot.name == "the Guardian Stones" || spot.name == "Embershard Mine") {
            roadArmour += spot.kind == Kind::Armour;
            roadWeapons += spot.kind == Kind::Weapon;
        }
    }
    CHECK(roadArmour >= 2);
    CHECK(roadWeapons >= 3);
    // The ones the 0.1 list asked for by name.
    CHECK(Find(1).name == "the Throat of the World");
    CHECK(Find(1).kind == Kind::Parachute);
    CHECK(Find(1).at.z > 36000.0f);  // the summit, above High Hrothgar
    CHECK(Find(2).kind == Kind::Parachute);
    CHECK(Find(2).at.z < Find(1).at.z);
    CHECK(Find(3).kind == Kind::Parachute);
    CHECK(Find(4).world == World::Whiterun);
    CHECK(Find(4).top);
    CHECK(WorldForm(World::Tamriel) == 0x3C);
    CHECK(WorldForm(World::Whiterun) == 0x1A26F);
    CHECK(SpotById(9999) == nullptr);
}

TEST_CASE("San Andreas's models for each kind")
{
    CHECK(ModelName(Kind::Health) == "health");
    CHECK(ModelName(Kind::Armour) == "bodyarmour");
    CHECK(ModelName(Kind::Parachute) == Weapons::ModelName(Weapons::Parachute));
    CHECK(ModelName(Kind::Jetpack) == "jetpack");
    CHECK(ModelName(Kind::Weapon).empty());
    CHECK(KindOfModel("jetpack") == Kind::Jetpack);
    CHECK(KindOfModel("bodyarmour") == Kind::Armour);
    CHECK(KindOfModel("cellphone") == Kind::None);
    CHECK(ModelKey(Kind::Weapon, Weapons::Ak47) != ModelKey(Kind::Weapon, Weapons::M4));
    CHECK(ModelKey(Kind::Health, 7) == ModelKey(Kind::Health, 0));
    CHECK(ModelKey(Kind::Health, 0) != ModelKey(Kind::Armour, 0));
}

TEST_CASE("walking into a weapon: what CJ holds in its slot decides, as in San Andreas")
{
    using Weapons::Type;
    const auto held = [](std::uint8_t a_type, std::uint32_t a_ammo) {
        Protocol::WeaponEntry e{};
        e.type = a_type;
        e.ammo = a_ammo;
        return e;
    };
    // A loaded Desert Eagle and a pistol lying there: he keeps his gun, and E offers the swap.
    CHECK(OnWalk(held(Weapons::DesertEagle, 70), Weapons::Pistol, false) == Walk::Offer);
    // The same weapon, or nothing in the slot: taken, the rounds on top.
    CHECK(OnWalk(held(Weapons::Pistol, 17), Weapons::Pistol, false) == Walk::Take);
    CHECK(OnWalk(held(Weapons::Unarmed, 0), Weapons::Pistol, false) == Walk::Take);
    // A handgun with no rounds left is swapped by walking over.
    CHECK(OnWalk(held(Weapons::DesertEagle, 0), Weapons::Pistol, false) == Walk::Take);
    // Shotguns, SMGs and rifles: the pickup's rounds go into his gun first, then the offer; empty, only the offer.
    CHECK(OnWalk(held(Weapons::Mp5, 90), Weapons::MicroUzi, false) == Walk::Ammo);
    CHECK(OnWalk(held(Weapons::Mp5, 90), Weapons::MicroUzi, true) == Walk::Offer);
    CHECK(OnWalk(held(Weapons::Mp5, 0), Weapons::MicroUzi, false) == Walk::Ammo);
    CHECK(OnWalk(held(Weapons::M4, 50), Weapons::Ak47, false) == Walk::Ammo);
    CHECK(OnWalk(held(Weapons::Shotgun, 8), Weapons::Spas12, false) == Walk::Ammo);
    // Heavy weapons and the rest: the plain offer.
    CHECK(OnWalk(held(Weapons::RocketLauncher, 3), Weapons::Minigun, false) == Walk::Offer);
    CHECK(OnWalk(held(Weapons::SniperRifle, 10), Weapons::CountryRifle, false) == Walk::Offer);
}

TEST_CASE("a swap keeps the old gun's rounds only in the shotgun, SMG and rifle slots")
{
    Protocol::WeaponEntry deagle{};
    deagle.type = Weapons::DesertEagle;
    deagle.ammo = 70;
    const auto pistol = AfterSwap(deagle, Weapons::Pistol, StreetAmmo(Weapons::Pistol));
    CHECK(pistol.type == Weapons::Pistol);
    CHECK(pistol.ammo == 30);  // the Desert Eagle's rounds go with it

    Protocol::WeaponEntry mp5{};
    mp5.type = Weapons::Mp5;
    mp5.ammo = 90;
    const auto uzi = AfterSwap(mp5, Weapons::MicroUzi, 0);  // its rounds went into the MP5 when he walked in
    CHECK(uzi.type == Weapons::MicroUzi);
    CHECK(uzi.ammo == 90);
    CHECK(AfterSwap(mp5, Weapons::MicroUzi, 60).ammo == 150);
    mp5.ammo = Weapons::kAmmoMax;
    CHECK(AfterSwap(mp5, Weapons::MicroUzi, 60).ammo == Weapons::kAmmoMax);
    CHECK(SharesRounds(Shop::SlotOf(Weapons::Spas12)));
    CHECK(SharesRounds(Shop::SlotOf(Weapons::Ak47)));
    CHECK_FALSE(SharesRounds(Shop::SlotOf(Weapons::Pistol)));
    CHECK_FALSE(SharesRounds(Shop::SlotOf(Weapons::SniperRifle)));
}

TEST_CASE("out of rounds: no gun of his has any left, or he has none")
{
    Weapons::Loadout loadout{};
    CHECK(OutOfRounds(loadout));  // fists only
    loadout[1] = { Weapons::Katana, 0, 0, 1 };
    loadout[8] = { Weapons::Grenade, 0, 0, 4 };
    CHECK(OutOfRounds(loadout));  // a katana and grenades are no guns
    loadout[2] = { Weapons::Pistol, 0, 0, 0 };
    loadout[4] = { Weapons::Mp5, 0, 0, 0 };
    CHECK(OutOfRounds(loadout));  // the guns he has are empty
    loadout[4].ammo = 30;
    CHECK_FALSE(OutOfRounds(loadout));
    loadout[4].ammo = 0;
    loadout[7] = { Weapons::Minigun, 0, 0, 500 };
    CHECK_FALSE(OutOfRounds(loadout));
    CHECK(IsGun(Weapons::Pistol));
    CHECK(IsGun(Weapons::SniperRifle));
    CHECK(IsGun(Weapons::RocketLauncher));
    CHECK_FALSE(IsGun(Weapons::Katana));
    CHECK_FALSE(IsGun(Weapons::Grenade));
    CHECK_FALSE(IsGun(Weapons::Camera));
    CHECK_FALSE(IsGun(Weapons::Parachute));
}

TEST_CASE("a pistol turns up 2 s after he ran dry, one at a time and without a cooldown, only while he is CJ")
{
    DryRule rule;
    CHECK_FALSE(rule.Due(true, true, false, 10.0));  // just ran dry
    CHECK_FALSE(rule.Due(true, true, false, 11.9));
    CHECK(rule.Due(true, true, false, 12.0));
    rule.Spawned();
    CHECK_FALSE(rule.Due(true, true, true, 13.0));  // one is out
    CHECK_FALSE(rule.Due(true, false, false, 14.0));  // taken: its clip
    // Empty again: another 2 s later, no cooldown.
    CHECK_FALSE(rule.Due(true, true, false, 20.0));
    CHECK(rule.Due(true, true, false, 22.0));
    // One that went untaken while he stays dry: the next 2 s after.
    rule.Spawned();
    CHECK_FALSE(rule.Due(true, true, false, 23.0));
    CHECK(rule.Due(true, true, false, 25.0));

    DryRule other;
    CHECK_FALSE(other.Due(false, true, false, 0.0));  // Skyrim has the player, or CJ isn't his own
    CHECK_FALSE(other.Due(false, true, false, 50.0));
    CHECK_FALSE(other.Due(true, true, false, 51.0));  // the 2 s start when he is CJ
    CHECK(other.Due(true, true, false, 53.0));
    // Rounds again (he picked some up): the wait starts over when he runs dry next.
    CHECK_FALSE(other.Due(true, false, false, 54.0));
    CHECK_FALSE(other.Due(true, true, false, 55.0));
    CHECK(other.Due(true, true, false, 57.0));
    other.Reset();
    CHECK_FALSE(other.Due(true, true, false, 58.0));
}
