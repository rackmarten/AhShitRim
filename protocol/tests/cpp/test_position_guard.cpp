#include <doctest/doctest.h>

#include <limits>

#include "../../cpp/position_guard.h"

using namespace AhShitRim;
using namespace AhShitRim::PositionGuard;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const Vec3      kAnchor{ 0.0f, 0.0f, 501.0f };
}

TEST_CASE("only finite positions in Skyrim's part of San Andreas are sound")
{
    CHECK(Sound({ 0.0f, 0.0f, 500.0f }));
    CHECK(Sound({ -900.0f, 950.0f, 210.0f }));
    CHECK_FALSE(Sound({ kNaN, 0.0f, 500.0f }));
    CHECK_FALSE(Sound({ 0.0f, 0.0f, kNaN }));
    CHECK_FALSE(Sound({ kInf, 0.0f, 500.0f }));
    CHECK_FALSE(Sound({ 0.0f, 0.0f, -95.0f }));  // San Andreas's own fall-through rescue, under its sea
    CHECK_FALSE(Sound({ 0.0f, 0.0f, 0.0f }));
    CHECK_FALSE(Sound({ 0.0f, 0.0f, 1.0f }));  // CJ seated in a car San Andreas dropped at its sea level
    CHECK_FALSE(Sound({ 0.0f, 25000.0f, 500.0f }));
}

TEST_CASE("a sound CJ is left alone")
{
    Watch w;
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(w.Update(0.033f, { i * 1.0f, 0.0f, 500.0f }, kAnchor).has_value());
    }
}

TEST_CASE("the car death off the Throat of the World: put back where he last was")
{
    Watch w;
    w.Update(0.033f, { 3.0f, -2.0f, 230.0f }, kAnchor);  // falling with his car
    // The landing: about a metre over San Andreas's sea, then NaN.
    auto r = w.Update(0.033f, { -0.2f, 0.1f, 1.0f }, kAnchor);
    REQUIRE(r.has_value());
    CHECK(r->to == To::LastGood);
    CHECK(r->at.x == 3.0f);
    CHECK(r->at.z == 230.0f);
}

TEST_CASE("broken again before he settles: the anchor")
{
    Watch w;
    w.Update(0.033f, { 3.0f, -2.0f, 230.0f }, kAnchor);
    REQUIRE(w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor)->to == To::LastGood);
    w.Update(0.033f, { 3.0f, -2.0f, 230.0f }, kAnchor);  // put back, one sound frame
    auto r = w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
    REQUIRE(r.has_value());
    CHECK(r->to == To::Anchor);
    CHECK(r->at.z == kAnchor.z);
    // Still broken: the anchor every frame, never a NaN.
    for (int i = 0; i < 5; ++i) {
        r = w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
        REQUIRE(r.has_value());
        CHECK(Sound(r->at));
    }
}

TEST_CASE("settled again, the last good spot is trusted again")
{
    Watch w;
    w.Update(0.033f, { 3.0f, -2.0f, 230.0f }, kAnchor);
    w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
    for (int i = 0; i < 70; ++i) {  // over kSettle
        w.Update(0.033f, { 10.0f, 0.0f, 500.0f }, kAnchor);
    }
    auto r = w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
    REQUIRE(r.has_value());
    CHECK(r->to == To::LastGood);
    CHECK(r->at.x == 10.0f);
}

TEST_CASE("nothing sound yet after a handover: the anchor")
{
    Watch w;
    w.Update(0.033f, { 3.0f, -2.0f, 230.0f }, kAnchor);
    w.Reset();  // a load: the spot of the old handover means nothing here
    auto r = w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
    REQUIRE(r.has_value());
    CHECK(r->to == To::Anchor);
    CHECK(r->at.z == kAnchor.z);
}

TEST_CASE("a broken anchor falls back to the origin's middle")
{
    Watch w;
    auto  r = w.Update(0.033f, { kNaN, kNaN, kNaN }, { kNaN, 0.0f, 0.0f });
    REQUIRE(r.has_value());
    CHECK(r->at.x == 0.0f);
    CHECK(r->at.z == Ground::kHeight);
}

TEST_CASE("the remembered spot moves with the origin, and a broken shift moves nothing")
{
    Watch w;
    w.Update(0.033f, { 1000.0f, 0.0f, 500.0f }, kAnchor);
    w.Shift({ 1000.0f, 0.0f, 0.0f });
    w.Shift({ kNaN, kNaN, kNaN });
    auto r = w.Update(0.033f, { kNaN, kNaN, kNaN }, kAnchor);
    REQUIRE(r.has_value());
    CHECK(r->at.x == 0.0f);
    CHECK(r->at.z == 500.0f);
}
