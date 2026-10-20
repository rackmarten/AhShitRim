#include <doctest/doctest.h>

#include "../../cpp/body.h"

using namespace AhShitRim;
using namespace AhShitRim::Body;
using namespace AhShitRim::Protocol;

namespace
{
    // A rotation about Z by a_angle (counter-clockwise from above) as RenderWare rows.
    RwRows TurnZ(float a_angle, const Vec3& a_pos = {})
    {
        const float c = std::cos(a_angle), s = std::sin(a_angle);
        return { { c, s, 0.0f }, { -s, c, 0.0f }, { 0.0f, 0.0f, 1.0f }, a_pos };
    }

    RwRows TurnX(float a_angle, const Vec3& a_pos = {})
    {
        const float c = std::cos(a_angle), s = std::sin(a_angle);
        return { { 1.0f, 0.0f, 0.0f }, { 0.0f, c, s }, { 0.0f, -s, c }, a_pos };
    }

    void CheckNear(const Vec3& a, const Vec3& b, float a_eps = 1e-3f)
    {
        CHECK(a.x == doctest::Approx(b.x).epsilon(a_eps));
        CHECK(a.y == doctest::Approx(b.y).epsilon(a_eps));
        CHECK(a.z == doctest::Approx(b.z).epsilon(a_eps));
    }

    SkinVertex Vertex(const Vec3& a_metres, std::uint8_t a_bone0, float a_w0, std::uint8_t a_bone1 = 0, float a_w1 = 0.0f)
    {
        SkinVertex v{};
        v.position[0] = a_metres.x * Pose::kUnitsPerMeter;
        v.position[1] = a_metres.y * Pose::kUnitsPerMeter;
        v.position[2] = a_metres.z * Pose::kUnitsPerMeter;
        v.normal[0] = 1.0f;
        v.bones[0] = a_bone0;
        v.weights[0] = a_w0;
        v.bones[1] = a_bone1;
        v.weights[1] = a_w1;
        return v;
    }

    BodyState Frame(std::uint32_t a_frame, std::uint32_t a_timeMs, float a_x)
    {
        BodyState b{};
        b.frame = a_frame;
        b.flags = kBodyValid;
        b.handover = 1;
        b.model = 1;
        b.boneCount = 1;
        b.timeMs = a_timeMs;
        b.feet[0] = a_x;
        b.cameraForward[1] = 1.0f;
        b.bones[0].rotation[3] = 1.0f;
        return b;
    }
}

TEST_CASE("RenderWare matrices apply as rows, and Then applies the first one first")
{
    const RwRows turn = TurnZ(3.14159265f / 2);  // east to north
    CheckNear(Apply(turn, { 1.0f, 0.0f, 0.0f }), { 0.0f, 1.0f, 0.0f });
    const RwRows move{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 5.0f, 0.0f, 0.0f } };
    CheckNear(Apply(Then(turn, move), { 1.0f, 0.0f, 0.0f }), { 5.0f, 1.0f, 0.0f });  // turned, then moved
    CheckNear(Apply(Then(move, turn), { 1.0f, 0.0f, 0.0f }), { 0.0f, 6.0f, 0.0f });  // moved, then turned
}

TEST_CASE("a skin matrix in local space also goes through the atomic's frame")
{
    const RwRows skinToBone = TurnX(0.3f, { 0.0f, 0.0f, -1.0f });
    const RwRows bone = TurnZ(0.5f, { 0.0f, 0.0f, 1.0f });
    const RwRows atomic = TurnZ(1.0f, { 10.0f, 20.0f, 500.0f });
    const Vec3    v{ 0.2f, 0.1f, 1.5f };
    CheckNear(Apply(SkinToWorld(skinToBone, bone, atomic, false), v), Apply(bone, Apply(skinToBone, v)));
    CheckNear(Apply(SkinToWorld(skinToBone, bone, atomic, true), v), Apply(atomic, Apply(bone, Apply(skinToBone, v))));
}

TEST_CASE("a bone maps a model vertex where San Andreas's skin matrix puts it, relative to the feet in Skyrim units")
{
    const RwRows world = Then(TurnX(0.4f), TurnZ(2.0f, { 12.0f, -3.0f, 501.2f }));
    const Vec3    feet{ 12.0f, -3.0f, 500.0f };
    const auto    bone = ToBone(world, feet);
    const auto    matrix = ToMatrix(bone);

    const Vec3 metres{ 0.1f, 0.2f, 0.7f };
    Vec3       position, normal;
    Skin(&matrix, 1, Vertex(metres, 0, 1.0f), position, normal);
    CheckNear(position, Scale(Sub(Apply(world, metres), feet), Pose::kUnitsPerMeter));
    CheckNear(normal, Normalize(RotateBy(world, { 1.0f, 0.0f, 0.0f })));
}

TEST_CASE("a bone's rotation stays a rotation when the matrix has drifted")
{
    RwRows world = TurnZ(0.8f);
    world.right = Scale(world.right, 1.02f);
    world.up = Add(world.up, { 0.01f, 0.0f, 0.0f });
    const auto  bone = ToBone(world, {});
    const float length = std::sqrt(bone.rotation[0] * bone.rotation[0] + bone.rotation[1] * bone.rotation[1] +
                                   bone.rotation[2] * bone.rotation[2] + bone.rotation[3] * bone.rotation[3]);
    CHECK(length == doctest::Approx(1.0f));
}

