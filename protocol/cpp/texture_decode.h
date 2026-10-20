// San Andreas's textures to RGBA8 for a SkinModel. The ASI locks the top level of each
// Direct3D 9 texture and hands its bytes here with the D3DFORMAT; header-only and free of game
// and Direct3D types, so the protocol tests cover it.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace AhShitRim::TextureDecode
{
    // The D3DFORMAT values San Andreas's textures come in.
    inline constexpr std::uint32_t kA8R8G8B8 = 21;
    inline constexpr std::uint32_t kX8R8G8B8 = 22;
    inline constexpr std::uint32_t kR5G6B5 = 23;
    inline constexpr std::uint32_t kX1R5G5B5 = 24;
    inline constexpr std::uint32_t kA1R5G5B5 = 25;
    inline constexpr std::uint32_t kA4R4G4B4 = 26;
    inline constexpr std::uint32_t kP8 = 41;
    inline constexpr std::uint32_t kL8 = 50;
    inline constexpr std::uint32_t kDxt1 = 0x31545844;  // MAKEFOURCC('D', 'X', 'T', '1')
    inline constexpr std::uint32_t kDxt3 = 0x33545844;
    inline constexpr std::uint32_t kDxt5 = 0x35545844;

    inline bool Supported(std::uint32_t a_format)
    {
        switch (a_format) {
        case kA8R8G8B8:
        case kX8R8G8B8:
        case kR5G6B5:
        case kX1R5G5B5:
        case kA1R5G5B5:
        case kA4R4G4B4:
        case kP8:
        case kL8:
        case kDxt1:
        case kDxt3:
        case kDxt5:
            return true;
        default:
            return false;
        }
    }

    inline bool Compressed(std::uint32_t a_format) { return a_format == kDxt1 || a_format == kDxt3 || a_format == kDxt5; }

    namespace Detail
    {
        inline std::uint8_t Expand(std::uint32_t a_value, int a_bits) { return static_cast<std::uint8_t>((a_value * 255 + ((1u << a_bits) - 1) / 2) / ((1u << a_bits) - 1)); }

        inline void Rgb565(std::uint16_t a_c, std::uint8_t* a_out)
        {
            a_out[0] = Expand((a_c >> 11) & 31, 5);
            a_out[1] = Expand((a_c >> 5) & 63, 6);
            a_out[2] = Expand(a_c & 31, 5);
            a_out[3] = 255;
        }

        inline std::uint16_t U16(const std::uint8_t* a_p) { return static_cast<std::uint16_t>(a_p[0] | (a_p[1] << 8)); }

        // A DXT colour block (8 bytes) into 4x4 RGBA pixels; a_dxt1 allows its transparent mode.
        inline void ColorBlock(const std::uint8_t* a_block, bool a_dxt1, std::uint8_t (&a_out)[16][4])
        {
            const std::uint16_t c0 = U16(a_block), c1 = U16(a_block + 2);
            std::uint8_t        palette[4][4];
            Rgb565(c0, palette[0]);
            Rgb565(c1, palette[1]);
            if (c0 > c1 || !a_dxt1) {
                for (int i = 0; i < 4; i++) {
                    palette[2][i] = static_cast<std::uint8_t>((2 * palette[0][i] + palette[1][i] + 1) / 3);
                    palette[3][i] = static_cast<std::uint8_t>((palette[0][i] + 2 * palette[1][i] + 1) / 3);
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    palette[2][i] = static_cast<std::uint8_t>((palette[0][i] + palette[1][i]) / 2);
                    palette[3][i] = 0;
                }
                palette[3][3] = 0;
            }
            const std::uint32_t bits = a_block[4] | (a_block[5] << 8) | (a_block[6] << 16) | (static_cast<std::uint32_t>(a_block[7]) << 24);
            for (int p = 0; p < 16; p++) {
                std::memcpy(a_out[p], palette[(bits >> (2 * p)) & 3], 4);
            }
        }
    }

    // a_pitch is the bytes per row (per row of 4x4 blocks for DXT); a_palette (P8 only) holds 256
    // PALETTEENTRY (red, green, blue, flags; RenderWare keeps the alpha in flags). Empty when the
    // format isn't one of the above or the data is short.
    inline std::vector<std::uint8_t> ToRgba(std::uint32_t a_format, std::uint32_t a_width, std::uint32_t a_height, const std::uint8_t* a_data,
        std::uint32_t a_pitch, const std::uint8_t* a_palette = nullptr)
    {
        if (!a_data || a_width == 0 || a_height == 0 || !Supported(a_format) || (a_format == kP8 && !a_palette)) {
            return {};
        }
        std::vector<std::uint8_t> out(static_cast<std::size_t>(a_width) * a_height * 4);
        auto                      pixel = [&](std::uint32_t x, std::uint32_t y) { return out.data() + (static_cast<std::size_t>(y) * a_width + x) * 4; };

        if (Compressed(a_format)) {
            const std::uint32_t blockBytes = a_format == kDxt1 ? 8 : 16;
            const std::uint32_t blocksX = (a_width + 3) / 4, blocksY = (a_height + 3) / 4;
            if (a_pitch < blocksX * blockBytes) {
                return {};
            }
            for (std::uint32_t by = 0; by < blocksY; by++) {
                for (std::uint32_t bx = 0; bx < blocksX; bx++) {
                    const std::uint8_t* block = a_data + static_cast<std::size_t>(by) * a_pitch + bx * blockBytes;
                    std::uint8_t        texels[16][4];
                    Detail::ColorBlock(block + (blockBytes - 8), a_format == kDxt1, texels);
                    if (a_format == kDxt3) {
                        for (int p = 0; p < 16; p++) {
                            texels[p][3] = Detail::Expand((block[p / 2] >> (4 * (p % 2))) & 15, 4);
                        }
                    } else if (a_format == kDxt5) {
                        const std::uint8_t a0 = block[0], a1 = block[1];
                        std::uint8_t       alpha[8] = { a0, a1 };
                        for (int i = 2; i < 8; i++) {
                            alpha[i] = a0 > a1 ? static_cast<std::uint8_t>(((8 - i) * a0 + (i - 1) * a1 + 3) / 7) :
                                       i < 6   ? static_cast<std::uint8_t>(((6 - i) * a0 + (i - 1) * a1 + 2) / 5) :
                                                 static_cast<std::uint8_t>(i == 6 ? 0 : 255);
                        }
                        std::uint64_t bits = 0;
                        for (int i = 0; i < 6; i++) {
                            bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
                        }
                        for (int p = 0; p < 16; p++) {
                            texels[p][3] = alpha[(bits >> (3 * p)) & 7];
                        }
                    }
                    for (int p = 0; p < 16; p++) {
                        const std::uint32_t x = bx * 4 + p % 4, y = by * 4 + p / 4;
                        if (x < a_width && y < a_height) {
                            std::memcpy(pixel(x, y), texels[p], 4);
                        }
                    }
                }
            }
            return out;
        }

        const std::uint32_t bytes = a_format == kA8R8G8B8 || a_format == kX8R8G8B8 ? 4 : a_format == kP8 || a_format == kL8 ? 1 : 2;
        if (a_pitch < a_width * bytes) {
            return {};
        }
        for (std::uint32_t y = 0; y < a_height; y++) {
            const std::uint8_t* row = a_data + static_cast<std::size_t>(y) * a_pitch;
            for (std::uint32_t x = 0; x < a_width; x++) {
                const std::uint8_t* in = row + x * bytes;
                std::uint8_t*       px = pixel(x, y);
                switch (a_format) {
                case kA8R8G8B8:
                case kX8R8G8B8:  // B, G, R, A in memory
                    px[0] = in[2];
                    px[1] = in[1];
                    px[2] = in[0];
                    px[3] = a_format == kA8R8G8B8 ? in[3] : 255;
                    break;
                case kR5G6B5:
                    Detail::Rgb565(Detail::U16(in), px);
                    break;
                case kX1R5G5B5:
                case kA1R5G5B5: {
                    const std::uint16_t c = Detail::U16(in);
                    px[0] = Detail::Expand((c >> 10) & 31, 5);
                    px[1] = Detail::Expand((c >> 5) & 31, 5);
                    px[2] = Detail::Expand(c & 31, 5);
                    px[3] = a_format == kA1R5G5B5 && (c & 0x8000) == 0 ? 0 : 255;
                    break;
                }
                case kA4R4G4B4: {
                    const std::uint16_t c = Detail::U16(in);
                    px[0] = Detail::Expand((c >> 8) & 15, 4);
                    px[1] = Detail::Expand((c >> 4) & 15, 4);
                    px[2] = Detail::Expand(c & 15, 4);
                    px[3] = Detail::Expand((c >> 12) & 15, 4);
                    break;
                }
                case kP8:
                    std::memcpy(px, a_palette + in[0] * 4, 4);
                    break;
                case kL8:
                    px[0] = px[1] = px[2] = in[0];
                    px[3] = 255;
                    break;
                default:
                    break;
                }
            }
        }
        return out;
    }

    // Whether any pixel is less than opaque (the mesh then gets alpha testing).
    inline bool HasAlpha(const std::vector<std::uint8_t>& a_rgba)
    {
        for (std::size_t i = 3; i < a_rgba.size(); i += 4) {
            if (a_rgba[i] < 255) {
                return true;
            }
        }
        return false;
    }
}
