#include <cmath>

#include <doctest/doctest.h>

#include "../../cpp/ride.h"

using namespace AhShitRim;
using namespace AhShitRim::Ride;

namespace
{
    constexpr float kPi = 3.14159265f;

    // A frame turned a_roll radians about its forward (Y) axis: its right side goes down for a positive angle there,
    // so a negative a_roll lifts it.
    Body::Matrix34 Rolled(float a_roll)
    {
        Protocol::BodyBone bone{};
        bone.rotation[1] = std::sin(a_roll / 2.0f);
        bone.rotation[3] = std::cos(a_roll / 2.0f);
        return Body::ToMatrix(bone);
    }

    Body::Matrix34 Pitched(float a_pitch)
    {
        Protocol::BodyBone bone{};
        bone.rotation[0] = std::sin(a_pitch / 2.0f);
        bone.rotation[3] = std::cos(a_pitch / 2.0f);
        return Body::ToMatrix(bone);
    }

    // Drives a_leg round a circle of a_radius clockwise (a right turn) at a_speed for a_seconds, 60 samples a second.
    // Its origin sits a_ahead in front of the point that follows the circle's tangent: 0 turns about its middle, a car
    // whose origin is a_ahead in front of its rear axle turns about that axle.
    void Circle(Leg& a_leg, float a_radius, float a_speed, float a_seconds, float a_ahead)
    {
        for (int i = 0; i <= static_cast<int>(a_seconds * 60.0f); i++) {
            const double t = i / 60.0;
            const float  angle = a_speed * static_cast<float>(t) / a_radius;  // round the centre, clockwise from west
            const float  heading = angle;                                       // facing north at the start, turning right
            // The pivot point on the circle (centre at (a_radius, 0)), then the origin a_ahead along the heading.
            const float px = a_radius - a_radius * std::cos(angle), py = a_radius * std::sin(angle);
            a_leg.Add(t, px + a_ahead * std::sin(heading), py + a_ahead * std::cos(heading), 0.0f, heading, {});
        }
    }
}

