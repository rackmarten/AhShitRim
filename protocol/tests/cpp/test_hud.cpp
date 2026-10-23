#include <doctest/doctest.h>

#include "../../cpp/hud_layout.h"
#include "../../cpp/txd.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace AhShitRim;

namespace
{
    // A fonts.dat as San Andreas ships it: comments, tabs, trailing comments and CRLF. Font 0's
    // widths count up from 0, font 1's from 100.
    std::string FontsDat(int a_rows = 26)
    {
        std::string out = "####\r\n# fonts.dat\r\n####\r\n\r\n[TOTAL_FONTS]\r\n2\r\n";
        for (int font = 0; font < 2; font++) {
            out += "\r\n# a font\r\n[FONT_ID]\r\n" + std::to_string(font) + "\r\n\r\n# proportional values:\r\n[PROP]\r\n";
            for (int row = 0; row < a_rows; row++) {
                for (int column = 0; column < 8; column++) {
                    out += std::to_string((font * 100 + row * 8 + column) % 256) + (column == 7 ? "\t  #\t" + std::to_string(row * 8) + " ! \" $\r\n" : "  ");
                }
            }
            out += "\r\n[UNPROP]\r\n" + std::to_string(font == 0 ? 27 : 20) + "\r\n\r\n[REPLACEMENT_SPACE_CHAR]\r\n10\r\n";
        }
        return out;
    }

    HudFont::Values Values()
    {
        HudFont::Values values;
        REQUIRE(HudFont::Parse(FontsDat(), values));
        return values;
    }
}

TEST_CASE("fonts.dat gives both fonts' letter widths and fixed width")
{
    const auto values = Values();
    CHECK(values[0].prop[0] == 0);
    CHECK(values[0].prop[33] == 33);
    CHECK(values[0].prop[207] == 207);
    CHECK(values[1].prop[0] == 100);
    CHECK(values[1].prop[155] == (100 + 155) % 256);
    CHECK(values[0].unprop == 27);
    CHECK(values[1].unprop == 20);

    HudFont::Values short_;
    CHECK_FALSE(HudFont::Parse(FontsDat(25), short_));
    CHECK_FALSE(HudFont::Parse("", short_));
}

TEST_CASE("Letters map to their cells: Gothic and Subtitles directly, Pricedown and Menu through the subfont")
{
    using HudFont::Letter;
    using HudFont::Style;
    CHECK(Letter('A', Style::Gothic) == 33);
    CHECK(Letter('a', Style::Subtitles) == 65);
    CHECK(Letter(' ', Style::Gothic) == 0);
    CHECK(Letter('_', Style::Gothic) == 0);  // San Andreas draws it as a space
    // Pricedown: capitals for both cases, digits and ':' in the subfont rows, '$' at 93.
    CHECK(Letter('A', Style::Pricedown) == 155);
    CHECK(Letter('a', Style::Pricedown) == 155);
    CHECK(Letter('0', Style::Pricedown) == 144);
    CHECK(Letter('9', Style::Pricedown) == 153);
    CHECK(Letter(':', Style::Pricedown) == 154);
    CHECK(Letter('$', Style::Pricedown) == 93);
    CHECK(Letter('!', Style::Pricedown) == 208);
    // ':' only moves in Pricedown.
    CHECK(Letter(':', Style::Menu) == 26);
    CHECK(Letter('Z', Style::Menu) == 180);
}

TEST_CASE("Widths add each letter's width plus the edge, times the scale")
{
    const auto      values = Values();
    HudFont::Params params;
    params.style = HudFont::Style::Gothic;
    params.scaleX = 2.0f;
    CHECK(HudFont::Width(values, params, "AB") == doctest::Approx((33 + 34) * 2.0f));
    params.edge = 3;
    CHECK(HudFont::Width(values, params, "AB") == doctest::Approx((33 + 3 + 34 + 3) * 2.0f));
    params.proportional = false;
    CHECK(HudFont::Width(values, params, "AB") == doctest::Approx((27 + 3) * 2 * 2.0f));
    // ~x~ codes take no room.
    CHECK(HudFont::Width(values, params, "~r~A~w~B") == doctest::Approx((27 + 3) * 2 * 2.0f));
}

