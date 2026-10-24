#include <doctest/doctest.h>

#include "../../cpp/radar.h"

using namespace AhShitRim;
using namespace AhShitRim::Radar;

namespace
{
    constexpr float kM = Pose::kUnitsPerMeter;
    constexpr float kQuarterTurn = Pose::kPi / 2.0f;

    const Screen kScreen = HudLayout::ScreenOf(1920.0f, 1080.0f);

    View At(float a_x, float a_y, float a_heading, float a_rangeMeters = kMinRange) { return { { a_x, a_y }, a_heading, a_rangeMeters * kM }; }
}

TEST_CASE("the radar reaches 180 m on foot and zooms out to 350 m at speed")
{
    CHECK(RangeMeters(Moving::OnFoot, 0.0f) == kMinRange);
    CHECK(RangeMeters(Moving::OnFoot, 80.0f) == kMinRange);  // only vehicles zoom
    CHECK(RangeMeters(Moving::Vehicle, 0.0f) == kMinRange);
    CHECK(RangeMeters(Moving::Vehicle, 14.9f) == kMinRange);  // under 0.3 m a step (54 km/h)
    CHECK(RangeMeters(Moving::Vehicle, 30.0f) == doctest::Approx(265.0f));  // half way: 0.6 m a step
    CHECK(RangeMeters(Moving::Vehicle, 44.9f) == doctest::Approx(kMaxRange).epsilon(0.01));
    CHECK(RangeMeters(Moving::Vehicle, 45.0f) == kMaxRange);
    CHECK(RangeMeters(Moving::Vehicle, 200.0f) == kMaxRange);
    CHECK(RangeMeters(Moving::Vehicle, -30.0f) == doctest::Approx(265.0f));  // reversing counts too
    CHECK(RangeMeters(Moving::Vehicle, std::nanf("")) == kMinRange);

    // It only grows with speed.
    float last = 0.0f;
    for (float speed = 0.0f; speed < 60.0f; speed += 0.5f) {
        const float range = RangeMeters(Moving::Vehicle, speed);
        CHECK(range >= last);
        last = range;
    }
}

TEST_CASE("a plane's radar zooms by its height")
{
    CHECK(RangeMeters(Moving::Plane, 0.0f, 0.0f) == kMaxRange - 10.0f);
    CHECK(RangeMeters(Moving::Plane, 100.0f, 50.0f) == kMaxRange - 10.0f);
    CHECK(RangeMeters(Moving::Plane, 0.0f, 120.0f) == doctest::Approx(kMaxRange - 10.0f).epsilon(0.001));
    CHECK(RangeMeters(Moving::Plane, 0.0f, 400.0f) == kMaxRange);
}

