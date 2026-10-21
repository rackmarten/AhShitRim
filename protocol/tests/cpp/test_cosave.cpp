#include <doctest/doctest.h>

#include "../../cpp/cosave.h"

using namespace AhShitRim::Cosave;

namespace
{
    Record Sample()
    {
        Record r;
        r.handover = true;
        r.armor = 62.5f;
        r.car.model = 495;
        r.car.colours[0] = 1;
        r.car.colours[1] = 2;
        r.car.colours[2] = 3;
        r.car.colours[3] = 4;
        r.car.offset[0] = 10.0f;
        r.car.offset[1] = -20.0f;
        r.car.offset[2] = 3.5f;
        r.car.heading = 1.25f;
        r.weapons.slot = 4;
        r.weapons.entries[0] = { AhShitRim::Weapons::BrassKnuckle, 0, 1, 1 };
        r.weapons.entries[2] = { AhShitRim::Weapons::Pistol, 0, 17, 117 };
        r.weapons.entries[4] = { AhShitRim::Weapons::MicroUzi, 0, 50, 650 };
        r.weapons.entries[7] = { AhShitRim::Weapons::Minigun, 0, 500, 99999 };
        return r;
    }
}

TEST_CASE("a record round trips")
{
    const auto bytes = Write(Sample());
    CHECK(bytes.size() == kSizeV3);

    Record back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back) == Result::Ok);
    CHECK(back.handover);
    CHECK(back.armor == doctest::Approx(62.5f));
    CHECK(back.car.model == 495);
    CHECK(back.car.colours[0] == 1);
    CHECK(back.car.colours[3] == 4);
    CHECK(back.car.offset[0] == doctest::Approx(10.0f));
    CHECK(back.car.offset[1] == doctest::Approx(-20.0f));
    CHECK(back.car.offset[2] == doctest::Approx(3.5f));
    CHECK(back.car.heading == doctest::Approx(1.25f));
    CHECK(back.weapons.slot == 4);
    CHECK(back.weapons.entries[0].type == AhShitRim::Weapons::BrassKnuckle);
    CHECK(back.weapons.entries[2].type == AhShitRim::Weapons::Pistol);
    CHECK(back.weapons.entries[2].clip == 17);
    CHECK(back.weapons.entries[2].ammo == 117);
    CHECK(back.weapons.entries[4].type == AhShitRim::Weapons::MicroUzi);
    CHECK(back.weapons.entries[4].ammo == 650);
    CHECK(back.weapons.entries[7].ammo == 99999);
    CHECK(back.weapons.entries[1].type == 0);
    CHECK(back.weapons.entries[12].type == 0);
}

TEST_CASE("a CJ on foot with the handover off round trips")
{
    const auto bytes = Write(Record{});
    Record     back = Sample();
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back) == Result::Ok);
    CHECK_FALSE(back.handover);
    CHECK(back.armor == 0.0f);
    CHECK(back.car.model == 0);
}

TEST_CASE("an older record upgrades: what it lacks is at its defaults")
{
    // Version 1 held the handover and the armour only.
    const auto full = Write(Sample());
    const auto v1 = std::vector<std::uint8_t>(full.begin(), full.begin() + kSizeV1);

    Record back = Sample();  // not the defaults, to see them restored
    REQUIRE(Read(1, v1.data(), v1.size(), back) == Result::Ok);
    CHECK(back.handover);
    CHECK(back.armor == doctest::Approx(62.5f));
    CHECK(back.car.model == 0);
    CHECK(back.car.heading == 0.0f);
}

TEST_CASE("a version 2 record upgrades: no weapons")
{
    const auto full = Write(Sample());
    const auto v2 = std::vector<std::uint8_t>(full.begin(), full.begin() + kSizeV2);

    Record back = Sample();  // not the defaults, to see them restored
    REQUIRE(Read(2, v2.data(), v2.size(), back) == Result::Ok);
    CHECK(back.car.model == 495);
    CHECK(back.weapons.slot == 0);
    CHECK(AhShitRim::Weapons::Empty(back.weapons.entries));
    CHECK(Read(2, v2.data(), kSizeV2 - 1, back) == Result::Short);
}

TEST_CASE("a weapon San Andreas can't hold is left out and the rest of the record stays; a slot past the last makes it bad")
{
    Record back;
    auto   r = Sample();
    r.weapons.entries[3] = { 19, 0, 1, 1 };  // a rocket projectile
    auto bytes = Write(r);
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back) == Result::Ok);
    CHECK(back.weapons.entries[3].type == 0);
    CHECK(back.weapons.entries[2].type == AhShitRim::Weapons::Pistol);
    CHECK(back.car.model == 495);
    CHECK(back.handover);

    r = Sample();
    r.weapons.slot = 13;
    bytes = Write(r);
    CHECK(Read(kVersion, bytes.data(), bytes.size(), back) == Result::Bad);
}

TEST_CASE("too many rounds or a clip bigger than the ammo are put right on the way in")
{
    auto r = Sample();
    r.weapons.entries[2] = { AhShitRim::Weapons::Pistol, 0, 500, 100 };
    r.weapons.entries[4] = { AhShitRim::Weapons::MicroUzi, 0, 10, 4000000000u };
    const auto bytes = Write(r);
    Record     back;
    REQUIRE(Read(kVersion, bytes.data(), bytes.size(), back) == Result::Ok);
    CHECK(back.weapons.entries[2].clip == 100);
    CHECK(back.weapons.entries[4].ammo == 99999);
}

TEST_CASE("a record from a later version or a damaged one is refused")
{
    const auto bytes = Write(Sample());
    Record     back;
    CHECK(Read(kVersion + 1, bytes.data(), bytes.size(), back) == Result::TooNew);
    CHECK(Read(kVersion, bytes.data(), bytes.size() - 1, back) == Result::Short);
    CHECK(Read(1, bytes.data(), kSizeV1 - 1, back) == Result::Short);
    CHECK(Read(0, bytes.data(), bytes.size(), back) == Result::Short);

    auto bad = Sample();
    bad.armor = std::nanf("");
    const auto nan = Write(bad);
    CHECK(Read(kVersion, nan.data(), nan.size(), back) == Result::Bad);
    bad = Sample();
    bad.car.heading = std::nanf("");
    const auto nanHeading = Write(bad);
    CHECK(Read(kVersion, nanHeading.data(), nanHeading.size(), back) == Result::Bad);
}

TEST_CASE("armour is kept between none and a full bar")
{
    auto r = Sample();
    r.armor = 400.0f;
    const auto high = Write(r);
    Record     back;
    REQUIRE(Read(kVersion, high.data(), high.size(), back) == Result::Ok);
    CHECK(back.armor == doctest::Approx(100.0f));
    r.armor = -3.0f;
    const auto low = Write(r);
    REQUIRE(Read(kVersion, low.data(), low.size(), back) == Result::Ok);
    CHECK(back.armor == 0.0f);
}