TEST_CASE("a vehicle's attitude from its frame")
{
    const auto level = AttitudeOf(Rolled(0.0f));
    CHECK(level.pitch == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(level.roll == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(level.tilt == doctest::Approx(0.0f).epsilon(0.001));
    const auto rolled = AttitudeOf(Rolled(-20.0f / kDegrees));
    CHECK(rolled.roll == doctest::Approx(20.0f).epsilon(0.01));  // right side up
    CHECK(rolled.tilt == doctest::Approx(20.0f).epsilon(0.01));
    CHECK(std::abs(rolled.pitch) < 0.01f);
    const auto nose = AttitudeOf(Pitched(15.0f / kDegrees));
    CHECK(nose.pitch == doctest::Approx(15.0f).epsilon(0.01));
    CHECK(std::abs(nose.roll) < 0.01f);
    CHECK(AttitudeOf(Rolled(kPi)).tilt == doctest::Approx(180.0f).epsilon(0.01));  // on its roof
}

TEST_CASE("a turn about the middle doesn't slip; one about the rear axle does")
{
    // 10 m radius at 5 m/s for 4 s, in Skyrim units (70 to the metre).
    const float m = 70.0f;
    Leg middle(2.0f * m);
    Circle(middle, 10.0f * m, 5.0f * m, 4.0f, 0.0f);
    CHECK(middle.MeanSlip() < 0.5f);
    CHECK(middle.Turned() == doctest::Approx(4.0f * 5.0f / 10.0f * kDegrees).epsilon(0.01));  // right: positive
    CHECK(middle.Radius() == doctest::Approx(10.0f * m).epsilon(0.01));
    CHECK(middle.TopSpeed() == doctest::Approx(5.0f * m).epsilon(0.01));

    // A car's origin 3 m ahead of its rear axle: the origin slides atan(3/10), about 16.7 degrees, as San Andreas's
    // Rhino did.
    Leg car(2.0f * m);
    Circle(car, 10.0f * m, 5.0f * m, 4.0f, 3.0f * m);
    CHECK(car.MeanSlip() == doctest::Approx(std::atan(0.3f) * kDegrees).epsilon(0.02));
    CHECK(car.MaxSlip() >= car.MeanSlip());

    // Sliding straight sideways: all slip.
    Leg crab(2.0f * m);
    for (int i = 0; i <= 60; i++) {
        crab.Add(i / 60.0, i * 3.0f * m / 60.0f, 0.0f, 0.0f, 0.0f, {});  // moving east, facing north
    }
    CHECK(crab.MeanSlip() == doctest::Approx(90.0f).epsilon(0.001));
    CHECK(std::abs(crab.Turned()) < 0.001f);
    CHECK(crab.Radius() == 0.0f);
}

TEST_CASE("an impact is counted, and the median slip leaves it out")
{
    // 10 m/s straight north for 1 s, then thrown sideways and stopped in 0.15 s, as against a tree.
    const float m = 70.0f;
    Leg leg(2.0f * m);
    int i = 0;
    for (; i <= 60; i++) {
        leg.Add(i / 60.0, 0.0f, i * 10.0f * m / 60.0f, 0.0f, 0.0f, {});
    }
    const float y = (i - 1) * 10.0f * m / 60.0f;
    for (int k = 1; k <= 9; k++) {
        leg.Add((i + k) / 60.0, k * 0.8f * m / 9.0f, y + k * 0.2f * m / 9.0f, 0.0f, 0.0f, {});  // mostly sideways, slowing
    }
    for (int k = 10; k <= 40; k++) {
        leg.Add((i + k) / 60.0, 0.8f * m, y + 0.2f * m, 0.0f, 0.0f, {});  // stopped
    }
    CHECK(leg.Impacts() == 1);
    CHECK(leg.MaxSlip() > 45.0f);
    CHECK(leg.MedianSlip() < 1.0f);
    CHECK(leg.MeanSlip() > leg.MedianSlip());
    Leg smooth(2.0f * m);
    Circle(smooth, 10.0f * m, 5.0f * m, 4.0f, 0.0f);
    CHECK(smooth.Impacts() == 0);
}

TEST_CASE("a leg's slip ignores standing still, and its height and tilt are kept")
{
    Leg leg(140.0f);
    leg.Add(0.0, 0.0f, 0.0f, 10.0f, 0.0f, {});
    leg.Add(0.5, 1.0f, 0.0f, 10.0f, 0.0f, {});  // 2 units/s sideways: jitter, not a slide
    CHECK(leg.SlipSamples() == 0);
    leg.Add(1.0, 1.0f, 100.0f, 60.0f, 0.0f, { .pitch = 5.0f, .roll = -12.0f, .tilt = 13.0f });
    CHECK(leg.SlipSamples() == 1);
    CHECK(leg.LastSlip() < 0.5f);
    CHECK(leg.MaxRise() == doctest::Approx(100.0f));  // 50 units in 0.5 s
    CHECK(leg.HeightRange() == doctest::Approx(50.0f));
    CHECK(leg.MaxRoll() == doctest::Approx(12.0f));
    CHECK(leg.MaxTilt() == doctest::Approx(13.0f));
    CHECK(leg.Moved() == doctest::Approx(std::hypot(1.0f, 100.0f)));
    // A heading across north (6.2 to 0.1 rad) counts as the short way round.
    Leg wrap(140.0f);
    wrap.Add(0.0, 0.0f, 0.0f, 0.0f, 6.2f, {});
    wrap.Add(0.1, 0.0f, 0.0f, 0.0f, 0.1f, {});
    CHECK(wrap.Turned() == doctest::Approx((0.1f + 6.2831853f - 6.2f) * kDegrees).epsilon(0.001));
}
