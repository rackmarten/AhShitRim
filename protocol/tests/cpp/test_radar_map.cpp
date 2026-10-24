#include <cmath>
#include <cstring>
#include <initializer_list>
#include <ostream>  // before doctest, so it can print string_view
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "../../cpp/esm.h"
#include "../../cpp/inflate.h"
#include "../../cpp/radar_map.h"

using namespace AhShitRim;
using Bytes = std::vector<std::uint8_t>;

namespace
{
    // ---- A tiny plugin writer ----

    void Put(Bytes& a_out, const void* a_data, std::size_t a_size)
    {
        const auto* p = static_cast<const std::uint8_t*>(a_data);
        a_out.insert(a_out.end(), p, p + a_size);
    }

    template <class T>
    void Put(Bytes& a_out, T a_value)
    {
        Put(a_out, &a_value, sizeof(a_value));
    }

    void Append(Bytes& a_out, const Bytes& a_more) { a_out.insert(a_out.end(), a_more.begin(), a_more.end()); }

    Bytes Field(const char (&a_tag)[5], const Bytes& a_data)
    {
        Bytes out;
        Put(out, a_tag, 4);
        Put(out, static_cast<std::uint16_t>(a_data.size()));
        Append(out, a_data);
        return out;
    }

    Bytes Text(std::string_view a_text)
    {
        Bytes out(a_text.begin(), a_text.end());
        out.push_back(0);
        return out;
    }

    template <class... T>
    Bytes Values(T... a_values)
    {
        Bytes out;
        (Put(out, a_values), ...);
        return out;
    }

    Bytes Concat(std::initializer_list<Bytes> a_parts)
    {
        Bytes out;
        for (const auto& part : a_parts) {
            Append(out, part);
        }
        return out;
    }