TEST_CASE("Layout puts each letter in its cell, aligned, and draws no spaces")
{
    const auto      values = Values();
    HudFont::Params params;
    params.style = HudFont::Style::Gothic;
    params.scaleX = 0.5f;
    params.scaleY = 2.0f;
    const auto glyphs = HudFont::Layout(values, params, "A B", 100.0f, 50.0f);
    REQUIRE(glyphs.size() == 2);
    // 'A' is letter 33: column 1, row 2 of 12.8; 32 x 20 font units.
    CHECK(glyphs[0].x0 == doctest::Approx(100.0f));
    CHECK(glyphs[0].y0 == doctest::Approx(50.0f));
    CHECK(glyphs[0].x1 == doctest::Approx(116.0f));
    CHECK(glyphs[0].y1 == doctest::Approx(90.0f));
    CHECK(glyphs[0].u0 == doctest::Approx(1.0f / 16.0f));
    CHECK(glyphs[0].v0 == doctest::Approx(2.0f / 12.8f + 0.0021f));
    CHECK(glyphs[0].v1 == doctest::Approx(3.0f / 12.8f));
    // 'B' after 'A' and the space.
    CHECK(glyphs[1].x0 == doctest::Approx(100.0f + (33 + 0) * 0.5f));

    params.align = HudFont::Align::Right;
    const auto right = HudFont::Layout(values, params, "AB", 100.0f, 0.0f);
    CHECK(right[0].x0 == doctest::Approx(100.0f - (33 + 34) * 0.5f));
    params.align = HudFont::Align::Centre;
    const auto centre = HudFont::Layout(values, params, "AB", 100.0f, 0.0f);
    CHECK(centre[0].x0 == doctest::Approx(100.0f - (33 + 34) * 0.25f));

    // Letters in the texture's cut-off last rows draw 16 units high.
    params.style = HudFont::Style::Pricedown;
    params.align = HudFont::Align::Left;
    const auto bang = HudFont::Layout(values, params, "!", 0.0f, 0.0f);
    REQUIRE(bang.size() == 1);
    CHECK(bang[0].y1 == doctest::Approx(16.0f * 2.0f));
    // Pricedown's '!' (208) is past the bottom and wraps to the top row, as in San Andreas.
    CHECK(bang[0].v0 == doctest::Approx(13.0f / 12.8f - 1.0f + 0.0021f));
    CHECK(bang[0].v1 <= 1.0f);
    CHECK(HudFont::Advance(values, params, 208) == doctest::Approx(20.0f * 0.5f));  // the fixed width
}

TEST_CASE("Text wraps at the wrap width and breaks at ~n~")
{
    const auto      values = Values();
    HudFont::Params params;
    params.style = HudFont::Style::Gothic;
    params.proportional = false;  // 27 per letter
    params.wrap = 27.0f * 7;
    const auto lines = HudFont::Lines(values, params, "ab cd ef gh~n~ij");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "ab cd");
    CHECK(lines[1] == "ef gh");
    CHECK(lines[2] == "ij");
    CHECK(HudFont::Lines(values, params, "ab~n~~n~cd").size() == 3);  // an empty line between
    // Without a wrap width only ~n~ breaks; a word wider than the line stays whole.
    params.wrap = 0.0f;
    CHECK(HudFont::Lines(values, params, "ab cd ef gh").size() == 1);
    params.wrap = 27.0f;
    CHECK(HudFont::Lines(values, params, "abcdef") == std::vector<std::string>{ "abcdef" });

    params.wrap = 27.0f * 7;
    params.scaleY = 1.0f;
    const auto glyphs = HudFont::Layout(values, params, "ab cd ef", 0.0f, 0.0f);
    REQUIRE(glyphs.size() == 6);
    CHECK(glyphs[4].y0 == doctest::Approx(18.0f));
    CHECK(glyphs[4].x0 == doctest::Approx(0.0f));
}