TEST_CASE("the camera's forward is up on the radar")
{
    const auto north = At(1000.0f, 2000.0f, 0.0f);
    auto       r = ToRadar(north, { 1000.0f, 2000.0f + 90.0f * kM });  // half the range ahead
    CHECK(r.x == doctest::Approx(0.0f));
    CHECK(r.y == doctest::Approx(0.5f));
    r = ToRadar(north, { 1000.0f + 180.0f * kM, 2000.0f });  // east is to the right
    CHECK(r.x == doctest::Approx(1.0f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5));

    // Looking east, east is up and north to the left.
    const auto east = At(0.0f, 0.0f, kQuarterTurn);
    r = ToRadar(east, { 90.0f * kM, 0.0f });
    CHECK(r.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(r.y == doctest::Approx(0.5f));
    r = ToRadar(east, { 0.0f, 90.0f * kM });
    CHECK(r.x == doctest::Approx(-0.5f));
    CHECK(r.y == doctest::Approx(0.0f).epsilon(1e-5));

    // Looking south, north is down.
    r = ToRadar(At(0.0f, 0.0f, Pose::kPi), { 0.0f, 90.0f * kM });
    CHECK(r.y == doctest::Approx(-0.5f));
}

TEST_CASE("the radar zooms out: the same point comes nearer the middle")
{
    const Vec2 ahead{ 0.0f, 100.0f * kM };
    CHECK(ToRadar(At(0.0f, 0.0f, 0.0f, kMinRange), ahead).y == doctest::Approx(100.0f / 180.0f));
    CHECK(ToRadar(At(0.0f, 0.0f, 0.0f, kMaxRange), ahead).y == doctest::Approx(100.0f / 350.0f));
}

TEST_CASE("radar to world turns back")
{
    const auto view = At(-12345.0f, 6789.0f, 2.1f, 260.0f);
    for (const Vec2 world : { Vec2{ -12000.0f, 7000.0f }, Vec2{ -20000.0f, -3000.0f }, Vec2{ -12345.0f, 6789.0f } }) {
        const auto back = ToWorld(view, ToRadar(view, world));
        CHECK(back.x == doctest::Approx(world.x).epsilon(1e-4));
        CHECK(back.y == doctest::Approx(world.y).epsilon(1e-4));
    }
}

TEST_CASE("far points are pinned to the rim, near ones stay")
{
    Vec2 near{ 0.3f, -0.4f };
    CHECK(Limit(near) == doctest::Approx(0.5f));
    CHECK(near.x == doctest::Approx(0.3f));
    CHECK(near.y == doctest::Approx(-0.4f));

    Vec2 far{ 3.0f, 4.0f };
    CHECK(Limit(far) == doctest::Approx(5.0f));
    CHECK(far.x == doctest::Approx(0.6f));
    CHECK(far.y == doctest::Approx(0.8f));
}

TEST_CASE("the radar sits bottom left in San Andreas's spot")
{
    // 1080 high: 2.25 pixels per HUD unit across, 2.41 down.
    const auto area = Area(kScreen);
    CHECK(area.x0 == doctest::Approx(40.0f * 1080.0f / 480.0f));
    CHECK(area.x1 == doctest::Approx(134.0f * 1080.0f / 480.0f));
    CHECK(area.y0 == doctest::Approx(1080.0f - 104.0f * 1080.0f / 448.0f));
    CHECK(area.y1 == doctest::Approx(1080.0f - 28.0f * 1080.0f / 448.0f));

    // Held to the left on a wider screen.
    const auto wide = Area(HudLayout::ScreenOf(2560.0f, 1080.0f));
    CHECK(wide.x0 == doctest::Approx(area.x0));
    CHECK(wide.y1 == doctest::Approx(area.y1));

    const auto middle = ToScreen(kScreen, {});
    CHECK(middle.x == doctest::Approx((area.x0 + area.x1) / 2.0f));
    CHECK(middle.y == doctest::Approx((area.y0 + area.y1) / 2.0f));
    const auto top = ToScreen(kScreen, { 0.0f, 1.0f });
    CHECK(top.y == doctest::Approx(area.y0));
    const auto right = ToScreen(kScreen, { 1.0f, 0.0f });
    CHECK(right.x == doctest::Approx(area.x1));
}

TEST_CASE("the ring's quarters meet in the radar's middle, mirrored")
{
    const auto disc = Disc(kScreen);
    const auto middle = ToScreen(kScreen, {});
    CHECK(disc[0].rect.x1 == doctest::Approx(middle.x));
    CHECK(disc[0].rect.y1 == doctest::Approx(middle.y));
    CHECK_FALSE(disc[0].flipU);
    CHECK_FALSE(disc[0].flipV);
    CHECK(disc[1].flipU);
    CHECK(disc[2].flipV);
    CHECK((disc[3].flipU && disc[3].flipV));
    CHECK(disc[3].rect.x0 == doctest::Approx(middle.x));
    CHECK(disc[3].rect.y0 == doctest::Approx(middle.y));
    const auto area = Area(kScreen);
    CHECK(disc[0].rect.x0 < area.x0);  // round the outside of the circle
    CHECK(disc[3].rect.y1 > area.y1);
}

TEST_CASE("the map fills the circle, turned with the camera")
{
    const MapBounds map{ -100000.0f, 100000.0f, 100000.0f, -100000.0f };  // 200000 units across, north up
    const auto      view = At(0.0f, 0.0f, kQuarterTurn);                   // looking east
    const auto      fill = Fill(kScreen, view, map, 8);
    REQUIRE(fill.size() == 4);
    const auto middle = ToScreen(kScreen, {});
    for (const auto& quad : fill) {
        CHECK(quad[0].x == doctest::Approx(middle.x));
        CHECK(quad[0].u == doctest::Approx(0.5f));
        CHECK(quad[0].v == doctest::Approx(0.5f));
    }
    CHECK(fill[1][0].x == fill[0][0].x);
    CHECK(fill.back()[3].x == doctest::Approx(fill[0][1].x));  // round to where it started
    CHECK(fill.back()[3].y == doctest::Approx(fill[0][1].y));

    // The rim's top is east of CJ: right of the image's middle by the range.
    const auto& top = fill[0][1];
    CHECK(top.y == doctest::Approx(Area(kScreen).y0));
    CHECK(top.u == doctest::Approx(0.5f + 180.0f * kM / 200000.0f));
    CHECK(top.v == doctest::Approx(0.5f));

    // Without a map the shape is there, the coordinates are 0.
    const auto plain = Fill(kScreen, view, MapBounds{}, 8);
    CHECK(plain[0][1].x == doctest::Approx(top.x));
    CHECK(plain[0][1].u == 0.0f);
}

TEST_CASE("height: up triangles above CJ, down below him, squares level")
{
    CHECK(HeightOf(0.0f) == Height::Level);
    CHECK(HeightOf(2.0f * kM) == Height::Level);
    CHECK(HeightOf(2.1f * kM) == Height::Above);
    CHECK(HeightOf(-4.0f * kM) == Height::Level);
    CHECK(HeightOf(-4.1f * kM) == Height::Below);

    const Vec2 at{ 200.0f, 900.0f };
    const auto level = Trace(kScreen, at, 2, Height::Level, kDestination);
    CHECK(level[0].colour.r == 0);
    CHECK(level[1].colour.r == kDestination.r);
    CHECK(level[1].corners[0].x == doctest::Approx(at.x - 2.0f * kScreen.unitX));
    CHECK(level[0].corners[2].y == doctest::Approx(at.y + 3.0f * kScreen.unitY));

    const auto up = Trace(kScreen, at, 2, Height::Above, kDestination);
    CHECK(up[1].corners[0].y < at.y);  // its point on top
    CHECK(up[1].corners[1].y > at.y);
    const auto down = Trace(kScreen, at, 2, Height::Below, kDestination);
    CHECK(down[1].corners[0].y > at.y);
    CHECK(down[1].corners[1].y < at.y);
}

TEST_CASE("blips: pinned to the rim, shops only while inside")
{
    const auto view = At(0.0f, 0.0f, 0.0f);
    const Vec2 far{ 0.0f, 5000.0f * kM };
    const auto quest = Place(kScreen, view, Kind::Quest, far);
    REQUIRE(quest);
    CHECK(quest->y == doctest::Approx(Area(kScreen).y0));  // on the rim, straight ahead
    const auto waypoint = Place(kScreen, view, Kind::Waypoint, { -5000.0f * kM, 0.0f });
    REQUIRE(waypoint);
    CHECK(waypoint->x == doctest::Approx(Area(kScreen).x0));
    CHECK_FALSE(Place(kScreen, view, Kind::Shop, far));
    const auto shop = Place(kScreen, view, Kind::Shop, { 0.0f, 90.0f * kM });
    REQUIRE(shop);
    CHECK(shop->y == doctest::Approx(ToScreen(kScreen, { 0.0f, 0.5f }).y));
    CHECK_FALSE(Place(kScreen, view, Kind::Quest, { std::nanf(""), 0.0f }));
}

TEST_CASE("the N is on the rim towards north")
{
    CHECK(North(kScreen, At(5.0f, 5.0f, 0.0f)).y == doctest::Approx(Area(kScreen).y0));
    const auto east = North(kScreen, At(5.0f, 5.0f, kQuarterTurn));  // looking east, north is left
    CHECK(east.x == doctest::Approx(Area(kScreen).x0));
    CHECK(east.y == doctest::Approx(ToScreen(kScreen, {}).y));
}

TEST_CASE("CJ's arrow turns with him against the camera")
{
    const auto middle = ToScreen(kScreen, {});
    const auto upright = Arrow(kScreen, 0.0f);
    CHECK(upright[0].x < middle.x);
    CHECK(upright[0].y < middle.y);  // top left
    CHECK(upright[2].x > middle.x);
    CHECK(upright[2].y > middle.y);  // bottom right
    const float half = 8.0f / 1.41421356f;
    CHECK(upright[1].x - middle.x == doctest::Approx(half * kScreen.unitX));

    // A quarter turn clockwise: the sprite's top left corner goes top right.
    const auto right = Arrow(kScreen, kQuarterTurn);
    CHECK(right[0].x > middle.x);
    CHECK(right[0].y < middle.y);
}

TEST_CASE("the radar map's JSON gives the image's place in the world")
{
    constexpr std::string_view kJson = R"({
  "image": "radar-map.png",
  "world": "Tamriel",
  "width": 3808,
  "height": 3072,
  "pixelsPerCell": 32,
  "unitsPerPixel": 128,
  "cells": { "minX": -57, "minY": -43, "maxX": 61, "maxY": 52 },
  "origin": { "x": -233472, "y": 217088 },
  "mapping": "pixelX = (worldX - origin.x) / unitsPerPixel, pixelY = (origin.y - worldY) / unitsPerPixel",
  "sources": [ { "name": "Skyrim.esm", "size": 250000000, "time": 1700000000 } ]
}
)";
    const auto map = ParseMapJson(kJson);
    REQUIRE(map);
    CHECK(map->left == doctest::Approx(-233472.0f));
    CHECK(map->top == doctest::Approx(217088.0f));
    CHECK(map->right == doctest::Approx(-233472.0f + 3808.0f * 128.0f));
    CHECK(map->bottom == doctest::Approx(217088.0f - 3072.0f * 128.0f));

    // The world point at the image's top left is uv 0, a pixel further is one pixel's worth in.
    const auto corner = UvOf(*map, { -233472.0f, 217088.0f });
    CHECK(corner.x == doctest::Approx(0.0f));
    CHECK(corner.y == doctest::Approx(0.0f));
    const auto next = UvOf(*map, { -233472.0f + 128.0f, 217088.0f - 128.0f });
    CHECK(next.x == doctest::Approx(1.0f / 3808.0f));
    CHECK(next.y == doctest::Approx(1.0f / 3072.0f));

    CHECK_FALSE(ParseMapJson(""));
    CHECK_FALSE(ParseMapJson(R"({ "width": 10, "height": 10, "unitsPerPixel": 128 })"));  // no origin
    CHECK_FALSE(ParseMapJson(R"({ "width": 0, "height": 10, "unitsPerPixel": 128, "origin": { "x": 0, "y": 0 } })"));
    CHECK_FALSE(ParseMapJson(R"({ "width": "big", "height": 10, "unitsPerPixel": 128, "origin": { "x": 0, "y": 0 } })"));
}