    // zlib with one stored block: what Skyrim's reader accepts like any other stream.
    Bytes ZlibStored(const Bytes& a_data)
    {
        Bytes out{ 0x78, 0x01, 0x01 };
        Put(out, static_cast<std::uint16_t>(a_data.size()));
        Put(out, static_cast<std::uint16_t>(~a_data.size()));
        Append(out, a_data);
        std::uint32_t a = 1, b = 0;
        for (const auto byte : a_data) {
            a = (a + byte) % 65521;
            b = (b + a) % 65521;
        }
        const std::uint32_t adler = b << 16 | a;
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>(adler >> shift));
        }
        return out;
    }

    Bytes Record(const char (&a_type)[5], std::uint32_t a_formId, const Bytes& a_fields, std::uint32_t a_flags = 0)
    {
        Bytes data = a_fields;
        if (a_flags & Esm::kFlagCompressed) {
            data = Values(static_cast<std::uint32_t>(a_fields.size()));
            Append(data, ZlibStored(a_fields));
        }
        Bytes out;
        Put(out, a_type, 4);
        Put(out, static_cast<std::uint32_t>(data.size()));
        Put(out, a_flags);
        Put(out, a_formId);
        Put(out, std::uint32_t{ 0 });
        Put(out, std::uint16_t{ 44 });
        Put(out, std::uint16_t{ 0 });
        Append(out, data);
        return out;
    }

    Bytes Group(std::uint32_t a_label, std::int32_t a_type, std::initializer_list<Bytes> a_children)
    {
        const Bytes contents = Concat(a_children);
        Bytes       out;
        Put(out, "GRUP", 4);
        Put(out, static_cast<std::uint32_t>(contents.size() + Esm::kHeaderSize));
        Put(out, a_label);
        Put(out, a_type);
        Put(out, std::uint64_t{ 0 });
        Append(out, contents);
        return out;
    }

    Bytes Header(std::initializer_list<std::string_view> a_masters, std::uint32_t a_flags = Esm::kFlagMaster)
    {
        Bytes fields = Field("HEDR", Values(1.71f, std::uint32_t{ 0 }, std::uint32_t{ 0x800 }));
        for (const auto master : a_masters) {
            Append(fields, Field("MAST", Text(master)));
            Append(fields, Field("DATA", Values(std::uint64_t{ 0 })));
        }
        return Record("TES4", 0, fields, a_flags);
    }

    // A flat landscape a_height units up: VHGT's offset is in units of 8, every delta 0.
    Bytes FlatHeights(float a_height)
    {
        Bytes out = Values(a_height / 8.0f);
        out.resize(4 + Esm::kLandVertices + 3, 0);
        return out;
    }

    Bytes BaseTexture(std::uint32_t a_texture, std::uint8_t a_quadrant) { return Field("BTXT", Values(a_texture, a_quadrant, std::uint8_t{ 0 }, std::int16_t{ -1 })); }

    Bytes AlphaTexture(std::uint32_t a_texture, std::uint8_t a_quadrant, std::int16_t a_layer, std::initializer_list<std::pair<std::uint16_t, float>> a_opacity)
    {
        Bytes out = Field("ATXT", Values(a_texture, a_quadrant, std::uint8_t{ 0 }, a_layer));
        Bytes points;
        for (const auto& [position, opacity] : a_opacity) {
            Append(points, Values(position, std::uint16_t{ 0 }, opacity));
        }
        Append(out, Field("VTXT", points));
        return out;
    }

    Bytes Bounds(std::int16_t a_x1, std::int16_t a_y1, std::int16_t a_z1, std::int16_t a_x2, std::int16_t a_y2, std::int16_t a_z2)
    {
        return Field("OBND", Values(a_x1, a_y1, a_z1, a_x2, a_y2, a_z2));
    }

    Bytes Placement(std::uint32_t a_base, float a_x, float a_y, float a_rotation = 0.0f)
    {
        return Concat({ Field("NAME", Values(a_base)), Field("DATA", Values(a_x, a_y, 0.0f, 0.0f, 0.0f, a_rotation)) });
    }

    constexpr std::uint32_t kTamriel = 0x3C;

    // Skyrim in miniature: two ground textures, a road piece, a house, a pine and a shrub; Tamriel with
    // one cell at (2, -1) standing 200 units up, its water at 100; a child city world on Tamriel's land
    // with a house of its own; and a cave world named as Tamriel's child that isn't on its land.
    Bytes Master()
    {
        const Bytes landFields = Concat({ Field("DATA", Values(std::uint32_t{ 1 })), Field("VHGT", FlatHeights(200.0f)), BaseTexture(0x801, 0), BaseTexture(0x801, 1), BaseTexture(0x801, 2),
            BaseTexture(0x801, 3), AlphaTexture(0x800, 0, 0, { { 0, 1.0f }, { 1, 0.4f } }) });
        return Concat({ Header({}),
            Group(Esm::Tag("LTEX"), Esm::kGroupTop,
                { Record("LTEX", 0x800, Field("EDID", Text("LSnow01"))), Record("LTEX", 0x801, Field("EDID", Text("LTundra01"))) }),
            Group(Esm::Tag("STAT"), Esm::kGroupTop,
                { Record("STAT", 0x900, Concat({ Field("EDID", Text("RoadStraightLong01")), Bounds(-486, -1024, -42, 486, 1024, 12), Field("MODL", Text("Landscape\\Roads\\RoadStraightLong01.nif")) })),
                    Record("STAT", 0x901, Concat({ Field("EDID", Text("WRHouse01")), Bounds(-400, -300, 0, 400, 300, 600), Field("MODL", Text("Architecture\\Whiterun\\WRHouse01.nif")) })) }),
            Group(Esm::Tag("TREE"), Esm::kGroupTop,
                { Record("TREE", 0x902, Concat({ Bounds(-300, -300, 0, 300, 300, 1400), Field("MODL", Text("Landscape\\Trees\\TreePineForest01.nif")) })),
                    Record("TREE", 0x903, Concat({ Bounds(-90, -90, 0, 90, 90, 130), Field("MODL", Text("Landscape\\Plants\\Thicket01.nif")) })) }),
            Group(Esm::Tag("WRLD"), Esm::kGroupTop,
                { Record("WRLD", kTamriel, Concat({ Field("EDID", Text("Tamriel")), Field("DNAM", Values(-27000.0f, -14000.0f)) })),
                    Group(kTamriel, Esm::kGroupWorldChildren,
                        { Group(0, Esm::kGroupExteriorBlock,
                            { Group(0, Esm::kGroupExteriorSubBlock,
                                { Record("CELL", 0xA00, Concat({ Field("DATA", Values(std::uint16_t{ 2 })), Field("XCLC", Values(std::int32_t{ 2 }, std::int32_t{ -1 }, std::uint32_t{ 0 })), Field("XCLW", Values(100.0f)) }),
                                      Esm::kFlagCompressed),
                                    Group(0xA00, Esm::kGroupCellChildren,
                                        { Group(0xA00, Esm::kGroupCellTemporary,
                                            { Record("LAND", 0xA01, landFields, Esm::kFlagCompressed), Record("REFR", 0xB00, Placement(0x900, 9000.0f, -2000.0f)),
                                                Record("REFR", 0xB01, Placement(0x902, 9500.0f, -2500.0f)), Record("REFR", 0xB02, Placement(0x903, 9600.0f, -2600.0f)),
                                                Record("REFR", 0xB03, Placement(0x901, 10000.0f, -3000.0f), Esm::kFlagInitiallyDisabled) }) }) }) }) }),
                    Record("WRLD", 0xC00, Concat({ Field("EDID", Text("WhiterunWorld")), Field("WNAM", Values(kTamriel)), Field("PNAM", Values(std::uint16_t{ 0x7F }, std::uint8_t{ 0 })) })),
                    Group(0xC00, Esm::kGroupWorldChildren,
                        { Record("CELL", 0xC01, Concat({ Field("XCLC", Values(std::int32_t{ 2 }, std::int32_t{ -1 }, std::uint32_t{ 0 })) })),
                            Group(0xC01, Esm::kGroupCellChildren,
                                { Group(0xC01, Esm::kGroupCellTemporary,
                                    { Record("LAND", 0xC02, Field("VHGT", FlatHeights(-5000.0f))), Record("REFR", 0xC03, Placement(0x901, 8500.0f, -1000.0f)) }) }) }),
                    Record("WRLD", 0xD00, Concat({ Field("EDID", Text("SomeCaveWorld")), Field("WNAM", Values(kTamriel)), Field("PNAM", Values(std::uint16_t{ 0x14 }, std::uint8_t{ 0 })) })),
                    Group(0xD00, Esm::kGroupWorldChildren,
                        { Record("CELL", 0xD01, Field("XCLC", Values(std::int32_t{ 0 }, std::int32_t{ 0 }, std::uint32_t{ 0 }))),
                            Group(0xD01, Esm::kGroupCellChildren, { Group(0xD01, Esm::kGroupCellTemporary, { Record("REFR", 0xD02, Placement(0x901, 100.0f, 100.0f)) }) }) }) }) });
    }

    // A mod on it: the cell back to the world's water (sea level), the road piece deleted, a house added.
    Bytes Mod()
    {
        return Concat({ Header({ "Test.esm" }, 0),
            Group(Esm::Tag("WRLD"), Esm::kGroupTop,
                { Record("WRLD", kTamriel, Concat({ Field("EDID", Text("Tamriel")), Field("DNAM", Values(-27000.0f, -14000.0f)) })),
                    Group(kTamriel, Esm::kGroupWorldChildren,
                        { Record("CELL", 0xA00, Field("XCLC", Values(std::int32_t{ 2 }, std::int32_t{ -1 }, std::uint32_t{ 0 }))),
                            Group(0xA00, Esm::kGroupCellChildren,
                                { Group(0xA00, Esm::kGroupCellTemporary,
                                    { Record("REFR", 0xB00, Placement(0x900, 9000.0f, -2000.0f), Esm::kFlagDeleted), Record("REFR", 0x01000001, Placement(0x901, 11000.0f, -3500.0f)) }) }) }) }) });
    }

    RadarMap::Mask MaskFrom(std::initializer_list<std::string_view> a_rows)
    {
        RadarMap::Mask mask(static_cast<int>(a_rows.begin()->size()), static_cast<int>(a_rows.size()));
        int            y = 0;
        for (const auto row : a_rows) {
            for (int x = 0; x < mask.width; ++x) {
                mask.at(x, y) = row[x] == '#' ? 1 : 0;
            }
            ++y;
        }
        return mask;
    }

    int Count(const RadarMap::Mask& a_mask)
    {
        int count = 0;
        for (const auto p : a_mask.pixels) {
            count += p;
        }
        return count;
    }
}

