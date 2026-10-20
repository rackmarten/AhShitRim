#include <doctest/doctest.h>

#include "../../cpp/txd.h"

#include <string>

using namespace AhShitRim;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    void Put32(Bytes& a_out, std::uint32_t a_value)
    {
        for (int i = 0; i < 4; i++) {
            a_out.push_back(static_cast<std::uint8_t>(a_value >> (i * 8)));
        }
    }

    void Put16(Bytes& a_out, std::uint16_t a_value)
    {
        a_out.push_back(static_cast<std::uint8_t>(a_value));
        a_out.push_back(static_cast<std::uint8_t>(a_value >> 8));
    }

    Bytes Chunk(std::uint32_t a_type, const Bytes& a_body)
    {
        Bytes out;
        Put32(out, a_type);
        Put32(out, static_cast<std::uint32_t>(a_body.size()));
        Put32(out, 0x1803FFFF);  // San Andreas's RenderWare version
        out.insert(out.end(), a_body.begin(), a_body.end());
        return out;
    }

    struct Native
    {
        std::string   name;
        std::uint32_t platform = Txd::kPlatformD3D9;
        std::uint32_t raster = Txd::kRaster8888;
        std::uint32_t format = TextureDecode::kA8R8G8B8;  // Direct3D 8: has alpha
        std::uint16_t width = 1, height = 1;
        std::uint8_t  last = 1;  // Direct3D 9 flags (1 = alpha), Direct3D 8 DXT number
        Bytes         palette;
        Bytes         texels;
    };

    Bytes NativeChunk(const Native& a_native)
    {
        Bytes body;
        Put32(body, a_native.platform);
        Put32(body, 0x1106);  // filter and addressing
        char name[32] = {}, mask[32] = {};
        a_native.name.copy(name, 31);
        body.insert(body.end(), name, name + 32);
        body.insert(body.end(), mask, mask + 32);
        Put32(body, a_native.raster);
        Put32(body, a_native.format);
        Put16(body, a_native.width);
        Put16(body, a_native.height);
        body.push_back(32);  // depth
        body.push_back(1);   // levels
        body.push_back(4);   // raster type: texture
        body.push_back(a_native.last);
        body.insert(body.end(), a_native.palette.begin(), a_native.palette.end());
        Put32(body, static_cast<std::uint32_t>(a_native.texels.size()));
        body.insert(body.end(), a_native.texels.begin(), a_native.texels.end());
        Bytes out = Chunk(Txd::kChunkStruct, body);
        const Bytes extension = Chunk(Txd::kChunkExtension, {});
        out.insert(out.end(), extension.begin(), extension.end());
        return Chunk(Txd::kChunkTextureNative, out);
    }

    Bytes Dictionary(const std::vector<Native>& a_natives)
    {
        Bytes body;
        Put16(body, static_cast<std::uint16_t>(a_natives.size()));
        Put16(body, 2);  // device: Direct3D 9
        Bytes inner = Chunk(Txd::kChunkStruct, body);
        for (const auto& native : a_natives) {
            const Bytes chunk = NativeChunk(native);
            inner.insert(inner.end(), chunk.begin(), chunk.end());
        }
        const Bytes extension = Chunk(Txd::kChunkExtension, {});
        inner.insert(inner.end(), extension.begin(), extension.end());
        return Chunk(Txd::kChunkTexDictionary, inner);
    }

    Bytes Pixel(const Txd::Texture& a_texture, std::uint32_t a_x, std::uint32_t a_y)
    {
        const auto at = (a_y * a_texture.width + a_x) * 4;
        return { a_texture.rgba[at], a_texture.rgba[at + 1], a_texture.rgba[at + 2], a_texture.rgba[at + 3] };
    }

    // A DXT1 4x4 block all one 565 colour.
    Bytes SolidDxt1(std::uint16_t a_colour) { return { static_cast<std::uint8_t>(a_colour), static_cast<std::uint8_t>(a_colour >> 8), static_cast<std::uint8_t>(a_colour), static_cast<std::uint8_t>(a_colour >> 8), 0, 0, 0, 0 }; }
}

