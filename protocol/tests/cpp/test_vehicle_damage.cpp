#include <doctest/doctest.h>

#include "../../cpp/explosives.h"
#include "../../cpp/vehicle_damage.h"

using namespace AhShitRim;
using namespace AhShitRim::VehicleDamage;

TEST_CASE("vehicle damage: the engine smokes from 650 down to 250, as San Andreas's overheat")
{
    CHECK(SmokeLevel(1000.0f) == 0.0f);
    CHECK(SmokeLevel(650.0f) == 0.0f);
    CHECK(SmokeLevel(649.0f) > 0.0f);
    CHECK(SmokeLevel(450.0f) == doctest::Approx(0.5f));
    CHECK(SmokeLevel(250.0f) == doctest::Approx(1.0f));
    CHECK(SmokeLevel(249.0f) == 0.0f);  // it burns instead
    CHECK(SmokeLevel(400.0f, true) == 0.0f);  // under water
    CHECK(SmokeLevel(300.0f) > SmokeLevel(500.0f));
}

TEST_CASE("vehicle damage: the engine burns under 250 unless wrecked or drowning")
{
    CHECK_FALSE(EngineBurns(250.0f, false, false));
    CHECK(EngineBurns(249.0f, false, false));
    CHECK(EngineBurns(0.0f, false, false));
    CHECK_FALSE(EngineBurns(100.0f, true, false));
    CHECK_FALSE(EngineBurns(100.0f, false, true));
}

TEST_CASE("vehicle damage: a slot's stage")
{
    const auto valid = Protocol::kBodyValid;
    CHECK(StageOf(valid, 1000.0f) == Stage::Fine);
    CHECK(StageOf(valid, 500.0f) == Stage::Smoke);
    CHECK(StageOf(valid | Protocol::kBodyBurning, 200.0f) == Stage::Fire);
    CHECK(StageOf(valid | Protocol::kBodyWrecked, 0.0f) == Stage::Wreck);
    CHECK(StageOf(valid | Protocol::kBodyWrecked | Protocol::kBodyBurning, 0.0f) == Stage::Wreck);
    CHECK(StageOf(valid, 200.0f) == Stage::Fine);  // under 250 without San Andreas's fire: nothing of ours
}

TEST_CASE("vehicle damage: white smoke, then all at once black, through the fire and a burning wreck")
{
    const float white = kDarkFrom - 0.001f;
    CHECK(WhiteSmoke(Stage::Smoke, white));
    CHECK_FALSE(BlackSmoke(Stage::Smoke, white, false));
    CHECK_FALSE(WhiteSmoke(Stage::Smoke, kDarkFrom));
    CHECK(BlackSmoke(Stage::Smoke, kDarkFrom, false));
    CHECK(SmokeLevel(450.0f) >= kDarkFrom);  // black from 450 health
    CHECK(SmokeLevel(451.0f) < kDarkFrom);
    CHECK(BlackSmoke(Stage::Fire, 0.0f, true));
    CHECK_FALSE(WhiteSmoke(Stage::Fire, 0.0f));
    CHECK(BlackSmoke(Stage::Wreck, 0.0f, true));
    CHECK_FALSE(BlackSmoke(Stage::Wreck, 0.0f, false));
    CHECK_FALSE(BlackSmoke(Stage::Fine, 0.0f, false));
    CHECK_FALSE(WhiteSmoke(Stage::Fine, 0.0f));
}

TEST_CASE("vehicle damage: white smoke grows thicker and more often; the fire is big")
{
    const Puff thin = SmokePuff(0.1f);
    const Puff thick = SmokePuff(0.45f);
    CHECK(thin.effect == Effect::WhiteSmoke);
    CHECK(thick.scale > thin.scale);
    CHECK(thick.every < thin.every);
    CHECK(thin.every > 0.0f);
    CHECK(SmokePuff(5.0f).scale == doctest::Approx(SmokePuff(1.0f).scale));
    CHECK(kEngineFire.effect == Effect::Fire);
    CHECK(kEngineFire.scale >= 3.0f);
    CHECK(kWreckFire.scale > kEngineFire.scale);
}

TEST_CASE("vehicle damage: the engine's place, or the bonnet without a dummy")
{
    const Vec3 dummy{ 0.0f, 1.8f, 0.3f };
    const Vec3 lo{ -1.0f, -2.5f, -0.8f }, hi{ 1.0f, 2.5f, 1.2f };
    const Vec3 a = EnginePoint(dummy, lo, hi);
    CHECK(a.y == doctest::Approx(1.8f));
    CHECK(a.z == doctest::Approx(0.3f));
    const Vec3 b = EnginePoint({}, lo, hi);
    CHECK(b.x == 0.0f);
    CHECK(b.y == doctest::Approx(1.75f));
    CHECK(b.y < hi.y);
    CHECK(b.z > (lo.z + hi.z) / 2.0f);
    CHECK(b.z < hi.z);
}

TEST_CASE("vehicle damage: a wreck's colours are scorched")
{
    CHECK(Scorch({ 255, 100, 0 }) == Vehicle::Rgb{ 51, 20, 0 });
    CHECK(Scorch({ 0, 0, 0 }) == Vehicle::Rgb{ 0, 0, 0 });
}

TEST_CASE("vehicle damage: a blow-up is seen once, and never for an old wreck")
{
    Watch w;
    CHECK_FALSE(w.BlewUp(true, 3, false));
    CHECK_FALSE(w.BlewUp(true, 3, false));
    CHECK(w.BlewUp(true, 3, true));
    CHECK_FALSE(w.BlewUp(true, 3, true));  // still a wreck
    CHECK_FALSE(w.BlewUp(false, 3, false));  // gone
    CHECK_FALSE(w.BlewUp(true, 3, true));  // first seen wrecked

    Watch fresh;
    CHECK_FALSE(fresh.BlewUp(true, 4, true));

    Watch handover;
    CHECK_FALSE(handover.BlewUp(true, 4, false));
    CHECK_FALSE(handover.BlewUp(true, 5, true));  // another handover's vehicle

    Watch reset;
    CHECK_FALSE(reset.BlewUp(true, 1, false));
    reset.Reset();
    CHECK_FALSE(reset.BlewUp(true, 1, true));

    Watch respawn;  // a new vehicle in the slot, not wrecked, then blown up
    CHECK_FALSE(respawn.BlewUp(true, 1, true));
    CHECK_FALSE(respawn.BlewUp(true, 1, false));
    CHECK(respawn.BlewUp(true, 1, true));
}

TEST_CASE("vehicle damage: a vehicle's blast is a rocket's, bigger, and opens no locks")
{
    using namespace Explosives;
    const Blast car = BlastOf(Kind::Vehicle);
    const Blast rocket = BlastOf(Kind::Rocket);
    CHECK(car.radius > rocket.radius);
    CHECK(car.damage == doctest::Approx(rocket.damage));
    CHECK_FALSE(car.opensLocks);
    CHECK(BlastDamage(Kind::Vehicle, 0.0f) == doctest::Approx(300.0f));
    CHECK(BlastDamage(Kind::Vehicle, car.radius + 1.0f) == 0.0f);
    CHECK(KnockMagnitude(Kind::Vehicle, 0.0f, Damage::Class::Normal) > 0.0f);
    CHECK(KindOf(19) == Kind::Rocket);  // no projectile is a vehicle
}