TEST_CASE("inflate: stored, fixed and dynamic Huffman blocks")
{
    const Bytes stored{ 0x78, 0x01, 0x01, 0x0c, 0x00, 0xf3, 0xff, 0x47, 0x72, 0x6f, 0x76, 0x65, 0x20, 0x53, 0x74, 0x72, 0x65, 0x65, 0x74, 0x1d, 0x35, 0x04, 0x9b };
    const auto  a = Inflate::Zlib(stored.data(), stored.size());
    REQUIRE(a);
    CHECK(std::string(a->begin(), a->end()) == "Grove Street");

    // Back-references reaching over the repeats.
    const Bytes fixed{ 0x78, 0xda, 0x73, 0xcc, 0x08, 0xce, 0xc8, 0x2c, 0x09, 0xca, 0xcc, 0x55, 0x70, 0xc4, 0x64, 0x01, 0x00, 0x9b, 0x1b, 0x0a, 0x7c };
    const auto  b = Inflate::Zlib(fixed.data(), fixed.size());
    REQUIRE(b);
    CHECK(std::string(b->begin(), b->end()) == "AhShitRim AhShitRim AhShitRim");

    const Bytes dynamic{ 0x78, 0xda, 0x15, 0xcb, 0x31, 0x0e, 0xc2, 0x30, 0x10, 0x44, 0xd1, 0xab, 0xcc, 0x01, 0xac, 0xdc, 0x81, 0x0a, 0x51, 0xd0, 0x90, 0x22, 0xb5, 0x91, 0x27, 0xf1, 0x8a, 0xd8,
        0x8b, 0xd6, 0x9b, 0x58, 0xdc, 0x1e, 0xa7, 0x9d, 0x79, 0xff, 0x6e, 0x7a, 0x12, 0xb3, 0x1b, 0xe9, 0x01, 0x59, 0x0b, 0x27, 0xdc, 0x1c, 0x3b, 0x63, 0x73, 0x88, 0xa3, 0xc7, 0x86, 0x37, 0x57, 0x35,
        0xe2, 0x81, 0xc2, 0xd6, 0x98, 0x70, 0x7c, 0xc1, 0x93, 0xf6, 0xf3, 0x2c, 0x75, 0x1b, 0x3c, 0xa3, 0x65, 0xb9, 0x6a, 0x0e, 0xd5, 0x89, 0x4d, 0x11, 0xb7, 0x28, 0x75, 0xc2, 0x32, 0x76, 0xda, 0x51,
        0x03, 0x5e, 0x32, 0x8a, 0xae, 0x9a, 0x02, 0x66, 0xdd, 0xc5, 0x8f, 0xc4, 0x80, 0x45, 0x6a, 0xca, 0xdc, 0xcb, 0x75, 0xaf, 0xce, 0xc1, 0x9e, 0xd1, 0x3e, 0xd1, 0x3c, 0x4f, 0xf8, 0x03, 0x11, 0xde,
        0x35, 0x5e };
    const auto c = Inflate::Zlib(dynamic.data(), dynamic.size(), 153);
    REQUIRE(c);
    CHECK(std::string(c->begin(), c->end()) ==
          "Grove Street, home. At least it was before I messed up everything. Ah shit, here we go again. Whiterun, Riverwood, Solitude, Windhelm, Riften, Markarth. ");
}

