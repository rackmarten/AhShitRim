#include <doctest/doctest.h>

#include "../../cpp/texture_decode.h"

using namespace AhShitRim::TextureDecode;

namespace
{
    std::vector<std::uint8_t> Pixel(const std::vector<std::uint8_t>& a_rgba, std::uint32_t a_width, std::uint32_t a_x, std::uint32_t a_y)
    {
        const auto at = (a_y * a_width + a_x) * 4;
        return { a_rgba[at], a_rgba[at + 1], a_rgba[at + 2], a_rgba[at + 3] };
    }

    using Rgba = std::vector<std::uint8_t>;
}

TEST_CASE("A8R8G8B8 is B, G, R, A in memory; X8R8G8B8 is opaque")
{
    const std::uint8_t data[8] = { 10, 20, 30, 40, 1, 2, 3, 4 };
    auto               rgba = ToRgba(kA8R8G8B8, 2, 1, data, 8);
    CHECK(Pixel(rgba, 2, 0, 0) == Rgba{ 30, 20, 10, 40 });
    rgba = ToRgba(kX8R8G8B8, 2, 1, data, 8);
    CHECK(Pixel(rgba, 2, 1, 0) == Rgba{ 3, 2, 1, 255 });
}

TEST_CASE("16-bit formats expand to full range, and rows follow the pitch")
{
    // Row pitch 8 for a 2-wide 565 texture: 4 bytes of padding per row.
    const std::uint8_t data[16] = { 0x00, 0xF8, 0xE0, 0x07, 0, 0, 0, 0, 0x1F, 0x00, 0xFF, 0xFF, 0, 0, 0, 0 };
    const auto         rgba = ToRgba(kR5G6B5, 2, 2, data, 8);
    CHECK(Pixel(rgba, 2, 0, 0) == Rgba{ 255, 0, 0, 255 });
    CHECK(Pixel(rgba, 2, 1, 0) == Rgba{ 0, 255, 0, 255 });
    CHECK(Pixel(rgba, 2, 0, 1) == Rgba{ 0, 0, 255, 255 });
    CHECK(Pixel(rgba, 2, 1, 1) == Rgba{ 255, 255, 255, 255 });

    const std::uint8_t argb1555[4] = { 0x00, 0x7C, 0x1F, 0x80 };  // red without alpha, blue with
    const auto         a1 = ToRgba(kA1R5G5B5, 2, 1, argb1555, 4);
    CHECK(Pixel(a1, 2, 0, 0) == Rgba{ 255, 0, 0, 0 });
    CHECK(Pixel(a1, 2, 1, 0) == Rgba{ 0, 0, 255, 255 });

    const std::uint8_t argb4444[2] = { 0x0F, 0x8F };  // A 8, R F, G 0, B F
    CHECK(Pixel(ToRgba(kA4R4G4B4, 1, 1, argb4444, 2), 1, 0, 0) == Rgba{ 255, 0, 255, 136 });
}

TEST_CASE("P8 looks pixels up in the palette, alpha from its flags")
{
    std::uint8_t palette[256 * 4]{};
    palette[5 * 4 + 0] = 200;
    palette[5 * 4 + 3] = 77;
    const std::uint8_t data[1] = { 5 };
    CHECK(Pixel(ToRgba(kP8, 1, 1, data, 1, palette), 1, 0, 0) == Rgba{ 200, 0, 0, 77 });
    CHECK(ToRgba(kP8, 1, 1, data, 1).empty());  // no palette
}

TEST_CASE("DXT1 decodes its four colours and its transparent mode")
{
    // c0 red, c1 blue (c0 > c1: four colours); pixel 0 = c0, 1 = c1, 2 = 2/3 red, 3 = 1/3 red.
    const std::uint8_t opaque[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0, 0, 0 };
    auto               rgba = ToRgba(kDxt1, 4, 4, opaque, 8);
    CHECK(Pixel(rgba, 4, 0, 0) == Rgba{ 255, 0, 0, 255 });
    CHECK(Pixel(rgba, 4, 1, 0) == Rgba{ 0, 0, 255, 255 });
    CHECK(Pixel(rgba, 4, 2, 0) == Rgba{ 170, 0, 85, 255 });
    CHECK(Pixel(rgba, 4, 3, 0) == Rgba{ 85, 0, 170, 255 });
    // c0 <= c1: index 3 is transparent black.
    const std::uint8_t cutout[8] = { 0x1F, 0x00, 0x00, 0xF8, 0xC0, 0, 0, 0 };
    rgba = ToRgba(kDxt1, 4, 4, cutout, 8);
    CHECK(Pixel(rgba, 4, 3, 0) == Rgba{ 0, 0, 0, 0 });
    CHECK(HasAlpha(rgba));
}

TEST_CASE("DXT3 and DXT5 carry their alpha ahead of the colour block, and a small texture keeps its size")
{
    std::uint8_t dxt3[16]{};
    dxt3[0] = 0xF0;  // pixel 0 alpha 0, pixel 1 alpha 15
    dxt3[8] = 0xFF;
    dxt3[9] = 0xFF;  // c0 white
    auto rgba = ToRgba(kDxt3, 2, 2, dxt3, 16);
    REQUIRE(rgba.size() == 2 * 2 * 4);
    CHECK(Pixel(rgba, 2, 0, 0) == Rgba{ 255, 255, 255, 0 });
    CHECK(Pixel(rgba, 2, 1, 0)[3] == 255);

    std::uint8_t dxt5[16]{};
    dxt5[0] = 200;
    dxt5[1] = 100;
    dxt5[2] = 0x08;  // pixel 0 index 0 (200), pixel 1 index 1 (100)
    dxt5[3] = 0x00;
    rgba = ToRgba(kDxt5, 4, 4, dxt5, 16);
    CHECK(Pixel(rgba, 4, 0, 0)[3] == 200);
    CHECK(Pixel(rgba, 4, 1, 0)[3] == 100);
    CHECK(Pixel(rgba, 4, 2, 0)[3] == 200);
}

TEST_CASE("unknown formats and short pitches give nothing")
{
    const std::uint8_t data[64]{};
    CHECK(ToRgba(999, 2, 2, data, 8).empty());
    CHECK(ToRgba(kA8R8G8B8, 4, 1, data, 8).empty());
    CHECK_FALSE(HasAlpha(ToRgba(kX8R8G8B8, 2, 2, data, 8)));
}