TEST_CASE("A Direct3D 9 dictionary reads every texture by name, in order")
{
    Native argb{ "Fist", Txd::kPlatformD3D9, Txd::kRaster8888, TextureDecode::kA8R8G8B8, 2, 1, 1, {}, { 10, 20, 30, 40, 1, 2, 3, 255 } };
    Native dxt{ "font1", Txd::kPlatformD3D9, 0x0300, TextureDecode::kDxt1, 4, 4, 9, {}, SolidDxt1(0xF800) };
    const Bytes data = Dictionary({ argb, dxt });

    std::uint32_t skipped = 99;
    const auto    textures = Txd::Read(data.data(), data.size(), &skipped);
    REQUIRE(textures.size() == 2);
    CHECK(skipped == 0);
    CHECK(textures[0].name == "Fist");
    CHECK(textures[0].width == 2);
    CHECK(textures[0].height == 1);
    CHECK(Pixel(textures[0], 0, 0) == Bytes{ 30, 20, 10, 40 });
    CHECK(Pixel(textures[0], 1, 0) == Bytes{ 3, 2, 1, 255 });
    CHECK(Pixel(textures[1], 3, 3) == Bytes{ 255, 0, 0, 255 });

    // RenderWare finds textures whatever their case.
    CHECK(Txd::Find(textures, "fist") == &textures[0]);
    CHECK(Txd::Find(textures, "FONT1") == &textures[1]);
    CHECK(Txd::Find(textures, "font2") == nullptr);
}

TEST_CASE("Direct3D 8 textures take their format from the DXT number or the raster format")
{
    // DXT3: explicit 4-bit alpha (all 0x8 = 136), then a colour block.
    Bytes dxt3(8, 0x88);
    const Bytes colour = SolidDxt1(0x07E0);
    dxt3.insert(dxt3.end(), colour.begin(), colour.end());
    Native compressed{ "a", Txd::kPlatformD3D8, 0x0300, 1, 4, 4, 3, {}, dxt3 };
    // No DXT number: 565 from the raster format.
    Native plain{ "b", Txd::kPlatformD3D8, Txd::kRaster565, 0, 1, 1, 0, {}, { 0x1F, 0x00 } };
    const Bytes data = Dictionary({ compressed, plain });

    const auto textures = Txd::Read(data.data(), data.size());
    REQUIRE(textures.size() == 2);
    CHECK(Pixel(textures[0], 0, 0) == Bytes{ 0, 255, 0, 136 });
    CHECK(Pixel(textures[1], 0, 0) == Bytes{ 0, 0, 255, 255 });
}

TEST_CASE("A palettized texture looks its texels up in the palette after the header")
{
    Bytes palette(256 * 4, 0);
    palette[5 * 4 + 0] = 200;  // entry 5: red 200, alpha 128
    palette[5 * 4 + 3] = 128;
    Native native{ "pal", Txd::kPlatformD3D9, Txd::kRasterPal8 | Txd::kRaster8888, TextureDecode::kP8, 2, 1, 1, palette, { 5, 0 } };
    const Bytes data = Dictionary({ native });

    const auto textures = Txd::Read(data.data(), data.size());
    REQUIRE(textures.size() == 1);
    CHECK(Pixel(textures[0], 0, 0) == Bytes{ 200, 0, 0, 128 });
    CHECK(Pixel(textures[0], 1, 0) == Bytes{ 0, 0, 0, 0 });
}

TEST_CASE("Textures that don't decode are skipped and counted; anything else reads as nothing")
{
    Native good{ "good", Txd::kPlatformD3D9, Txd::kRaster8888, TextureDecode::kA8R8G8B8, 1, 1, 1, {}, { 1, 2, 3, 4 } };
    Native ps2{ "ps2", 0x00325350, Txd::kRaster8888, TextureDecode::kA8R8G8B8, 1, 1, 1, {}, { 1, 2, 3, 4 } };
    Native short_{ "short", Txd::kPlatformD3D9, 0x0300, TextureDecode::kDxt1, 8, 8, 9, {}, SolidDxt1(0) };  // needs 4 blocks
    const Bytes data = Dictionary({ ps2, good, short_ });

    std::uint32_t skipped = 0;
    const auto    textures = Txd::Read(data.data(), data.size(), &skipped);
    REQUIRE(textures.size() == 1);
    CHECK(textures[0].name == "good");
    CHECK(skipped == 2);

    // A cut-off file reads as nothing rather than running past its end.
    const Bytes one = Dictionary({ good, good });
    const auto  cut = Txd::Read(one.data(), one.size() - 20);
    CHECK(cut.empty());

    Bytes notTxd = data;
    notTxd[0] = 0x10;  // a clump
    CHECK(Txd::Read(notTxd.data(), notTxd.size()).empty());
    CHECK(Txd::Read(nullptr, 0).empty());
}