TEST_CASE("inflate: refuses what isn't zlib, and streams cut short")
{
    const Bytes notZlib{ 0x1f, 0x8b, 0x08, 0x00 };
    CHECK_FALSE(Inflate::Zlib(notZlib.data(), notZlib.size()));
    const Bytes fixed{ 0x78, 0xda, 0x73, 0xcc, 0x08, 0xce, 0xc8, 0x2c, 0x09, 0xca, 0xcc, 0x55, 0x70, 0xc4, 0x64, 0x01, 0x00, 0x9b, 0x1b, 0x0a, 0x7c };
    CHECK_FALSE(Inflate::Zlib(fixed.data(), 10));
    const Bytes badStored{ 0x78, 0x01, 0x01, 0x0c, 0x00, 0x00, 0x00, 0x47 };  // length and its complement disagree
    CHECK_FALSE(Inflate::Zlib(badStored.data(), badStored.size()));
}

TEST_CASE("esm: records, groups, big fields and compressed records")
{
    const Bytes big(70000, 7);
    Bytes       fields = Field("EDID", Text("Big"));
    Append(fields, Field("XXXX", Values(static_cast<std::uint32_t>(big.size()))));
    Append(fields, Concat({ Bytes{ 'D', 'A', 'T', 'A', 0, 0 }, big }));
    Append(fields, Field("FULL", Text("after")));

    const Bytes file = Concat({ Record("STAT", 0x123, fields), Group(Esm::Tag("TREE"), Esm::kGroupTop, { Record("TREE", 0x456, Field("EDID", Text("Pine")), Esm::kFlagCompressed) }) });
    std::vector<Esm::Entry> entries;
    CHECK(Esm::ForEach(file.data(), file.data() + file.size(), [&](const Esm::Entry& a_entry) { entries.push_back(a_entry); }));
    REQUIRE(entries.size() == 2);
    CHECK_FALSE(entries[0].group);
    CHECK(entries[0].type == Esm::Tag("STAT"));
    CHECK(entries[0].formId == 0x123);
    CHECK(entries[1].group);
    CHECK(entries[1].label == Esm::Tag("TREE"));

    std::vector<std::pair<std::uint32_t, std::size_t>> seen;
    const auto                                         data = Esm::Fields(entries[0]);
    CHECK(Esm::ForEachField(data.data, data.size, [&](std::uint32_t a_type, const std::uint8_t*, std::size_t a_size) { seen.emplace_back(a_type, a_size); }));
    REQUIRE(seen.size() == 3);
    CHECK(seen[1] == std::pair{ Esm::Tag("DATA"), big.size() });
    CHECK(seen[2] == std::pair{ Esm::Tag("FULL"), std::size_t{ 6 } });

    Esm::ForEach(entries[1].data, entries[1].data + entries[1].size, [&](const Esm::Entry& a_tree) {
        const auto inner = Esm::Fields(a_tree);
        REQUIRE(inner.ok);
        CHECK(Esm::ReadBaseObject(inner.data, inner.size).editorId == "Pine");
    });

    // A record running past its group is refused rather than read out of bounds.
    CHECK_FALSE(Esm::ForEach(file.data(), file.data() + file.size() - 3, [](const Esm::Entry&) {}));
}