TEST_CASE("The HUD keeps 4:3 proportions on 16:9, 16:10 and 21:9, held to its edges")
{
    using namespace HudLayout;
    for (const auto& [width, height] : { std::pair{ 1920.0f, 1080.0f }, std::pair{ 2560.0f, 1080.0f }, std::pair{ 1440.0f, 1080.0f } }) {
        const auto screen = ScreenOf(width, height);
        CHECK(screen.unitX == doctest::Approx(2.25f));
        CHECK(screen.unitY == doctest::Approx(1080.0f / 448.0f));
        // Square pixels: one HUD unit across is 448/480 of one down, as on a 4:3 screen.
        CHECK(screen.unitX / screen.unitY == doctest::Approx(448.0f / 480.0f));
        // The clock's right edge is 32 units in from the screen's right edge, at any width.
        CHECK(X(screen, 608.0f, Edge::End) == doctest::Approx(width - 72.0f));
        CHECK(X(screen, 32.0f, Edge::Start) == doctest::Approx(72.0f));
        CHECK(X(screen, 320.0f, Edge::Middle) == doctest::Approx(width / 2.0f));
        CHECK(Y(screen, 448.0f, Edge::End) == doctest::Approx(1080.0f));
        CHECK(Y(screen, 224.0f, Edge::Middle) == doctest::Approx(540.0f));
    }
    const auto wide = ScreenOf(1680.0f, 1050.0f);
    CHECK(wide.unitX == doctest::Approx(1050.0f / 480.0f));
    // Narrower than 4:3 (5:4): the units come from the width.
    const auto narrow = ScreenOf(1280.0f, 1024.0f);
    CHECK(narrow.unitX == doctest::Approx(2.0f));
    CHECK(X(narrow, 640.0f, Edge::End) == doctest::Approx(1280.0f));
    CHECK(X(narrow, 0.0f, Edge::Start) == doctest::Approx(0.0f));
}

TEST_CASE("The HUD pieces sit top right at 1080p, the bars filled from the left")
{
    using namespace HudLayout;
    const auto screen = ScreenOf(1920.0f, 1080.0f);
    const auto uy = 1080.0f / 448.0f;

    const auto health = HealthBar(screen);
    CHECK(health.x0 == doctest::Approx(1920.0f - 94.0f * 2.25f));
    CHECK(health.x1 == doctest::Approx(1920.0f - 32.0f * 2.25f));
    CHECK(health.y0 == doctest::Approx(77.0f * uy));
    CHECK(health.y1 == doctest::Approx(86.0f * uy));
    CHECK(ArmourBar(screen).y0 == doctest::Approx(48.0f * uy));
    const auto icon = WeaponIcon(screen);
    CHECK(icon.x1 - icon.x0 == doctest::Approx(47.0f * 2.25f));
    CHECK(icon.y1 - icon.y0 == doctest::Approx(58.0f * uy));

    const auto bar = BarOf(screen, health, 0.25f, kRed);
    CHECK(bar.full.x1 == doctest::Approx(health.x0 + (health.x1 - health.x0) * 0.25f));
    CHECK(bar.empty.x0 == doctest::Approx(bar.full.x1));
    CHECK(bar.empty.x1 == doctest::Approx(health.x1));
    CHECK(bar.border.x0 == doctest::Approx(health.x0 - 2.25f));
    CHECK(bar.emptyColour.r == 90);
    CHECK(BarOf(screen, health, 3.0f, kRed).full.x1 == doctest::Approx(health.x1));
    CHECK(BarOf(screen, health, -1.0f, kRed).full.x1 == doctest::Approx(health.x0));
    CHECK(BarOf(screen, health, std::nanf(""), kRed).full.x1 == doctest::Approx(health.x0));

    // The star is font2's ']' (Gothic); font1's is a plain bracket.
    CHECK(HudFont::FontOf(kWantedStar.style) == 0);
    CHECK(HudFont::Letter(']', kWantedStar.style) == 61);
    CHECK(StarRight(screen, 0) == doctest::Approx(1920.0f - 29.0f * 2.25f));
    CHECK(StarRight(screen, 1) == doctest::Approx(1920.0f - 48.0f * 2.25f));
    CHECK(LetterboxTop(screen).y1 == doctest::Approx(1080.0f * 56.0f / 448.0f));
    CHECK(LetterboxBottom(screen).y0 == doctest::Approx(1080.0f - 1080.0f * 56.0f / 448.0f));
}

