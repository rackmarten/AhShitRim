#include <doctest/doctest.h>

#include "../../cpp/body.h"
#include "../../cpp/passengers.h"

using namespace AhShitRim;
using namespace AhShitRim::Passengers;

namespace
{
    // A seat bone for a vehicle at a heading (Skyrim radians), its seat a_local (x right, y forward, z up) from the
    // vehicle's middle, as San Andreas writes it (Body::ToBone of the vehicle's matrix at the seat).
    Protocol::BodyBone SeatAt(float a_heading, const Pose::Vec3& a_local)
    {
        const Pose::Vec3 forward{ std::sin(a_heading), std::cos(a_heading), 0.0f };
        const Pose::Vec3 right{ forward.y, -forward.x, 0.0f };
        Body::RwRows     rows;
        rows.right = right;
        rows.up = forward;
        rows.at = { 0.0f, 0.0f, 1.0f };
        // ToBone takes San Andreas's metres to Skyrim's units.
        rows.pos = Body::Scale(Body::Add(Body::Add(Body::Scale(right, a_local.x), Body::Scale(forward, a_local.y)), { 0.0f, 0.0f, a_local.z }), Pose::kMetersPerUnit);
        return Body::ToBone(rows, {});
    }
}

TEST_CASE("passengers: followers take the free seats nearest first, as far as there are seats")
{
    const std::vector<Follower> followers{ { 1, 500.0f }, { 2, 120.0f }, { 3, 300.0f }, { 4, 200.0f } };
    const auto                  seats = Assign(followers, 3);
    CHECK(seats[1] == 0);   // the nearest: the front seat
    CHECK(seats[3] == 1);
    CHECK(seats[2] == 2);
    CHECK(seats[0] == -1);  // no seat left
}

TEST_CASE("passengers: a two-seater takes one, a bike its pillion, a bicycle none")
{
    const std::vector<Follower> followers{ { 1, 300.0f }, { 2, 100.0f } };
    const auto                  coupe = Assign(followers, 1);
    CHECK(coupe[1] == 0);
    CHECK(coupe[0] == -1);
    const auto bmx = Assign(followers, 0);
    CHECK(bmx[0] == -1);
    CHECK(bmx[1] == -1);
    // More seats than San Andreas sends are never handed out.
    const std::vector<Follower> many{ { 1, 1.0f }, { 2, 2.0f }, { 3, 3.0f }, { 4, 4.0f } };
    CHECK(Assign(many, 8)[3] == -1);
}

TEST_CASE("passengers: whoever sits keeps the seat, the rest fill around them")
{
    const std::vector<Follower> followers{ { 1, 900.0f, 0 }, { 2, 50.0f }, { 3, 60.0f, 2 } };
    const auto                  seats = Assign(followers, 3);
    CHECK(seats[0] == 0);  // farther, but already in the front seat
    CHECK(seats[2] == 2);
    CHECK(seats[1] == 1);
}

TEST_CASE("passengers: followers too far away stay behind")
{
    const std::vector<Follower> followers{ { 1, kRange + 1.0f }, { 2, kRange - 1.0f }, { 3, std::nanf("") } };
    const auto                  seats = Assign(followers, 3);
    CHECK(seats[0] == -1);
    CHECK(seats[1] == 0);
    CHECK(seats[2] == -1);
}