TEST_CASE("esm: landscape heights add up along rows and columns, in units of 8")
{
    Bytes vhgt = Values(10.0f);
    vhgt.resize(4 + Esm::kLandVertices + 3, 0);
    vhgt[4 + 0] = 2;                          // row 0 starts 2 up from the offset
    vhgt[4 + 1] = 1;                          // then each column adds
    vhgt[4 + 2] = static_cast<std::uint8_t>(-3);
    vhgt[4 + Esm::kLandSide] = 5;              // row 1 starts 5 up from row 0's start
    std::array<float, Esm::kLandVertices> heights{};
    Esm::DecodeHeights(vhgt.data(), vhgt.size(), heights);
    CHECK(heights[0] == doctest::Approx(12 * 8));
    CHECK(heights[1] == doctest::Approx(13 * 8));
    CHECK(heights[2] == doctest::Approx(10 * 8));
    CHECK(heights[32] == doctest::Approx(10 * 8));
    CHECK(heights[Esm::kLandSide] == doctest::Approx(17 * 8));
    CHECK(heights[Esm::kLandSide + 1] == doctest::Approx(17 * 8));
}

TEST_CASE("radar map: ground classes from Skyrim's landscape texture names")
{
    using RadarMap::Ground;
    CHECK(RadarMap::ClassifyLandTexture("LTundra01") == Ground::Tundra);
    CHECK(RadarMap::ClassifyLandTexture("LFieldGrass01") == Ground::Grass);
    CHECK(RadarMap::ClassifyLandTexture("LReachMoss01") == Ground::Grass);
    CHECK(RadarMap::ClassifyLandTexture("LPineForest02NoGrass") == Ground::Forest);
    CHECK(RadarMap::ClassifyLandTexture("LFallForestLeaves01") == Ground::Forest);
    CHECK(RadarMap::ClassifyLandTexture("LSnow02") == Ground::Snow);
    CHECK(RadarMap::ClassifyLandTexture("LSnowRocks01wGrass") == Ground::Snow);
    CHECK(RadarMap::ClassifyLandTexture("LGrassSnow01") == Ground::Snow);
    CHECK(RadarMap::ClassifyLandTexture("LFrozenMarshIce01") == Ground::Snow);
    CHECK(RadarMap::ClassifyLandTexture("LMountainCliff01") == Ground::Rock);
    CHECK(RadarMap::ClassifyLandTexture("LTundraRocks01NoRocks") == Ground::Rock);
    CHECK(RadarMap::ClassifyLandTexture("LVolcanicTundraGravel01") == Ground::Rock);
    CHECK(RadarMap::ClassifyLandTexture("LVolcanicTundraDirt01") == Ground::Dirt);
    CHECK(RadarMap::ClassifyLandTexture("LRiverMud01") == Ground::Dirt);
    CHECK(RadarMap::ClassifyLandTexture("LCoastBeach01") == Ground::Sand);
    CHECK(RadarMap::ClassifyLandTexture("LRiverBottom01NoGrass") == Ground::Sand);
    CHECK(RadarMap::ClassifyLandTexture("LDirtPath01") == Ground::Path);
    CHECK(RadarMap::ClassifyLandTexture("LDirtSnowPath01") == Ground::Path);
    CHECK(RadarMap::ClassifyLandTexture("LSnowCobble01") == Ground::Path);
    CHECK(RadarMap::ClassifyLandTexture("LFrozenMarshLichen01") == Ground::Tundra);
    // A mod's texture with an unhelpful editor ID still classifies by its file.
    CHECK(RadarMap::ClassifyLandTexture("ModTex042", "Landscape\\Mod\\snowfield.dds") == Ground::Snow);
    CHECK(RadarMap::ClassifyLandTexture("SomethingNew") == Ground::Tundra);
}

