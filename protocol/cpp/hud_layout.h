// Where San Andreas's HUD and text go on Skyrim's screen. San Andreas lays its HUD out on a 640 x
// 448 screen stretched over a 4:3 one; here that layout keeps its 4:3 proportions on any screen,
// each piece held to the edge San Andreas holds it to (the HUD to the top right, the help box to
// the top left, big messages to the middle), so 16:9, 16:10 and 21:9 show it as 4:3 does and
// nothing is stretched. Positions and styles are gta-reversed's CHud ones. Header-only and
// game-free, so the protocol tests cover it.

#pragma once

#include "hud_font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace AhShitRim::HudLayout
{
    inline constexpr float kWidth = 640.0f;
    inline constexpr float kHeight = 448.0f;

    // A screen in pixels and its pixels per San Andreas HUD unit across and down. The units are
    // those of a 4:3 screen as high as this one (or as wide, on a screen narrower than 4:3).
    struct Screen
    {
        float width = 0.0f;
        float height = 0.0f;
        float unitX = 0.0f;
        float unitY = 0.0f;
    };

    inline Screen ScreenOf(float a_width, float a_height)
    {
        const float height43 = (std::min)(a_height, a_width * 0.75f);
        return { a_width, a_height, height43 / 480.0f, height43 / kHeight };
    }

    enum class Edge : std::uint8_t
    {
        Start,   // left, or top
        Middle,
        End,     // right, or bottom
    };

    // A HUD x (0..640) on the screen, held to the left edge, the middle or the right edge.
    inline float X(const Screen& a_screen, float a_x, Edge a_edge)
    {
        switch (a_edge) {
        case Edge::Start: return a_x * a_screen.unitX;
        case Edge::Middle: return a_screen.width / 2.0f + (a_x - kWidth / 2.0f) * a_screen.unitX;
        default: return a_screen.width - (kWidth - a_x) * a_screen.unitX;
        }
    }

    // A HUD y (0..448) on the screen, held to the top, the middle or the bottom.
    inline float Y(const Screen& a_screen, float a_y, Edge a_edge)
    {
        switch (a_edge) {
        case Edge::Start: return a_y * a_screen.unitY;
        case Edge::Middle: return a_screen.height / 2.0f + (a_y - kHeight / 2.0f) * a_screen.unitY;
        default: return a_screen.height - (kHeight - a_y) * a_screen.unitY;
        }
    }

    struct Rect
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    // A HUD box (top left corner and size in HUD units) on the screen.
    inline Rect Box(const Screen& a_screen, float a_x, float a_y, float a_width, float a_height, Edge a_edgeX, Edge a_edgeY)
    {
        const float x = X(a_screen, a_x, a_edgeX);
        const float y = Y(a_screen, a_y, a_edgeY);
        return { x, y, x + a_width * a_screen.unitX, y + a_height * a_screen.unitY };
    }

    struct Colour
    {
        std::uint8_t r = 0, g = 0, b = 0, a = 255;
    };

    // CHudColours.
    inline constexpr Colour kRed{ 180, 25, 29, 255 };
    inline constexpr Colour kGreen{ 54, 104, 44, 255 };
    inline constexpr Colour kLightGray{ 225, 225, 225, 255 };
    inline constexpr Colour kGold{ 144, 98, 16, 255 };
    inline constexpr Colour kBlack{ 0, 0, 0, 255 };

    inline Colour WithAlpha(Colour a_colour, float a_alpha)
    {
        a_colour.a = static_cast<std::uint8_t>(std::clamp(a_alpha, 0.0f, 1.0f) * a_colour.a + 0.5f);
        return a_colour;
    }

    // ---- The HUD, top right (CHud::DrawPlayerInfo) ----

    inline Rect WeaponIcon(const Screen& a_screen) { return Box(a_screen, 497.0f, 20.0f, 47.0f, 58.0f, Edge::End, Edge::Start); }
    inline Rect ArmourBar(const Screen& a_screen) { return Box(a_screen, 546.0f, 48.0f, 62.0f, 9.0f, Edge::End, Edge::Start); }
    // San Andreas widens it with the max health stat; CJ's starting width.
    inline Rect HealthBar(const Screen& a_screen) { return Box(a_screen, 546.0f, 77.0f, 62.0f, 9.0f, Edge::End, Edge::Start); }

    // A bar chart (CSprite2d::DrawBarChart with its border): a black border one unit round it, the
    // filled part from the left in the colour, the rest in the colour at half brightness.
    struct Bar
    {
        Rect   border, full, empty;
        Colour fullColour, emptyColour;
    };

    inline Bar BarOf(const Screen& a_screen, const Rect& a_rect, float a_fraction, Colour a_colour)
    {
        const float fraction = std::clamp(std::isfinite(a_fraction) ? a_fraction : 0.0f, 0.0f, 1.0f);
        const float split = a_rect.x0 + (a_rect.x1 - a_rect.x0) * fraction;
        Bar         bar;
        bar.border = { a_rect.x0 - a_screen.unitX, a_rect.y0 - a_screen.unitY, a_rect.x1 + a_screen.unitX, a_rect.y1 + a_screen.unitY };
        bar.full = { a_rect.x0, a_rect.y0, split, a_rect.y1 };
        bar.empty = { split, a_rect.y0, a_rect.x1, a_rect.y1 };
        bar.fullColour = a_colour;
        bar.emptyColour = { static_cast<std::uint8_t>(a_colour.r / 2), static_cast<std::uint8_t>(a_colour.g / 2), static_cast<std::uint8_t>(a_colour.b / 2), a_colour.a };
        return bar;
    }

    // ---- Text styles ----

    // One of San Andreas's text styles: its CFont settings and where it goes, in HUD units.
    struct TextStyle
    {
        HudFont::Style style = HudFont::Style::Gothic;
        float          scaleX = 1.0f, scaleY = 1.0f;  // CFont::SetScale before the screen's stretch
        bool           proportional = true;
        int            edge = 0;
        HudFont::Align align = HudFont::Align::Left;
        float          x = 0.0f, y = 0.0f;
        Edge           edgeX = Edge::Start, edgeY = Edge::Start;
        bool           fromBottom = false;  // y is the last line's bottom (PrintStringFromBottom)
        float          wrap = 0.0f;  // line width, 0 = none
        Colour         colour = kLightGray;
    };

    inline constexpr TextStyle kClock{ HudFont::Style::Pricedown, 0.55f, 1.1f, false, 2, HudFont::Align::Right, 608.0f, 22.0f, Edge::End, Edge::Start, false, 0.0f, kLightGray };
    inline constexpr TextStyle kMoney{ HudFont::Style::Pricedown, 0.55f, 1.1f, false, 2, HudFont::Align::Right, 608.0f, 89.0f, Edge::End, Edge::Start, false, 0.0f, kGreen };
    // The star is font2's ']' cell, so Gothic (Pricedown's ']' is a bracket).
    inline constexpr TextStyle kWantedStar{ HudFont::Style::Gothic, 0.6f, 1.21f, true, 1, HudFont::Align::Right, 611.0f, 114.0f, Edge::End, Edge::Start, false, 0.0f, kGold };
    // CHud::DrawMissionTitle: bottom right, gold.
    inline constexpr TextStyle kMissionTitle{ HudFont::Style::Pricedown, 1.0f, 1.3f, true, 2, HudFont::Align::Right, 620.0f, 333.0f, Edge::End, Edge::End, true, 0.0f, kGold };
    // CHud::DrawBustedWastedMessage: "wasted" and "busted" in the middle.
    inline constexpr TextStyle kBigMessage{ HudFont::Style::Gothic, 2.1f, 2.1f, true, 3, HudFont::Align::Centre, 320.0f, 194.0f, Edge::Middle, Edge::Middle, true, 0.0f, kLightGray };
    // CHud::DrawSuccessFailedMessage: "mission passed!" in the middle.
    inline constexpr TextStyle kMissionPassed{ HudFont::Style::Pricedown, 1.0f, 1.6f, true, 2, HudFont::Align::Centre, 320.0f, 194.0f, Edge::Middle, Edge::Middle, true, 0.0f, kGold };
    // CHud::DrawHelpText: a dark box top left.
    inline constexpr TextStyle kHelp{ HudFont::Style::Subtitles, 0.52f, 1.1f, true, 0, HudFont::Align::Left, 36.0f, 24.0f, Edge::Start, Edge::Start, false, 192.0f, kLightGray };
    // CHud::DrawSubtitles: bottom centre, lines going up.
    inline constexpr TextStyle kSubtitles{ HudFont::Style::Subtitles, 0.58f, 1.2f, true, 1, HudFont::Align::Centre, 320.0f, 416.0f, Edge::Middle, Edge::End, true, 500.0f, kLightGray };

    inline constexpr float kHelpPadding = 4.0f;  // the help box round its text, HUD units
    inline constexpr Colour kHelpBox{ 0, 0, 0, 190 };
    inline constexpr float kStarSpacing = 19.0f;  // between wanted stars, HUD units
    inline constexpr int   kStarCount = 6;
    inline constexpr float kLetterboxHeight = 56.0f;  // each bar, HUD units of the full height

    inline HudFont::Params ParamsOf(const Screen& a_screen, const TextStyle& a_style)
    {
        HudFont::Params params;
        params.style = a_style.style;
        params.scaleX = a_style.scaleX * a_screen.unitX;
        params.scaleY = a_style.scaleY * a_screen.unitY;
        params.proportional = a_style.proportional;
        params.edge = a_style.edge;
        params.align = a_style.align;
        params.wrap = a_style.wrap * a_screen.unitX;
        return params;
    }

    // The outline's offset in pixels: San Andreas draws it a_style.edge pixels out on a 640-wide
    // screen.
    inline float OutlinePixels(const Screen& a_screen, const TextStyle& a_style) { return static_cast<float>(a_style.edge) * a_screen.unitX; }

    // Where a_text in a_style goes: x as its alignment needs it, y its first line's top.
    struct Placed
    {
        HudFont::Params params;
        float           x = 0.0f, y = 0.0f;
        int             lines = 0;
    };

    inline Placed Place(const HudFont::Values& a_values, const Screen& a_screen, const TextStyle& a_style, std::string_view a_text)
    {
        Placed placed;
        placed.params = ParamsOf(a_screen, a_style);
        placed.x = X(a_screen, a_style.x, a_style.edgeX);
        placed.y = Y(a_screen, a_style.y, a_style.edgeY);
        placed.lines = static_cast<int>(HudFont::Lines(a_values, placed.params, a_text).size());
        if (a_style.fromBottom) {
            placed.y -= HudFont::LineHeight(placed.params) * static_cast<float>(placed.lines);
        }
        return placed;
    }

    // The box behind help text: its widest line and its lines, padded.
    inline Rect HelpBox(const HudFont::Values& a_values, const Screen& a_screen, std::string_view a_text)
    {
        const auto  placed = Place(a_values, a_screen, kHelp, a_text);
        const float padX = kHelpPadding * a_screen.unitX, padY = kHelpPadding * a_screen.unitY;
        return { placed.x - padX, placed.y - padY, placed.x + placed.params.wrap + padX,
            placed.y + HudFont::LineHeight(placed.params) * static_cast<float>(placed.lines) + padY };
    }

    // The right edge of wanted star a_index (0 = the rightmost; San Andreas fills them from the right).
    inline float StarRight(const Screen& a_screen, int a_index) { return X(a_screen, kWantedStar.x - kStarSpacing * static_cast<float>(a_index), kWantedStar.edgeX); }

    inline Rect LetterboxTop(const Screen& a_screen) { return { 0.0f, 0.0f, a_screen.width, kLetterboxHeight / kHeight * a_screen.height }; }
    inline Rect LetterboxBottom(const Screen& a_screen) { return { 0.0f, a_screen.height - kLetterboxHeight / kHeight * a_screen.height, a_screen.width, a_screen.height }; }

    // ---- What the HUD says ----

    // Skyrim's hour of the day (0..24) as San Andreas's clock, "HH:MM".
    inline std::string ClockText(float a_hour)
    {
        float hour = std::isfinite(a_hour) ? std::fmod(a_hour, 24.0f) : 0.0f;
        if (hour < 0.0f) {
            hour += 24.0f;
        }
        int minutes = static_cast<int>(hour * 60.0f);
        minutes = std::clamp(minutes, 0, 24 * 60 - 1);
        char text[8];
        std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
        return text;
    }

    // Money as San Andreas shows it: "$00000123", or "-$0000123" in red when negative.
    inline std::string MoneyText(std::int64_t a_money)
    {
        const std::int64_t clamped = std::clamp<std::int64_t>(a_money, -9999999, 99999999);
        char               text[16];
        if (clamped < 0) {
            std::snprintf(text, sizeof(text), "-$%07lld", static_cast<long long>(-clamped));
        } else {
            std::snprintf(text, sizeof(text), "$%08lld", static_cast<long long>(clamped));
        }
        return text;
    }

    inline Colour MoneyColour(std::int64_t a_money) { return a_money < 0 ? kRed : kGreen; }
}
