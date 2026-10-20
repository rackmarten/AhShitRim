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
        return r;
    }
}

TEST_CASE("a record round trips")
{
    const auto bytes = Write(Sample());
    CHECK(bytes.size() == kSizeV2);

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