TEST_CASE("radar map: what placed models draw")
{
    using RadarMap::Feature;
    CHECK(RadarMap::ClassifyModel("Landscape\\Roads\\RoadStraightLong01.nif") == Feature::Road);
    CHECK(RadarMap::ClassifyModel("meshes/landscape/roads/roadcurve90r01.nif") == Feature::Road);
    CHECK(RadarMap::ClassifyModel("Landscape\\Bridges\\Bridge01.nif") == Feature::Road);
    CHECK(RadarMap::ClassifyModel("Architecture\\WhiteRun\\WRTerrain\\WRMainRoadPlains.nif") == Feature::Road);
    CHECK(RadarMap::ClassifyModel("Architecture\\Farmhouse\\Farmhouse01.nif") == Feature::Building);
    CHECK(RadarMap::ClassifyModel("Architecture\\Solitude\\SCastle.nif") == Feature::Building);
    CHECK(RadarMap::ClassifyModel("Architecture\\WhiteRun\\WRTerrain\\WRWindDistrictTerrainLOD.nif") == Feature::Town);
    CHECK(RadarMap::ClassifyModel("Architecture\\Solitude\\SolitudeBase.nif") == Feature::Town);
    CHECK(RadarMap::ClassifyModel("Architecture\\Riften\\RTLandR01.nif") == Feature::Town);
    CHECK(RadarMap::ClassifyModel("Landscape\\Trees\\TreePineForest05.nif") == Feature::Tree);
    CHECK(RadarMap::ClassifyModel("Landscape\\Plants\\Thicket01.nif") == Feature::None);
    CHECK(RadarMap::ClassifyModel("Dungeons\\Dwemer\\Roads\\DweRoadChunkL01.nif") == Feature::None);
    CHECK(RadarMap::ClassifyModel("Clutter\\Signage\\RoadSigns\\RoadSignWhiterun01.nif") == Feature::None);
}

TEST_CASE("radar map: world coordinates to pixels and back")
{
    RadarMap::Mapping mapping{ -57, -43, 61, 50 };
    CHECK(mapping.Width() == 119 * 32);
    CHECK(mapping.Height() == 94 * 32);
    CHECK(mapping.UnitsPerPixel() == doctest::Approx(128));
    // The image's top-left corner is the north-west corner of the north-westernmost cell.
    const auto [x0, y0] = mapping.WorldToPixel(-57 * 4096.0, 51 * 4096.0);
    CHECK(x0 == doctest::Approx(0));
    CHECK(y0 == doctest::Approx(0));
    // Cell (0, 0)'s south-west corner, world (0, 0).
    const auto [x1, y1] = mapping.WorldToPixel(0, 0);
    CHECK(x1 == doctest::Approx(57 * 32));
    CHECK(y1 == doctest::Approx(51 * 32));
    // North is up: going north lowers the row.
    CHECK(mapping.WorldToPixel(0, 128).second == doctest::Approx(51 * 32 - 1));
    const auto [wx, wy] = mapping.PixelToWorld(1234.5, 678.25);
    const auto [px, py] = mapping.WorldToPixel(wx, wy);
    CHECK(px == doctest::Approx(1234.5));
    CHECK(py == doctest::Approx(678.25));
}