TEST_CASE("passengers: a seat's heading is the vehicle's")
{
    CHECK(HeadingOf(SeatAt(0.0f, { 30.0f, 0.0f, 20.0f })) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(HeadingOf(SeatAt(1.5f, { 30.0f, 0.0f, 20.0f })) == doctest::Approx(1.5f).epsilon(1e-4));
    CHECK(HeadingOf(SeatAt(4.0f, { -30.0f, -50.0f, 20.0f })) == doctest::Approx(4.0f).epsilon(1e-4));
}

TEST_CASE("passengers: seated feet hang under the seat, and lean with a bike")
{
    const auto seat = SeatAt(0.0f, { 0.0f, -40.0f, 50.0f });
    const auto feet = SeatedFeet(seat, 18.0f);
    CHECK(feet.x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(feet.y == doctest::Approx(-40.0f));
    CHECK(feet.z == doctest::Approx(32.0f));

    // A bike leant 30 degrees to the right: down is along its own up.
    Body::RwRows rows;
    const float  lean = 0.5236f;
    rows.right = { std::cos(lean), 0.0f, -std::sin(lean) };
    rows.up = { 0.0f, 1.0f, 0.0f };
    rows.at = { std::sin(lean), 0.0f, std::cos(lean) };
    rows.pos = { 0.0f, 0.0f, 50.0f * Pose::kMetersPerUnit };
    const auto leant = SeatedFeet(Body::ToBone(rows, {}), 18.0f);
    CHECK(leant.x == doctest::Approx(-18.0f * std::sin(lean)).epsilon(1e-3));
    CHECK(leant.z == doctest::Approx(50.0f - 18.0f * std::cos(lean)).epsilon(1e-3));
}

TEST_CASE("passengers: they get out on their own door's side")
{
    // Facing north: the front passenger seat is on the right (east), the back left one on the left.
    const auto right = ExitAt(SeatAt(0.0f, { 30.0f, 0.0f, 20.0f }));
    CHECK(right.x == doctest::Approx(30.0f + kStepOut));
    CHECK(right.z == doctest::Approx(20.0f + kStepUp));
    const auto left = ExitAt(SeatAt(0.0f, { -30.0f, -50.0f, 20.0f }));
    CHECK(left.x == doctest::Approx(-30.0f - kStepOut));
    CHECK(left.y == doctest::Approx(-50.0f));
    // Facing east, the right is south.
    const auto east = ExitAt(SeatAt(Pose::kPi / 2.0f, { 30.0f, 0.0f, 20.0f }));
    CHECK(east.y == doctest::Approx(-30.0f - kStepOut).epsilon(1e-3));
    // A bike's pillion, in the middle: off to the left.
    const auto pillion = ExitAt(SeatAt(0.0f, { 0.0f, -40.0f, 50.0f }));
    CHECK(pillion.x == doctest::Approx(-kStepOut));
}

TEST_CASE("passengers: the seats are smoothed with the vehicle")
{
    Protocol::BodyState a{}, b{};
    a.seatCount = b.seatCount = 2;
    a.boneCount = b.boneCount = 1;
    a.bones[0].rotation[3] = b.bones[0].rotation[3] = 1.0f;
    a.seats[1] = SeatAt(0.0f, { -30.0f, -50.0f, 20.0f });
    b.seats[1] = SeatAt(0.0f, { -30.0f, -30.0f, 20.0f });
    a.seats[0].rotation[3] = b.seats[0].rotation[3] = 1.0f;
    const auto half = Body::Blend(a, b, 0.5f);
    CHECK(half.seats[1].translation[1] == doctest::Approx(-40.0f));
    CHECK(Body::Finite(half));
    b.seats[1].translation[0] = std::nanf("");
    CHECK_FALSE(Body::Finite(b));
}

TEST_CASE("passengers: getting out, they step from the seat to beside the door and down to the ground, without a jump")
{
    // Facing north, the front passenger seat 30 units right of the middle, the ground 40 units under it.
    const auto  seat = SeatAt(0.0f, { 30.0f, 10.0f, 20.0f });
    const float floor = -40.0f;
    const auto  start = ExitPath(seat, 0.0f, floor);
    const auto  seated = SeatedFeet(seat);
    CHECK(start.x == doctest::Approx(seated.x));
    CHECK(start.z == doctest::Approx(seated.z));
    const auto end = ExitPath(seat, kExitSeconds, floor);
    CHECK(end.x == doctest::Approx(ExitAt(seat).x));
    CHECK(end.y == doctest::Approx(10.0f));
    CHECK(end.z == doctest::Approx(floor + kLandLift));  // on the ground, not half a metre over it
    // Later it stays there; a bad time puts it there too.
    CHECK(ExitPath(seat, 5.0f, floor).x == doctest::Approx(end.x));
    CHECK(ExitPath(seat, std::nanf(""), floor).x == doctest::Approx(end.x));
    // Every 60th of a second it moves a little, always outwards.
    float last = start.x;
    float biggest = 0.0f;
    for (int i = 1; i <= 90; i++) {
        const auto at = ExitPath(seat, i / 60.0f, floor);
        CHECK(at.x >= last);
        biggest = (std::max)(biggest, at.x - last);
        last = at.x;
    }
    CHECK(biggest * Pose::kMetersPerUnit < 0.05f);
    // No box: ExitAt's height.
    CHECK(ExitPath(seat, kExitSeconds, std::nanf("")).z == doctest::Approx(ExitAt(seat).z));
}

TEST_CASE("passengers: the vehicle's floor is its box's bottom, none without a box")
{
    Protocol::BodyState car{};
    car.vehicleFrame.rotation[3] = 1.0f;
    CHECK(std::isnan(FloorOf(car)));
    car.boxMin[2] = -45.0f;
    car.boxMax[2] = 60.0f;
    CHECK(FloorOf(car) == doctest::Approx(-45.0f));
}
