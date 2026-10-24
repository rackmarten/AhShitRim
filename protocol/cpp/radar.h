// San Andreas's radar in Skyrim's HUD (#900), the game-free half: the circle bottom left in San
// Andreas's spot and size, turning with the camera, zooming out at speed, the map inside it and the
// blips on it, pinned to the rim when they're further than it reaches. All of it is gta-reversed's
// CRadar (TransformRealWorldPointToRadarSpace, LimitRadarPoint, TransformRadarPointToScreenSpace,
// DrawMap's range, ShowRadarTraceWithHeight, DrawBlips) and CHud::DrawRadar (the disc). Header-only
// and free of game types, so the protocol tests cover it; skse/src/hud/SaHud.cpp draws it.
//
// World points are Skyrim's (units, X east, Y north). Headings turn clockwise from north, as
// Skyrim's do. Radar space is San Andreas's: the camera's forward is up, the rim is the unit
// circle.

#pragma once

#include "hud_layout.h"
#include "pose.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::Radar
{
    using HudLayout::Colour;
    using HudLayout::Rect;
    using HudLayout::Screen;

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // ---- How far it reaches (CRadar::DrawMap) ----

    inline constexpr float kMinRange = 180.0f;  // metres from CJ to the rim, on foot and slow (RADAR_MIN_RANGE)
    inline constexpr float kMaxRange = 350.0f;  // fast, or flying (RADAR_MAX_RANGE)
    inline constexpr float kMinSpeed = 0.3f;    // San Andreas's speeds, metres a 1/50 s step: faster than this it zooms out
    inline constexpr float kMaxSpeed = 0.9f;    // and from this on it's all the way out
    inline constexpr float kStepsPerSecond = 50.0f;

    enum class Moving : std::uint8_t
    {
        OnFoot,
        Vehicle,
        Plane,  // San Andreas zooms a plane's radar by its height, not its speed
    };

    // Metres from CJ to the rim: on foot always the nearest, in a vehicle further the faster it goes, in
    // a plane by its height above the sea (a_altitude, metres).
    inline float RangeMeters(Moving a_moving, float a_speed, float a_altitude = 0.0f)
    {
        switch (a_moving) {
        case Moving::Vehicle: {
            const float speed = std::isfinite(a_speed) ? std::abs(a_speed) / kStepsPerSecond : 0.0f;
            if (speed < kMinSpeed) {
                return kMinRange;
            }
            if (speed >= kMaxSpeed) {
                return kMaxRange;
            }
            return (speed - kMinSpeed) * (850.0f / (kMinSpeed * 10.0f)) + kMinRange;
        }
        case Moving::Plane: {
            const float height = std::isfinite(a_altitude) ? a_altitude / 200.0f : 0.0f;
            if (height < kMinSpeed) {
                return kMaxRange - 10.0f;
            }
            if (height < kMaxSpeed) {
                return (height - kMinSpeed) / 60.0f + kMaxRange - 10.0f;
            }
            return kMaxRange;
        }
        default: return kMinRange;
        }
    }

    // ---- World to radar ----

    // Where the radar is looking from: CJ, the camera's heading (up on the radar) and the range in
    // Skyrim units.
    struct View
    {
        Vec2  centre;
        float heading = 0.0f;
        float range = kMinRange * Pose::kUnitsPerMeter;
    };

    // a_world in radar space: the camera's right along x, its forward along y, the rim at 1.
    inline Vec2 ToRadar(const View& a_view, Vec2 a_world)
    {
        const float dx = (a_world.x - a_view.centre.x) / a_view.range;
        const float dy = (a_world.y - a_view.centre.y) / a_view.range;
        const float s = std::sin(a_view.heading), c = std::cos(a_view.heading);
        return { dx * c - dy * s, dx * s + dy * c };
    }

    // The world point at a_radar.
    inline Vec2 ToWorld(const View& a_view, Vec2 a_radar)
    {
        const float s = std::sin(a_view.heading), c = std::cos(a_view.heading);
        return { a_view.centre.x + (a_radar.x * c + a_radar.y * s) * a_view.range, a_view.centre.y + (a_radar.y * c - a_radar.x * s) * a_view.range };
    }

    // a_radar pinned to the rim when it's outside it (LimitRadarPoint); its distance from the middle
    // before, 1 at the rim.
    inline float Limit(Vec2& a_radar)
    {
        const float distance = std::hypot(a_radar.x, a_radar.y);
        if (distance > 1.0f) {
            a_radar.x /= distance;
            a_radar.y /= distance;
        }
        return distance;
    }

    // ---- On the screen (TransformRadarPointToScreenSpace, CHud::DrawRadar) ----

    // The circle's box: 94 x 76 HUD units, 40 in from the left and 104 up from the bottom to its top,
    // held to the bottom left. San Andreas's radar is that little wider than high on a 4:3 screen.
    inline Rect Area(const Screen& a_screen)
    {
        return HudLayout::Box(a_screen, 40.0f, HudLayout::kHeight - 104.0f, 94.0f, 76.0f, HudLayout::Edge::Start, HudLayout::Edge::End);
    }

    inline Vec2 ToScreen(const Screen& a_screen, Vec2 a_radar)
    {
        const auto area = Area(a_screen);
        const float halfW = (area.x1 - area.x0) / 2.0f, halfH = (area.y1 - area.y0) / 2.0f;
        return { area.x0 + halfW + a_radar.x * halfW, area.y0 + halfH - a_radar.y * halfH };
    }

    // The black ring round it: hud.txd's radardisc (a quarter of the ring, its middle bottom right)
    // four times, 4 units outside the circle, mirrored for the other three.
    inline std::array<HudLayout::Quarter, 4> Disc(const Screen& a_screen)
    {
        using HudLayout::Edge;
        const float left = HudLayout::X(a_screen, 36.0f, Edge::Start), middle = HudLayout::X(a_screen, 87.0f, Edge::Start), right = HudLayout::X(a_screen, 138.0f, Edge::Start);
        const float top = HudLayout::Y(a_screen, HudLayout::kHeight - 108.0f, Edge::End), centre = HudLayout::Y(a_screen, HudLayout::kHeight - 66.0f, Edge::End),
                    bottom = HudLayout::Y(a_screen, HudLayout::kHeight - 24.0f, Edge::End);
        return { { { { left, top, middle, centre }, false, false }, { { middle, top, right, centre }, true, false }, { { left, centre, middle, bottom }, false, true },
            { { middle, centre, right, bottom }, true, true } } };
    }

    // ---- The map ----

    // Where the map image is in Skyrim's world: the x of its left and right edges and the y of its top
    // (north) and bottom edges, in units.
    struct MapBounds
    {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

        bool Valid() const { return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom) && right != left && top != bottom; }
    };

    inline Vec2 UvOf(const MapBounds& a_map, Vec2 a_world) { return { (a_world.x - a_map.left) / (a_map.right - a_map.left), (a_map.top - a_world.y) / (a_map.top - a_map.bottom) }; }

    namespace Detail
    {
        // The number after "a_key": in a_json from a_from on; nullopt when there's none.
        inline std::optional<double> NumberAfter(std::string_view a_json, std::string_view a_key, std::size_t a_from = 0)
        {
            const auto quoted = std::string("\"") + std::string(a_key) + "\"";
            const auto at = a_json.find(quoted, a_from);
            if (at == std::string_view::npos) {
                return std::nullopt;
            }
            auto i = a_json.find(':', at + quoted.size());
            if (i == std::string_view::npos) {
                return std::nullopt;
            }
            i++;
            while (i < a_json.size() && (a_json[i] == ' ' || a_json[i] == '\t' || a_json[i] == '\r' || a_json[i] == '\n')) {
                i++;
            }
            const auto end = a_json.find_first_not_of("+-0123456789.eE", i);
            const auto text = std::string(a_json.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
            if (text.empty()) {
                return std::nullopt;
            }
            char*        rest = nullptr;
            const double value = std::strtod(text.c_str(), &rest);
            return rest && *rest == '\0' && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
        }
    }

    // The map's bounds from the radar map's JSON (#899's radar-map.json): its size in pixels, the world
    // units a pixel covers and the world point at its top left corner ("origin": { "x", "y" }).
    inline std::optional<MapBounds> ParseMapJson(std::string_view a_json)
    {
        const auto width = Detail::NumberAfter(a_json, "width");
        const auto height = Detail::NumberAfter(a_json, "height");
        const auto unitsPerPixel = Detail::NumberAfter(a_json, "unitsPerPixel");
        const auto origin = a_json.find("\"origin\"");
        if (!width || !height || !unitsPerPixel || origin == std::string_view::npos || *width <= 0.0 || *height <= 0.0 || *unitsPerPixel <= 0.0) {
            return std::nullopt;
        }
        const auto x = Detail::NumberAfter(a_json, "x", origin);
        const auto y = Detail::NumberAfter(a_json, "y", origin);
        if (!x || !y) {
            return std::nullopt;
        }
        const MapBounds bounds{ static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*x + *width * *unitsPerPixel),
            static_cast<float>(*y - *height * *unitsPerPixel) };
        return bounds.Valid() ? std::optional<MapBounds>(bounds) : std::nullopt;
    }

    // A corner of a shape on the screen: pixels and texture coordinates.
    struct Corner
    {
        float x = 0.0f, y = 0.0f, u = 0.0f, v = 0.0f;
    };

    inline constexpr int kSlices = 64;  // the circle's rim in this many straight pieces; the ring covers the edge

    // The inside of the circle as pairs of slices of it, each a quad (the middle and three corners on
    // the rim), the map image laid under it turned with the camera: as San Andreas draws its map's
    // tiles through the radar's mask. Without a map (a_map invalid) the coordinates are all 0.
    inline std::vector<std::array<Corner, 4>> Fill(const Screen& a_screen, const View& a_view, const MapBounds& a_map, int a_slices = kSlices)
    {
        const bool mapped = a_map.Valid();
        const auto corner = [&](Vec2 a_radar) {
            const auto at = ToScreen(a_screen, a_radar);
            const auto uv = mapped ? UvOf(a_map, ToWorld(a_view, a_radar)) : Vec2{};
            return Corner{ at.x, at.y, uv.x, uv.y };
        };
        const int slices = (std::max)(a_slices + (a_slices & 1), 4);  // even, two to a quad
        std::vector<std::array<Corner, 4>> out;
        out.reserve(static_cast<std::size_t>(slices / 2));
        const auto rim = [&](int a_index) {
            const float angle = 2.0f * Pose::kPi * static_cast<float>(a_index % slices) / static_cast<float>(slices);
            return corner({ std::sin(angle), std::cos(angle) });
        };
        const auto middle = corner({});
        for (int i = 0; i < slices; i += 2) {
            out.push_back({ middle, rim(i), rim(i + 1), rim(i + 2) });
        }
        return out;
    }

    // The colours under the map: San Andreas's sea past the map's edges, and the dark circle with only
    // the blips on it where there's no map (an interior, or a worldspace the map isn't of).
    inline constexpr Colour kSea{ 111, 137, 170, 255 };
    inline constexpr Colour kDark{ 0, 0, 0, 170 };
    inline constexpr Colour kNoMap{ 204, 204, 204, 255 };  // San Andreas's "off the map" grey: until the map is there

    // ---- Blips ----

    enum class Kind : std::uint8_t
    {
        Hostile,   // someone fighting CJ: a small red square
        Quest,     // what Skyrim's compass points to: a bigger square in San Andreas's destination colour
        Shop,      // Ammu-Nation: radar_ammugun, only while it's inside the circle
        Waypoint,  // the player's own marker on Skyrim's map: radar_waypoint, San Andreas's red target
    };

    // San Andreas's traces: a square, or a triangle pointing up (the target is above CJ) or down (below him).
    enum class Height : std::uint8_t
    {
        Level,
        Above,
        Below,
    };

    // More than 2 metres above CJ is above, more than 4 below is below (DrawCoordBlip).
    inline Height HeightOf(float a_dz)
    {
        if (a_dz > 2.0f * Pose::kUnitsPerMeter) {
            return Height::Above;
        }
        return a_dz >= -4.0f * Pose::kUnitsPerMeter ? Height::Level : Height::Below;
    }

    inline constexpr Colour kDestination{ 226, 192, 99, 255 };  // BLIP_COLOUR_DESTINATION: HUD_COLOUR_CREAM
    inline constexpr Colour kThreat = HudLayout::kRed;         // BLIP_COLOUR_THREAT

    // The sprites in hud.txd, by name as the file has them.
    inline constexpr std::string_view kCentreSprite = "radar_centre";  // CJ's arrow
    inline constexpr std::string_view kNorthSprite = "radar_north";
    inline constexpr std::string_view kWaypointSprite = "radar_waypoint";
    inline constexpr std::string_view kShopSprite = "radar_ammugun";
    inline constexpr std::string_view kDiscSprite = "radardisc";

    inline bool IsSprite(Kind a_kind) { return a_kind == Kind::Shop || a_kind == Kind::Waypoint; }

    // A trace's size (m_nBlipSize): half its width in HUD units, without the border.
    inline int TraceSize(Kind a_kind) { return a_kind == Kind::Quest ? 2 : 1; }

    // Where a blip at a_world goes on the screen: pinned to the rim when it's further away, or nowhere
    // for a short range one (a shop) out of reach, as San Andreas keeps its shops off the radar until
    // they're near.
    inline std::optional<Vec2> Place(const Screen& a_screen, const View& a_view, Kind a_kind, Vec2 a_world)
    {
        auto radar = ToRadar(a_view, a_world);
        if (!std::isfinite(radar.x) || !std::isfinite(radar.y)) {
            return std::nullopt;
        }
        if (Limit(radar) > 1.0f && a_kind == Kind::Shop) {
            return std::nullopt;
        }
        return ToScreen(a_screen, radar);
    }

    // Where the N goes: north of CJ, always on the rim (a point further north than the rim reaches).
    inline Vec2 North(const Screen& a_screen, const View& a_view)
    {
        auto radar = ToRadar(a_view, { a_view.centre.x, a_view.centre.y + a_view.range * 1.41421356f });
        Limit(radar);
        return ToScreen(a_screen, radar);
    }

    // A sprite blip's box (DrawRadarSprite): 8 units each way of a_at.
    inline Rect SpriteBox(const Screen& a_screen, Vec2 a_at)
    {
        const float w = std::floor(8.0f * a_screen.unitX), h = std::floor(8.0f * a_screen.unitY);
        return { a_at.x - w, a_at.y - h, a_at.x + w, a_at.y + h };
    }

    // A filled shape of four corners (a triangle has two the same), in order round it.
    struct Polygon
    {
        std::array<Vec2, 4> corners{};
        Colour              colour{};
    };

    // A trace (ShowRadarTraceWithHeight): the black border one unit out, then the shape in a_colour.
    inline std::array<Polygon, 2> Trace(const Screen& a_screen, Vec2 a_at, int a_size, Height a_height, Colour a_colour)
    {
        const float ux = a_screen.unitX, uy = a_screen.unitY, x = a_at.x, y = a_at.y;
        const auto  s = [&](int a_more) { return static_cast<float>(a_size + a_more); };
        const Colour border{ 0, 0, 0, a_colour.a };
        const auto   box = [&](float a_half, Colour a_fill) {
            return Polygon{ { { { x - a_half * ux, y - a_half * uy }, { x + a_half * ux, y - a_half * uy }, { x + a_half * ux, y + a_half * uy }, { x - a_half * ux, y + a_half * uy } } }, a_fill };
        };
        switch (a_height) {
        case Height::Above:  // pointing up
            return { Polygon{ { { { x, y - s(3) * uy }, { x + s(3) * ux, y + s(2) * uy }, { x - s(3) * ux, y + s(2) * uy }, { x, y - s(3) * uy } } }, border },
                Polygon{ { { { x, y - s(1) * uy }, { x + s(1) * ux, y + s(1) * uy }, { x - s(1) * ux, y + s(1) * uy }, { x, y - s(1) * uy } } }, a_colour } };
        case Height::Below:  // pointing down
            return { Polygon{ { { { x, y + s(3) * uy }, { x - s(3) * ux, y - s(2) * uy }, { x + s(3) * ux, y - s(2) * uy }, { x, y + s(3) * uy } } }, border },
                Polygon{ { { { x, y + s(1) * uy }, { x - s(1) * ux, y - s(1) * uy }, { x + s(1) * ux, y - s(1) * uy }, { x, y + s(1) * uy } } }, a_colour } };
        default: return { box(s(1), border), box(s(0), a_colour) };
        }
    }

    // CJ's arrow (DrawRotatingRadarSprite of radar_centre): in the middle, its corners 8 units out,
    // turned clockwise by a_turn radians (his heading less the camera's; the sprite points up). The
    // corners go top left, top right, bottom right, bottom left of the sprite.
    inline std::array<Vec2, 4> Arrow(const Screen& a_screen, float a_turn)
    {
        const auto        middle = ToScreen(a_screen, {});
        std::array<Vec2, 4> out{};
        for (int i = 0; i < 4; i++) {
            const float angle = a_turn + Pose::kPi * (static_cast<float>(i) * 0.5f - 0.25f);
            out[i] = { middle.x + std::sin(angle) * 8.0f * a_screen.unitX, middle.y - std::cos(angle) * 8.0f * a_screen.unitY };
        }
        return out;
    }
}
