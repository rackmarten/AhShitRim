#include <doctest/doctest.h>

#include "../../cpp/gxt.h"

#include <utility>
#include <vector>

using namespace AhShitRim::Gxt;

namespace
{
    void Put32(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
    {
        for (int i = 0; i < 4; i++) {
            a_out.push_back(static_cast<std::uint8_t>(a_value >> (8 * i)));
        }
    }

    void PutTag(std::vector<std::uint8_t>& a_out, const char* a_tag, std::size_t a_size)
    {
        a_out.insert(a_out.end(), a_tag, a_tag + a_size);
    }

    // A San Andreas GXT with MAIN holding a_texts (key, text) and a second table after it.
    std::vector<std::uint8_t> MakeGxt(const std::vector<std::pair<std::string, std::string>>& a_texts)
    {
        std::vector<std::uint8_t> out = { 4, 0, 8, 0 };
        PutTag(out, "TABL", 4);
        Put32(out, 24);
        const std::size_t mainAt = 12 + 24;  // the header, TABL and its two entries
        std::vector<std::uint8_t> dat;
        std::vector<std::uint8_t> keys;
        for (const auto& [key, text] : a_texts) {
            Put32(keys, static_cast<std::uint32_t>(dat.size()));
            Put32(keys, KeyHash(key));
            dat.insert(dat.end(), text.begin(), text.end());
            dat.push_back(0);
        }
        const std::size_t otherAt = mainAt + 8 + keys.size() + 8 + dat.size();
        PutTag(out, "MAIN\0\0\0\0", 8);
        Put32(out, static_cast<std::uint32_t>(mainAt));
        PutTag(out, "OTHER\0\0\0", 8);
        Put32(out, static_cast<std::uint32_t>(otherAt));
        PutTag(out, "TKEY", 4);
        Put32(out, static_cast<std::uint32_t>(keys.size()));
        out.insert(out.end(), keys.begin(), keys.end());
        PutTag(out, "TDAT", 4);
        Put32(out, static_cast<std::uint32_t>(dat.size()));
        out.insert(out.end(), dat.begin(), dat.end());
        PutTag(out, "OTHER\0\0\0TKEY", 12);
        Put32(out, 0);
        PutTag(out, "TDAT", 4);
        Put32(out, 0);
        return out;
    }

    constexpr std::string_view kIde = R"(# vehicles.ide
peds
0, null, null, CIVMALE, STAT_PLAYER, man, 7FFFFFFF, 0, null, 0, 0
end
cars
400,	landstal,	landstal,	car,		LANDSTAL,	LANDSTK,	null,	richfamily,	10,	0,	0,	-1,	0.768,	0.768,	0
481, bmx, bmx, bmx, BMX, BMX, bmx, bicycle, 10, 0, 0, -1, 0.65, 0.65, -1   # a comment
432, rhino, rhino, car, RHINO, RHINO, null, ignore, 1, 0, 0, -1, 1.2, 1.2, -1
435, artict1, artict1, trailer, ARTICT1, ARTICT1, null, ignore, 1, 0, 0, -1, 1.1, 1.1, -1
broken line
end
)";
}

TEST_CASE("a GXT key's hash is San Andreas's CRC-32 of it in upper case")
{
    CHECK(KeyHash("") == 0xFFFFFFFFu);
    CHECK(KeyHash("landstk") == KeyHash("LANDSTK"));
    CHECK(KeyHash("LANDSTK") != KeyHash("LANDSTA"));
    // The standard CRC-32 of "123456789" is 0xCBF43926; without its final inversion it is the complement.
    CHECK(KeyHash("123456789") == (0xCBF43926u ^ 0xFFFFFFFFu));
}

TEST_CASE("the MAIN table's texts read by key")
{
    const auto gxt = MakeGxt({ { "LANDSTK", "Landstalker" }, { "BMX", "BMX" }, { "RHINO", "Rhino" } });
    const auto texts = ReadMain(gxt.data(), gxt.size());
    REQUIRE(texts.size() == 3);
    CHECK(texts.at(KeyHash("LANDSTK")) == "Landstalker");
    CHECK(texts.at(KeyHash("RHINO")) == "Rhino");
    CHECK(ReadMain(nullptr, 0).empty());
    CHECK(ReadMain(gxt.data(), 20).empty());  // cut short
    std::vector<std::uint8_t> other = gxt;
    other[4] = 'X';  // not a GXT
    CHECK(ReadMain(other.data(), other.size()).empty());
}

TEST_CASE("vehicles.ide's cars give each model its game name")
{
    const auto names = ReadVehicleGameNames(kIde);
    CHECK(names.size() == 4);
    CHECK(names.at(400) == "LANDSTK");
    CHECK(names.at(481) == "BMX");
    CHECK(names.count(0) == 0);  // a ped, not a car
}

TEST_CASE("vehicle names: models whose game name the GXT has")
{
    const auto gxt = MakeGxt({ { "LANDSTK", "Landstalker" }, { "BMX", "BMX" }, { "RHINO", "Rhino" } });
    const auto names = VehicleNames(kIde, gxt.data(), gxt.size());
    CHECK(names.size() == 3);
    CHECK(names.at(400) == "Landstalker");
    CHECK(names.at(432) == "Rhino");
    CHECK(names.count(435) == 0);  // a trailer San Andreas has no name for
    CHECK(VehicleNames(kIde, nullptr, 0).empty());
}