TEST_CASE("Text styles place their text: clock top right, big messages up from the middle")
{
    using namespace HudLayout;
    const auto values = Values();
    const auto screen = ScreenOf(1920.0f, 1080.0f);
    const auto uy = 1080.0f / 448.0f;

    const auto clock = Place(values, screen, kClock, "12:00");
    CHECK(clock.x == doctest::Approx(1920.0f - 72.0f));
    CHECK(clock.y == doctest::Approx(22.0f * uy));
    CHECK(clock.params.scaleX == doctest::Approx(0.55f * 2.25f));
    CHECK(clock.params.scaleY == doctest::Approx(1.1f * uy));
    CHECK_FALSE(clock.params.proportional);

    // From the bottom: the last line ends 30 units above the middle.
    const auto wasted = Place(values, screen, kBigMessage, "wasted");
    CHECK(wasted.x == doctest::Approx(960.0f));
    CHECK(wasted.y + 18.0f * 2.1f * uy == doctest::Approx(540.0f - 30.0f * uy));

    const auto title = Place(values, screen, kMissionTitle, "Big Smoke");
    CHECK(title.x == doctest::Approx(1920.0f - 20.0f * 2.25f));
    CHECK(title.y + 18.0f * 1.3f * uy == doctest::Approx(1080.0f - 115.0f * uy));

    // Help text wraps inside its box.
    const std::string help = "Press F to get into a vehicle, and F again to get out of it. Hold Space for the handbrake.";
    const auto        placed = Place(values, screen, kHelp, help);
    CHECK(placed.lines > 1);
    const auto box = HelpBox(values, screen, help);
    CHECK(box.x0 == doctest::Approx(32.0f * 2.25f));
    CHECK(box.x1 == doctest::Approx(232.0f * 2.25f));
    CHECK(box.y1 - box.y0 == doctest::Approx(placed.lines * 18.0f * 1.1f * uy + 8.0f * uy));

    // Subtitles grow upwards from the bottom.
    const auto one = Place(values, screen, kSubtitles, "Ah");
    const auto two = Place(values, screen, kSubtitles, "Ah~n~sh");
    REQUIRE(one.lines == 1);
    CHECK(two.y == doctest::Approx(one.y - 18.0f * 1.2f * uy));
    CHECK(OutlinePixels(screen, kBigMessage) == doctest::Approx(3.0f * 2.25f));
    // Subtitles have a drop shadow down and right, no outline.
    CHECK(OutlinePixels(screen, kSubtitles) == 0.0f);
    const auto [sx, sy] = ShadowPixels(screen, kSubtitles);
    CHECK(sx == doctest::Approx(2.0f * 2.25f));
    CHECK(sy == doctest::Approx(2.0f * uy));
    CHECK(ShadowPixels(screen, kHelp).first == 0.0f);
}