TEST_CASE("a vertex between two bones goes to the weighted middle, and one without weight stays put")
{
    const BodyBone a = ToBone(RwRows{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.0f, 0.0f, 0.0f } }, {});
    const BodyBone b = ToBone(RwRows{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1.0f, 0.0f, 0.0f } }, {});
    const Matrix34 bones[2] = { ToMatrix(a), ToMatrix(b) };
    Vec3           position, normal;
    Skin(bones, 2, Vertex({ 0.0f, 0.0f, 1.0f }, 0, 0.25f, 1, 0.75f), position, normal);
    CheckNear(position, { 0.75f * Pose::kUnitsPerMeter, 0.0f, Pose::kUnitsPerMeter });
    Skin(bones, 2, Vertex({ 0.0f, 0.0f, 1.0f }, 0, 0.0f), position, normal);
    CheckNear(position, { 0.0f, 0.0f, Pose::kUnitsPerMeter });
}

TEST_CASE("blending turns the short way and moves in a straight line")
{
    BodyState a = Frame(1, 0, 0.0f), b = Frame(2, 33, 70.0f);
    const auto q = FromAxes(TurnZ(3.0f).right, TurnZ(3.0f).up);
    b.bones[0].rotation[0] = -q.x;  // the same rotation as q, other sign
    b.bones[0].rotation[1] = -q.y;
    b.bones[0].rotation[2] = -q.z;
    b.bones[0].rotation[3] = -q.w;
    const auto half = Blend(a, b, 0.5f);
    CHECK(half.feet[0] == doctest::Approx(35.0f));
    CHECK(half.frame == 2);
    // Halfway from no turn to 3 rad about Z is 1.5 rad, not the long way round.
    const Matrix34 m = ToMatrix(half.bones[0]);
    CHECK(std::atan2(m.m[1][0], m.m[0][0]) == doctest::Approx(1.5f).epsilon(1e-3));
}

TEST_CASE("the interpolator plays San Andreas's frames back a fixed delay behind, smoothly")
{
    Interpolator interpolator;
    BodyState    out{};
    CHECK_FALSE(interpolator.Sample(0.0, out));

    // San Andreas's clock runs 1000 ms behind Skyrim's; frames arrive 2 ms after they were stamped.
    const double gap = 1000.0;
    for (std::uint32_t i = 0; i < 4; i++) {
        interpolator.Push(Frame(i + 1, 100 + i * 33, static_cast<float>(i) * 10.0f), 100 + i * 33 + gap + 2.0);
    }
    // At Skyrim 1202 ms: San Andreas time 1202 - 1002 - 50 = 150, between 133 (x 10) and 166 (x 20).
    REQUIRE(interpolator.Sample(1202.0, out));
    CHECK(out.feet[0] == doctest::Approx(10.0f + 10.0f * 17.0f / 33.0f));

    // Past the newest frame it holds the newest, before the oldest the oldest.
    REQUIRE(interpolator.Sample(5000.0, out));
    CHECK(out.feet[0] == doctest::Approx(30.0f));
    REQUIRE(interpolator.Sample(0.0, out));
    CHECK(out.feet[0] == doctest::Approx(0.0f));
}

TEST_CASE("the interpolator ignores a frame it has, and starts over on a new handover")
{
    Interpolator interpolator;
    BodyState    out{};
    interpolator.Push(Frame(1, 0, 0.0f), 0.0);
    interpolator.Push(Frame(2, 33, 10.0f), 33.0);
    interpolator.Push(Frame(2, 33, 99.0f), 40.0);  // read again: not a new frame
    REQUIRE(interpolator.Sample(1000.0, out));
    CHECK(out.feet[0] == doctest::Approx(10.0f));

    auto moved = Frame(1, 66, 500.0f);
    moved.handover = 2;
    interpolator.Push(moved, 66.0);
    REQUIRE(interpolator.Sample(66.0, out));
    CHECK(out.feet[0] == doctest::Approx(500.0f));  // nothing to blend with from the old handover
}

TEST_CASE("the interpolator's clock gap follows a San Andreas that falls behind, slowly")
{
    Interpolator interpolator;
    interpolator.Push(Frame(1, 0, 0.0f), 0.0);
    // Every later frame arrives 20 ms later than the first suggested: the gap grows only
    // kGapDriftPerMs per ms, so playback doesn't jump.
    for (std::uint32_t i = 1; i < 60; i++) {
        interpolator.Push(Frame(i + 1, i * 33, static_cast<float>(i)), i * 33 + 20.0);
    }
    BodyState out{};
    REQUIRE(interpolator.Sample(59 * 33 + 20.0, out));
    // The gap has caught up to 20 ms, so playback runs kDelayMs behind the newest frame.
    CHECK(out.feet[0] == doctest::Approx(59.0f - static_cast<float>(Interpolator::kDelayMs / 33.0)).epsilon(0.02));
}

TEST_CASE("a body with a NaN is not finite")
{
    BodyState b = Frame(1, 0, 0.0f);
    CHECK(Finite(b));
    b.bones[0].translation[1] = std::nanf("");
    CHECK_FALSE(Finite(b));
}
