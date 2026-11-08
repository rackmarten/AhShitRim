// San Andreas's text, as its CFont lays it out: the letter widths from data/fonts.dat and each
// letter's cell in the font textures (models/fonts.txd), so Skyrim draws San Andreas's text styles
// with its own fonts. Header-only and game-free, so the protocol tests cover it.
//
// The four styles share two textures, 16 cells across and 12.8 rows down (32 x 40 texels on a
// 512 x 512 texture): Gothic and Menu draw from font2, Subtitles and Pricedown from font1. Menu and
// Pricedown are subfonts, their letters further down the same texture (gta-reversed's
// CFont::FindSubFontCharacter); both draw lower case as capitals.

#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::HudFont
{
    enum class Style : std::uint8_t
    {
        Gothic = 0,  // San Andreas's eFontStyle order
        Subtitles = 1,
        Menu = 2,
        Pricedown = 3,
    };

    enum class Align : std::uint8_t
    {
        Left,
        Centre,
        Right,
    };

    inline constexpr std::size_t kPropCount = 208;  // 26 rows of 8 in fonts.dat
    inline constexpr int         kFontCount = 2;
    inline constexpr const char* kTextureNames[kFontCount] = { "font2", "font1" };  // by fonts.dat font id

    // San Andreas's font units: a letter is drawn 32 wide and 20 high (16 for the cells in the last,
    // cut-off row) times the scale; a line is 18 high.
    inline constexpr float kCellWidth = 32.0f;
    inline constexpr float kCellHeight = 20.0f;
    inline constexpr float kShortCellHeight = 16.0f;
    inline constexpr float kLineHeight = 18.0f;

    struct FontValues
    {
        std::array<std::uint8_t, kPropCount> prop{};  // each letter's width (proportional)
        std::uint8_t                         unprop = 0;  // every letter's width otherwise
        bool                                 loaded = false;
    };
    using Values = std::array<FontValues, kFontCount>;

    // Reads data/fonts.dat: per [FONT_ID], 26 lines of 8 widths after [PROP] and one after
    // [UNPROP]. True when both fonts were read in full.
    inline bool Parse(std::string_view a_text, Values& a_out)
    {
        a_out = {};
        std::vector<std::string_view> lines;
        for (std::size_t at = 0; at <= a_text.size();) {
            std::size_t end = a_text.find('\n', at);
            if (end == std::string_view::npos) {
                end = a_text.size();
            }
            auto line = a_text.substr(at, end - at);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1);
            }
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                line.remove_suffix(1);
            }
            if (!line.empty() && line.front() != '#') {
                lines.push_back(line);
            }
            at = end + 1;
        }
        auto number = [](std::string_view a_line, int& a_value) {
            const std::string text(a_line);
            char*             end = nullptr;
            a_value = static_cast<int>(std::strtol(text.c_str(), &end, 10));
            return end != text.c_str();
        };
        int font = -1;
        for (std::size_t i = 0; i < lines.size(); i++) {
            const auto line = lines[i];
            int        value = 0;
            if (line.starts_with("[FONT_ID]") && i + 1 < lines.size() && number(lines[i + 1], value)) {
                font = value >= 0 && value < kFontCount ? value : -1;
                i++;
            } else if (line.starts_with("[UNPROP]") && i + 1 < lines.size() && font >= 0 && number(lines[i + 1], value)) {
                a_out[font].unprop = static_cast<std::uint8_t>(value);
                i++;
            } else if (line.starts_with("[PROP]") && font >= 0) {
                if (i + 26 >= lines.size()) {
                    return false;
                }
                for (std::size_t row = 0; row < 26; row++) {
                    const std::string text(lines[i + 1 + row]);
                    const char*       at = text.c_str();
                    for (std::size_t column = 0; column < 8; column++) {
                        char*      end = nullptr;
                        const long width = std::strtol(at, &end, 10);
                        if (end == at) {
                            return false;
                        }
                        a_out[font].prop[row * 8 + column] = static_cast<std::uint8_t>(width);
                        at = end;
                    }
                }
                a_out[font].loaded = true;
                i += 26;
            }
        }
        return a_out[0].loaded && a_out[1].loaded && a_out[0].unprop > 0 && a_out[1].unprop > 0;
    }

    inline int FontOf(Style a_style) { return a_style == Style::Gothic || a_style == Style::Menu ? 0 : 1; }

    // gta-reversed's CFont::FindSubFontCharacter: where a subfont keeps a letter (a_subFont 1 =
    // Pricedown, 2 = Menu). Letter ids count from the space (character - 0x20).
    inline std::uint8_t SubFontLetter(std::uint8_t a_id, int a_subFont)
    {
        if (a_subFont == 1) {
            switch (a_id) {
            case 1: return 208;
            case 4: return 93;
            case 7: return 206;
            case 8:
            case 9: return static_cast<std::uint8_t>(a_id + 86);
            case 14: return 207;
            case 26: return 154;
            default: break;
            }
        }
        if (a_id == 6) return 10;
        if (a_id >= 16 && a_id <= 25) return static_cast<std::uint8_t>(a_id + 128);
        if (a_id == 31) return 91;
        if (a_id >= 33 && a_id <= 58) return static_cast<std::uint8_t>(a_id + 122);
        if (a_id == 62) return 32;
        if (a_id >= 65 && a_id <= 90) return static_cast<std::uint8_t>(a_id + 90);
        if (a_id >= 96 && a_id <= 118) return static_cast<std::uint8_t>(a_id + 85);
        if (a_id >= 119 && a_id <= 140) return static_cast<std::uint8_t>(a_id + 62);
        if (a_id >= 141 && a_id <= 142) return 204;
        if (a_id == 143) return 205;
        return a_id;
    }

    // The cell (and width index) a character draws from in a_style's texture; 0 is the space.
    inline std::uint8_t Letter(unsigned char a_char, Style a_style)
    {
        std::uint8_t id = a_char < 0x20 ? 0 : static_cast<std::uint8_t>(a_char - 0x20);
        if (id == '?') {
            id = 0;  // San Andreas's own rule: id 63 ('_') draws as a space
        }
        if (a_style == Style::Pricedown) {
            return SubFontLetter(id, 1);
        }
        if (a_style == Style::Menu) {
            return SubFontLetter(id, 2);
        }
        if (id == 145) {
            return '@';
        }
        return id >= kPropCount ? 0 : id;
    }

    struct Params
    {
        Style style = Style::Gothic;
        float scaleX = 1.0f;  // pixels per font unit across (CFont's scale, already on the screen)
        float scaleY = 1.0f;  // and down
        bool  proportional = true;
        int   edge = 0;  // CFont::SetEdge: widens every letter by this many font units
        Align align = Align::Left;
        float wrap = 0.0f;  // line width in pixels for word wrapping; 0 = no wrapping
    };

    // A letter's quad: screen pixels, and texture coordinates (0..1) in its style's texture.
    struct Glyph
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        char  token = 0;  // the ~x~ colour code in force for it (~y~ gives 'y'), 0 before any
    };

    inline float Advance(const Values& a_values, const Params& a_params, std::uint8_t a_letter)
    {
        const auto& font = a_values[FontOf(a_params.style)];
        // Past the table (Pricedown's '!') San Andreas reads the fixed width, stored right after it.
        const float width = a_params.proportional && a_letter < kPropCount ? font.prop[a_letter] : font.unprop;
        return (width + static_cast<float>(a_params.edge)) * a_params.scaleX;
    }

    inline float LineHeight(const Params& a_params) { return kLineHeight * a_params.scaleY; }

    // a_line's width in pixels; San Andreas's ~x~ codes take no room.
    inline float Width(const Values& a_values, const Params& a_params, std::string_view a_line)
    {
        float width = 0.0f;
        for (std::size_t i = 0; i < a_line.size(); i++) {
            if (a_line[i] == '~') {
                const auto close = a_line.find('~', i + 1);
                i = close == std::string_view::npos ? a_line.size() : close;
                continue;
            }
            width += Advance(a_values, a_params, Letter(static_cast<unsigned char>(a_line[i]), a_params.style));
        }
        return width;
    }

    // a_text in lines: ~n~ breaks a line, and with a wrap width the words go on the next line once
    // the line is full (a single word wider than the line stays whole).
    inline std::vector<std::string> Lines(const Values& a_values, const Params& a_params, std::string_view a_text)
    {
        std::vector<std::string> out;
        std::size_t              at = 0;
        while (true) {
            const auto        brk = a_text.find("~n~", at);
            const std::string_view paragraph = a_text.substr(at, brk == std::string_view::npos ? std::string_view::npos : brk - at);
            std::string       line;
            std::size_t       word = 0;
            while (word <= paragraph.size()) {
                auto end = paragraph.find(' ', word);
                if (end == std::string_view::npos) {
                    end = paragraph.size();
                }
                const auto  next = paragraph.substr(word, end - word);
                std::string candidate = line.empty() ? std::string(next) : line + ' ' + std::string(next);
                if (a_params.wrap > 0.0f && !line.empty() && Width(a_values, a_params, candidate) > a_params.wrap) {
                    out.push_back(line);
                    line = std::string(next);
                } else {
                    line = std::move(candidate);
                }
                word = end + 1;
            }
            out.push_back(line);
            if (brk == std::string_view::npos) {
                break;
            }
            at = brk + 3;
        }
        return out;
    }

    // a_text's letters, the first line's top at a_y and each line placed at a_x by a_params.align
    // (its left edge, middle or right edge). Spaces and ~x~ codes draw nothing; each glyph has the colour code in force.
    inline std::vector<Glyph> Layout(const Values& a_values, const Params& a_params, std::string_view a_text, float a_x, float a_y)
    {
        std::vector<Glyph> out;
        float              top = a_y;
        char               token = 0;  // a colour code goes on to the next line, as San Andreas's does
        for (const auto& line : Lines(a_values, a_params, a_text)) {
            const float width = Width(a_values, a_params, line);
            float       x = a_params.align == Align::Left ? a_x : a_params.align == Align::Centre ? a_x - width / 2.0f : a_x - width;
            for (std::size_t i = 0; i < line.size(); i++) {
                if (line[i] == '~') {
                    const auto close = line.find('~', i + 1);
                    if (close == i + 2) {
                        token = line[i + 1];
                    }
                    i = close == std::string::npos ? line.size() : close;
                    continue;
                }
                const std::uint8_t letter = Letter(static_cast<unsigned char>(line[i]), a_params.style);
                if (letter != 0) {
                    const bool cut = letter >= 0xC0;  // the last row is cut short by the texture's bottom
                    // Past the bottom the texture wraps, as San Andreas samples it: Pricedown's '!'
                    // (208) is in the top left cell, the space's, which nothing else draws.
                    float row = static_cast<float>(letter >> 4) / 12.8f;
                    if (row >= 1.0f) {
                        row -= 1.0f;
                    }
                    Glyph       glyph;
                    glyph.x0 = x;
                    glyph.y0 = top;
                    glyph.x1 = x + kCellWidth * a_params.scaleX;
                    glyph.y1 = top + (cut ? kShortCellHeight : kCellHeight) * a_params.scaleY;
                    glyph.u0 = static_cast<float>(letter & 15) / 16.0f;
                    glyph.u1 = glyph.u0 + 0.0615f;
                    glyph.v0 = row + 0.0021f;
                    glyph.v1 = row + (cut ? 0.0625f : 0.078125f);
                    glyph.token = token;
                    out.push_back(glyph);
                }
                x += Advance(a_values, a_params, letter);
            }
            top += LineHeight(a_params);
        }
        return out;
    }
}