TEST_CASE("The clock and money read as San Andreas shows them")
{
    using namespace HudLayout;
    CHECK(ClockText(0.0f) == "00:00");
    CHECK(ClockText(13.5f) == "13:30");
    CHECK(ClockText(23.999f) == "23:59");
    CHECK(ClockText(25.25f) == "01:15");
    CHECK(ClockText(-1.0f) == "23:00");
    CHECK(ClockText(std::nanf("")) == "00:00");

    CHECK(MoneyText(0) == "$00000000");
    CHECK(MoneyText(1234) == "$00001234");
    CHECK(MoneyText(-45) == "-$0000045");
    CHECK(MoneyText(5'000'000'000LL) == "$99999999");
    CHECK(MoneyText(-5'000'000'000LL) == "-$9999999");
    CHECK(MoneyColour(-1).r == kRed.r);
    CHECK(MoneyColour(0).g == kGreen.g);
}

// The player's own files, never in the repository: scripts/test.ps1 passes SaPath from local.props
// as AHSR_SA_PATH, and without it this checks nothing.
TEST_CASE("The San Andreas install's fonts.dat, fonts.txd and hud.txd read in full")
{
    const char* sa = std::getenv("AHSR_SA_PATH");
    if (!sa || !*sa) {
        MESSAGE("AHSR_SA_PATH not set, skipped");
        return;
    }
    auto load = [&](const char* a_file) {
        std::ifstream file(std::filesystem::path(sa) / a_file, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
    };

    const auto      dat = load("data/fonts.dat");
    HudFont::Values values;
    REQUIRE(HudFont::Parse(std::string_view(reinterpret_cast<const char*>(dat.data()), dat.size()), values));
    CHECK(values[0].unprop == 27);
    CHECK(values[1].unprop == 20);
    CHECK(values[1].prop[HudFont::Letter('A', HudFont::Style::Pricedown)] > 0);

    std::uint32_t skipped = 99;
    const auto    fontsData = load("models/fonts.txd");
    const auto    fonts = Txd::Read(fontsData.data(), fontsData.size(), &skipped);
    CHECK(skipped == 0);
    for (const char* name : HudFont::kTextureNames) {
        const auto* font = Txd::Find(fonts, name);
        REQUIRE(font);
        CHECK(font->width == 512);
        CHECK(font->height == 512);
        // White letters on clear: the 'A' cell of each has both.
        int solid = 0, clear = 0;
        for (std::uint32_t y = 2 * 40; y < 3 * 40; y++) {
            for (std::uint32_t x = 32; x < 64; x++) {
                const auto alpha = font->rgba[(y * 512 + x) * 4 + 3];
                solid += alpha > 200;
                clear += alpha < 20;
            }
        }
        CHECK(solid > 50);
        CHECK(clear > 50);
    }

    const auto hudData = load("models/hud.txd");
    const auto hud = Txd::Read(hudData.data(), hudData.size(), &skipped);
    CHECK(skipped == 0);
    const auto* fist = Txd::Find(hud, "fist");
    REQUIRE(fist);
    CHECK(fist->width == 64);
}

TEST_CASE("the scopes and crosshairs are four quarters round the middle, the others mirrored")
{
    using namespace HudLayout;
    const auto screen = ScreenOf(1920.0f, 1080.0f);
    const auto sniper = SniperScope(screen);
    CHECK(sniper[0].rect.x1 == doctest::Approx(960.0f));  // the top left ends at the middle
    CHECK(sniper[0].rect.y1 == doctest::Approx(540.0f));
    CHECK(sniper[0].rect.x0 == doctest::Approx(960.0f - 210.0f * screen.unitX));
    CHECK(sniper[1].rect.x0 == doctest::Approx(960.0f));  // the top right starts there
    CHECK(sniper[3].rect.y0 == doctest::Approx(540.0f));
    CHECK_FALSE(sniper[0].flipU);
    CHECK_FALSE(sniper[0].flipV);
    CHECK(sniper[1].flipU);
    CHECK_FALSE(sniper[1].flipV);
    CHECK_FALSE(sniper[2].flipU);
    CHECK(sniper[2].flipV);
    CHECK(sniper[3].flipU);
    CHECK(sniper[3].flipV);

    const auto rocket = RocketSight(screen);  // 20 units out from the middle
    CHECK(rocket[0].rect.x1 == doctest::Approx(960.0f - 20.0f * screen.unitX));
    CHECK(rocket[1].rect.x0 == doctest::Approx(960.0f + 20.0f * screen.unitX));

    const auto aim = AimSight(screen, 0.5f, 0.4f, 1.0f);  // where San Andreas aims, a point above the middle
    CHECK(aim[0].rect.x1 == doctest::Approx(960.0f));  // the quarters meet at the point, making a ring
    CHECK(aim[1].rect.x0 == doctest::Approx(960.0f));
    CHECK(aim[0].rect.y1 == doctest::Approx(1080.0f * 0.4f));
    CHECK(aim[2].rect.y0 == doctest::Approx(1080.0f * 0.4f));
    CHECK((aim[0].rect.y1 - aim[0].rect.y0) == doctest::Approx(32.0f * screen.unitY));
    // Through Skyrim's frustum: with the same 4:3 field of view the point is the fractions of the middle 4:3,
    // with a narrower one (a field of view meant across 16:9) it sits higher, where the shots go.
    const float half = std::tan(70.0f * 3.14159265f / 360.0f);
    const auto  same = AimPoint(screen, 0.5f, 0.4f, 70.0f, half * 16.0f / 9.0f / (4.0f / 3.0f), half * 0.75f);
    CHECK(same.first == doctest::Approx(960.0f));
    CHECK(same.second == doctest::Approx(1080.0f * 0.4f));
    const auto narrow = AimPoint(screen, 0.5f, 0.4f, 70.0f, half, half * 9.0f / 16.0f);
    CHECK(narrow.second < same.second);
    const auto at = AimSightAt(screen, 900.0f, 400.0f, 0.2f);
    CHECK(at[0].rect.x1 == doctest::Approx(900.0f));
    CHECK(at[0].rect.y1 == doctest::Approx(400.0f));
    // Off the middle across, by the middle 4:3's width, not the whole screen's.
    const auto right = AimSight(screen, 0.53f, 0.4f, 0.2f);
    CHECK(right[0].rect.x1 == doctest::Approx(960.0f + 0.03f * 640.0f * screen.unitX));
    CHECK((right[0].rect.x1 - right[0].rect.x0) == doctest::Approx(64.0f * 0.2f / 2.0f * screen.unitX));

    CHECK(kAmmo.align == HudFont::Align::Centre);
    CHECK(kAmmo.colour.b == 241);
    CHECK(CameraFinder(screen)[0].rect.x0 == doctest::Approx(960.0f - 256.0f * screen.unitX));
}
