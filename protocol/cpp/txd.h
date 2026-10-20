// RenderWare texture dictionaries (.txd) from the player's San Andreas install, read into RGBA8:
// models/fonts.txd and models/hud.txd for the HUD Skyrim draws. Only PC native textures
// (Direct3D 8 and 9); the texels go through TextureDecode. Header-only and game-free, so the
// protocol tests cover it.

#pragma once

#include "texture_decode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::Txd
{
    inline constexpr std::uint32_t kChunkStruct = 0x01;
    inline constexpr std::uint32_t kChunkExtension = 0x03;
    inline constexpr std::uint32_t kChunkTextureNative = 0x15;
    inline constexpr std::uint32_t kChunkTexDictionary = 0x16;

    inline constexpr std::uint32_t kPlatformD3D8 = 8;
    inline constexpr std::uint32_t kPlatformD3D9 = 9;

    // RenderWare raster format bits.
    inline constexpr std::uint32_t kRaster1555 = 0x0100;
    inline constexpr std::uint32_t kRaster565 = 0x0200;
    inline constexpr std::uint32_t kRaster4444 = 0x0300;
    inline constexpr std::uint32_t kRasterLum8 = 0x0400;
    inline constexpr std::uint32_t kRaster8888 = 0x0500;
    inline constexpr std::uint32_t kRaster888 = 0x0600;
    inline constexpr std::uint32_t kRaster555 = 0x0A00;
    inline constexpr std::uint32_t kRasterPal8 = 0x2000;
    inline constexpr std::uint32_t kRasterPal4 = 0x4000;

    // A native texture's struct up to its palette: platform, filter and addressing, name, mask,
    // raster format, then the platform's format fields, size, depth, levels, type and flags.
    inline constexpr std::size_t kNativeHeaderSize = 88;

    struct Texture
    {
        std::string               name;
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;  // width * height * 4, top row first
    };

    namespace Detail
    {
        inline std::uint32_t U32(const std::uint8_t* a_p) { return a_p[0] | (a_p[1] << 8) | (a_p[2] << 16) | (static_cast<std::uint32_t>(a_p[3]) << 24); }
        inline std::uint16_t U16(const std::uint8_t* a_p) { return static_cast<std::uint16_t>(a_p[0] | (a_p[1] << 8)); }

        struct Chunk
        {
            std::uint32_t       type = 0;
            const std::uint8_t* data = nullptr;
            std::size_t         size = 0;
        };

        // The chunk at a_at, or false when its header or body runs past a_end.
        inline bool ReadChunk(const std::uint8_t* a_at, const std::uint8_t* a_end, Chunk& a_out)
        {
            if (a_end - a_at < 12) {
                return false;
            }
            a_out.type = U32(a_at);
            a_out.size = U32(a_at + 4);
            a_out.data = a_at + 12;
            return static_cast<std::size_t>(a_end - a_out.data) >= a_out.size;
        }

        // The D3DFORMAT of a raster without one (Direct3D 8, or a Direct3D 9 texture that left it 0).
        inline std::uint32_t FormatOfRaster(std::uint32_t a_raster, bool a_alpha)
        {
            if (a_raster & kRasterPal8) {
                return TextureDecode::kP8;
            }
            switch (a_raster & 0x0F00) {
            case kRaster1555: return TextureDecode::kA1R5G5B5;
            case kRaster565: return TextureDecode::kR5G6B5;
            case kRaster4444: return TextureDecode::kA4R4G4B4;
            case kRasterLum8: return TextureDecode::kL8;
            case kRaster8888: return a_alpha ? TextureDecode::kA8R8G8B8 : TextureDecode::kX8R8G8B8;
            case kRaster888: return TextureDecode::kX8R8G8B8;
            case kRaster555: return TextureDecode::kX1R5G5B5;
            default: return 0;
            }
        }

        // One native texture's struct, its top level decoded. False when it isn't a PC texture this
        // reads, or its data is short.
        inline bool ReadNative(const Chunk& a_struct, Texture& a_out)
        {
            if (a_struct.size < kNativeHeaderSize) {
                return false;
            }
            const std::uint8_t* p = a_struct.data;
            const std::uint8_t* end = p + a_struct.size;
            const std::uint32_t platform = U32(p);
            if (platform != kPlatformD3D8 && platform != kPlatformD3D9) {
                return false;
            }
            const char* name = reinterpret_cast<const char*>(p + 8);
            a_out.name.assign(name, strnlen(name, 32));
            const std::uint32_t raster = U32(p + 72);
            std::uint32_t       format = 0;
            bool                alpha = false;
            const std::uint16_t width = U16(p + 80);
            const std::uint16_t height = U16(p + 82);
            const std::uint8_t  levels = p[85];
            const std::uint8_t  last = p[87];
            if (platform == kPlatformD3D9) {
                format = U32(p + 76);
                alpha = (last & 1) != 0;
            } else {
                alpha = U32(p + 76) != 0;
                // Direct3D 8 keeps the DXT number where Direct3D 9 keeps its flags.
                format = last == 1 ? TextureDecode::kDxt1 : last == 3 ? TextureDecode::kDxt3 : last == 5 ? TextureDecode::kDxt5 : 0;
            }
            if (!TextureDecode::Supported(format)) {
                format = FormatOfRaster(raster, alpha);
            }
            if (width == 0 || height == 0 || levels == 0 || !TextureDecode::Supported(format) || (raster & kRasterPal4)) {
                return false;
            }
            p += kNativeHeaderSize;
            const std::uint8_t* palette = nullptr;
            if (format == TextureDecode::kP8) {
                if (end - p < 256 * 4) {
                    return false;
                }
                palette = p;
                p += 256 * 4;
            }
            if (end - p < 4) {
                return false;
            }
            const std::uint32_t size = U32(p);
            p += 4;
            if (static_cast<std::size_t>(end - p) < size) {
                return false;
            }
            std::uint32_t pitch = 0;
            if (TextureDecode::Compressed(format)) {
                pitch = ((width + 3) / 4) * (format == TextureDecode::kDxt1 ? 8u : 16u);
                if (static_cast<std::size_t>(pitch) * ((height + 3) / 4) > size) {
                    return false;
                }
            } else {
                pitch = size / height;
            }
            a_out.width = width;
            a_out.height = height;
            a_out.rgba = TextureDecode::ToRgba(format, width, height, p, pitch, palette);
            return !a_out.rgba.empty();
        }
    }

    // Every texture of a dictionary that decodes, in its order. Empty when a_data isn't a texture
    // dictionary. a_skipped (optional) counts the native textures that didn't decode.
    inline std::vector<Texture> Read(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t* a_skipped = nullptr)
    {
        std::vector<Texture> out;
        std::uint32_t        skipped = 0;
        Detail::Chunk        root;
        if (a_data && Detail::ReadChunk(a_data, a_data + a_size, root) && root.type == kChunkTexDictionary) {
            const std::uint8_t* at = root.data;
            const std::uint8_t* end = root.data + root.size;
            Detail::Chunk       chunk;
            while (Detail::ReadChunk(at, end, chunk)) {
                at = chunk.data + chunk.size;
                if (chunk.type != kChunkTextureNative) {
                    continue;  // the dictionary's own struct and extension
                }
                Detail::Chunk inner;
                Texture       texture;
                if (Detail::ReadChunk(chunk.data, chunk.data + chunk.size, inner) && inner.type == kChunkStruct && Detail::ReadNative(inner, texture)) {
                    out.push_back(std::move(texture));
                } else {
                    skipped++;
                }
            }
        }
        if (a_skipped) {
            *a_skipped = skipped;
        }
        return out;
    }

    // The texture named a_name (case-insensitive, as RenderWare looks them up), or null.
    inline const Texture* Find(const std::vector<Texture>& a_textures, std::string_view a_name)
    {
        for (const auto& texture : a_textures) {
            if (texture.name.size() == a_name.size() &&
                std::equal(texture.name.begin(), texture.name.end(), a_name.begin(), [](char a, char b) {
                    return (a >= 'A' && a <= 'Z' ? a + 32 : a) == (b >= 'A' && b <= 'Z' ? b + 32 : b);
                })) {
                return &texture;
            }
        }
        return nullptr;
    }
}