TEST_CASE("radar map: a vertex's ground blends its quadrant's layers by opacity")
{
    Esm::Land land;
    for (int q = 0; q < 4; ++q) {
        Esm::TextureLayer base;
        base.texture = 1;
        base.quadrant = q;
        base.opacity.fill(1.0f);
        land.layers.push_back(base);
    }
    Esm::TextureLayer snow;
    snow.texture = 2;
    snow.quadrant = 3;  // north-east
    snow.layer = 0;
    snow.opacity[0] = 0.8f;   // its south-west vertex: mostly snow
    snow.opacity[1] = 0.3f;   // the next one: mostly the base still
    land.layers.push_back(snow);

    const std::unordered_map<std::uint64_t, RadarMap::Ground> textures{ { 1, RadarMap::Ground::Tundra }, { 2, RadarMap::Ground::Snow } };
    const auto resolve = [](std::uint32_t a_id, const void*) { return static_cast<std::uint64_t>(a_id); };
    std::array<RadarMap::Ground, Esm::kLandVertices> ground{};
    RadarMap::BlendGround(land, textures, resolve, nullptr, ground);
    CHECK(ground[16 * Esm::kLandSide + 16] == RadarMap::Ground::Snow);
    CHECK(ground[16 * Esm::kLandSide + 17] == RadarMap::Ground::Tundra);
    CHECK(ground[0] == RadarMap::Ground::Tundra);
    CHECK(ground[32 * Esm::kLandSide + 32] == RadarMap::Ground::Tundra);
}

TEST_CASE("radar map: gathers landscape, water and placed things across the load order")
{
    const Bytes master = Master();
    {
        RadarMap::Builder builder;
        REQUIRE(builder.AddPlugin("Test.esm", master.data(), master.size()));
        const auto world = builder.Finish();
        CHECK(world.found);
        CHECK(world.defaultWater == doctest::Approx(-14000));
        REQUIRE(world.land.size() == 1);  // the city world's landscape isn't Tamriel's
        CHECK(world.land[0].x == 2);
        CHECK(world.land[0].y == -1);
        CHECK(world.land[0].heights[500] == doctest::Approx(200));
        CHECK(world.land[0].ground[0] == RadarMap::Ground::Snow);    // fully snow at the south-west corner
        CHECK(world.land[0].ground[2] == RadarMap::Ground::Tundra);  // beyond the snow layer's points
        CHECK(world.water.at(RadarMap::CellKey(2, -1)) == doctest::Approx(100));
        // The road, the pine and the city world's house; not the shrub, the disabled house or the cave's.
        REQUIRE(world.placed.size() == 3);
        int roads = 0, trees = 0, buildings = 0;
        for (const auto& placed : world.placed) {
            roads += placed.feature == RadarMap::Feature::Road;
            trees += placed.feature == RadarMap::Feature::Tree;
            buildings += placed.feature == RadarMap::Feature::Building && placed.x == 8500.0f;
        }
        CHECK(roads == 1);
        CHECK(trees == 1);
        CHECK(buildings == 1);
    }

    const Bytes mod = Mod();
    RadarMap::Builder builder;
    REQUIRE(builder.AddPlugin("Test.esm", master.data(), master.size()));
    REQUIRE(builder.AddPlugin("Test.esp", mod.data(), mod.size()));
    const auto world = builder.Finish();
    REQUIRE(world.land.size() == 1);  // the mod's cell has no landscape of its own: the master's stays
    CHECK(world.water.count(RadarMap::CellKey(2, -1)) == 0);  // back to the sea level
    int roads = 0, buildings = 0;
    for (const auto& placed : world.placed) {
        roads += placed.feature == RadarMap::Feature::Road;
        buildings += placed.feature == RadarMap::Feature::Building;
    }
    CHECK(roads == 0);
    CHECK(buildings == 2);
}

TEST_CASE("radar map: the painted map has the cell's ground, the water below it and the road")
{
    const Bytes       master = Master();
    RadarMap::Builder builder;
    REQUIRE(builder.AddPlugin("Test.esm", master.data(), master.size()));
    auto world = builder.Finish();
    const auto mapping = RadarMap::MappingFor(world);
    CHECK(mapping.Width() == 32);
    CHECK(mapping.Height() == 32);
    auto image = RadarMap::Paint(world, mapping);
    // The road piece at (9000, -2000) runs north-south through pixel column (9000 - 8192) / 128 = 6.
    const auto [rx, ry] = mapping.WorldToPixel(9000, -2000);
    CHECK(image.pixels.at(static_cast<int>(rx), static_cast<int>(ry)) == RadarMap::Palette::kRoad);
    CHECK(image.pixels.at(28, 4) == RadarMap::Palette::kTundra);

    // Water above the landscape everywhere: all water but the road (roads cross rivers on bridges).
    world.water[RadarMap::CellKey(2, -1)] = 500.0f;
    image = RadarMap::Paint(world, mapping);
    CHECK(image.pixels.at(28, 4) == RadarMap::Palette::kWater);
}

TEST_CASE("radar map: a rotated box covers the pixels it should")
{
    RadarMap::Mapping mapping{ 0, 0, 0, 0 };  // one cell, 32 x 32 pixels, 128 units each
    RadarMap::Mask    mask(32, 32);
    // 4 pixels wide (east-west), 16 long (north-south), centred on the cell.
    const std::array<float, 4> bounds{ -256, -1024, 256, 1024 };
    RadarMap::FillRotatedBox(mask, mapping, 2048, 2048, 0.0, 1.0, bounds);
    CHECK(Count(mask) == 4 * 16);
    CHECK(mask.at(16, 16));
    CHECK_FALSE(mask.at(20, 16));
    CHECK(mask.at(16, 9));

    // A quarter turn lays it east-west; doubling the scale doubles each side.
    RadarMap::Mask turned(32, 32);
    RadarMap::FillRotatedBox(turned, mapping, 2048, 2048, std::acos(-1.0) / 2, 1.0, bounds);
    CHECK(Count(turned) == 4 * 16);
    CHECK(turned.at(9, 16));
    CHECK_FALSE(turned.at(16, 9));
    RadarMap::Mask scaled(32, 32);
    RadarMap::FillRotatedBox(scaled, mapping, 2048, 2048, 0.0, 2.0, bounds);
    CHECK(Count(scaled) == 8 * 32);
}

TEST_CASE("radar map: thinning leaves a connected one-pixel centre line")
{
    auto mask = MaskFrom({
        "..............",
        ".############.",
        ".############.",
        ".############.",
        ".############.",
        ".############.",
        "..............",
    });
    RadarMap::Thin(mask);
    // Every column of the bar keeps one pixel, all in the middle row (thinning shortens the ends a little).
    int middle = 0;
    for (int x = 4; x <= 9; ++x) {
        int column = 0;
        for (int y = 0; y < mask.height; ++y) {
            column += mask.at(x, y);
        }
        CHECK(column == 1);
        middle += mask.at(x, 3);
    }
    CHECK(middle == 6);
}

TEST_CASE("radar map: pruning cuts short stubs and specks but keeps the road")
{
    auto mask = MaskFrom({
        "....................",
        "....#...............",
        "....#...............",
        "####################",
        "....................",
        "..............##....",
        "....................",
    });
    RadarMap::Prune(mask, 4, 5);
    CHECK_FALSE(mask.at(4, 1));  // the two-pixel stub
    CHECK_FALSE(mask.at(14, 5)); // the speck
    for (int x = 2; x < 18; ++x) {
        CHECK(mask.at(x, 3));
    }
}

TEST_CASE("radar map: a ragged road comes out as a clean line of one width")
{
    // A road footprint that wobbles between 2 and 5 pixels thick.
    RadarMap::Mask ragged(60, 20);
    for (int x = 5; x < 55; ++x) {
        const int half = 1 + (x * 7 % 5) / 2;
        for (int y = 10 - half; y <= 10 + half; ++y) {
            ragged.at(x, y) = 1;
        }
    }
    const auto smooth = RadarMap::SmoothLines(ragged, 4.0);
    // Away from the ends, every column is the same thickness, around the centre row.
    int first = -1;
    for (int x = 12; x < 48; ++x) {
        int thickness = 0;
        for (int y = 0; y < smooth.height; ++y) {
            thickness += smooth.at(x, y);
        }
        if (first < 0) {
            first = thickness;
        }
        CHECK(thickness == first);
        CHECK(smooth.at(x, 10));
    }
    CHECK(first >= 3);
    CHECK(first <= 5);
}
