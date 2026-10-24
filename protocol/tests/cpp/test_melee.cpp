#include <doctest/doctest.h>

#include "../../cpp/melee.h"

#include <array>

using namespace AhShitRim;
using namespace AhShitRim::Melee;

namespace
{
    constexpr float kNorth = 0.0f;               // Skyrim headings
    constexpr float kEast = 0.5f * Pose::kPi;

    Candidate At(std::uint32_t id, float x, float y, bool hostile = false) { return { id, { x, y, 0.0f }, hostile }; }

    const auto kSeesAll = [](std::size_t) { return true; };

    // A person standing at (x, y, 0), metres.
    RunOver::Capsule PersonAt(float x, float y)
    {
        const auto s = RunOver::ShapeOf(RunOver::Build::Person, 1.0f, 0.0f);
        auto       c = RunOver::Place(s, { x * RunOver::kMeter, y * RunOver::kMeter, 0.0f }, 0.0f);
        c.a = Scale(c.a, Pose::kMetersPerUnit);
        c.b = Scale(c.b, Pose::kMetersPerUnit);
        c.radius *= Pose::kMetersPerUnit;
        return c;
    }
}

TEST_CASE("melee: rating follows San Andreas's EvaluateTarget")
{
    // Straight ahead at a metre: the priority as it is.
    CHECK(Rating(At(1, 0.0f, 1.0f), kNorth) == doctest::Approx(kOtherPriority));
    CHECK(Rating(At(1, 0.0f, 1.0f, true), kNorth) == doctest::Approx(kHostilePriority));
    // At 16 m, out of range; at 9 m, down by the fourth root.
    CHECK(Rating(At(1, 0.0f, 16.0f), kNorth) == 0.0f);
    CHECK(Rating(At(1, 0.0f, 9.0f), kNorth) == doctest::Approx(kOtherPriority / std::sqrt(3.0f)));
    // 70 degrees off: half; behind (180 off, past the 140 degree view), nothing.
    const float off70 = 70.0f * Pose::kPi / 180.0f;
    CHECK(Rating(At(1, std::sin(off70), std::cos(off70)), kNorth) == doctest::Approx(0.5f * kOtherPriority).epsilon(0.01));
    CHECK(Rating(At(1, 0.0f, -2.0f), kNorth) == 0.0f);
    // The heading counts: east of CJ is ahead when he faces east, whichever way round the angle wraps.
    CHECK(Rating(At(1, 2.0f, 0.0f), kEast) > Rating(At(1, 2.0f, 0.0f), kNorth));
    CHECK(Rating(At(1, -0.1f, 2.0f), 2.0f * Pose::kPi - 0.05f) > 0.07f);
}

TEST_CASE("melee: the hostile one wins over a bystander nearer and in front")
{
    const std::array all{ At(10, 0.0f, 1.5f), At(20, 3.0f, 3.0f, true), At(30, 0.0f, -1.0f, true) };
    const auto       ranked = Ranked(all, kNorth);
    REQUIRE(ranked.size() == 2);  // the one behind isn't rated
    CHECK(all[ranked[0]].id == 20);
    CHECK(all[ranked[1]].id == 10);
}

TEST_CASE("melee: of two bystanders the one in front wins")
{
    const std::array all{ At(10, 2.0f, 0.5f), At(20, 0.3f, 2.5f) };
    CHECK(all[Ranked(all, kNorth)[0]].id == 20);
    CHECK(all[Ranked(all, kEast)[0]].id == 10);
}

TEST_CASE("melee: the lock holds on to its target until it's out of reach or let go")
{
    Lock lock;
    std::array all{ At(10, 0.0f, 2.0f, true), At(20, 0.0f, 6.0f) };
    CHECK(lock.Update(false, all, kNorth, kSeesAll) == 0);
    CHECK(lock.Update(true, all, kNorth, kSeesAll) == 10);
    // A better one walking up doesn't take it.
    all[1] = At(20, 0.0f, 1.0f, true);
    CHECK(lock.Update(true, all, kNorth, kSeesAll) == 10);
    // Nor does CJ turning away (San Andreas breaks a lock by range, not by angle).
    CHECK(lock.Update(true, all, Pose::kPi, kSeesAll) == 10);
    // Past kBreakRange it breaks and the best one left takes it.
    all[0] = At(10, 0.0f, kBreakRange + 0.5f, true);
    CHECK(lock.Update(true, all, kNorth, kSeesAll) == 20);
    // Gone (dead, unloaded): the next.
    const std::array none{ At(30, 0.0f, 30.0f) };
    CHECK(lock.Update(true, none, kNorth, kSeesAll) == 0);
    // Let go: nothing, and pressing again picks afresh.
    CHECK(lock.Update(true, all, kNorth, kSeesAll) == 20);
    CHECK(lock.Update(false, all, kNorth, kSeesAll) == 0);
    CHECK(lock.Target() == 0);
}

TEST_CASE("melee: a new target has to be in sight")
{
    Lock             lock;
    const std::array all{ At(10, 0.0f, 2.0f, true), At(20, 1.0f, 3.0f) };
    int              asked = 0;
    CHECK(lock.Update(true, all, kNorth, [&](std::size_t i) { asked++; return i != 0; }) == 20);
    CHECK(asked == 2);
    // Kept, it isn't asked again.
    CHECK(lock.Update(true, all, kNorth, [&](std::size_t) { asked++; return false; }) == 20);
    CHECK(asked == 2);
    Lock blind;
    CHECK(blind.Update(true, all, kNorth, [](std::size_t) { return false; }) == 0);
}

TEST_CASE("melee: CJ turns to his target at the turn rate, the short way round")
{
    const Vec3 cj{ 0.0f, 0.0f, 0.0f };
    // San Andreas: 0 faces north, a quarter turn left (+pi/2) faces west.
    CHECK(Facing(cj, { 0.0f, 5.0f, 0.0f }, 1.0f) == doctest::Approx(0.0f));
    CHECK(Facing(cj, { -5.0f, 0.0f, 0.0f }, 1.0f) == doctest::Approx(0.5f * Pose::kPi));
    CHECK(Facing(cj, { 5.0f, 0.0f, 0.0f }, 1.0f) == doctest::Approx(-0.5f * Pose::kPi));
    CHECK(Facing(cj, { 0.01f, 0.0f, 3.0f }, 1.0f) == 1.0f);  // straight above: as he is
    // A frame at 30 fps turns kTurnRate / 30 toward it.
    const float step = kTurnRate / 30.0f;
    CHECK(Turn(0.0f, cj, { -5.0f, 0.0f, 0.0f }, 1.0f / 30.0f) == doctest::Approx(step));
    CHECK(Turn(0.0f, cj, { 5.0f, 0.0f, 0.0f }, 1.0f / 30.0f) == doctest::Approx(-step));
    // Close enough, it lands on the target's heading.
    CHECK(Turn(0.0f, cj, { -0.1f, 5.0f, 0.0f }, 1.0f / 30.0f) == doctest::Approx(Facing(cj, { -0.1f, 5.0f, 0.0f }, 0.0f)));
    // From 170 degrees to -170, through 180, not back through 0.
    const float from = 170.0f * Pose::kPi / 180.0f;
    const float turned = Turn(from, cj, { std::sin(170.0f * Pose::kPi / 180.0f), std::cos(170.0f * Pose::kPi / 180.0f), 0.0f }, 1.0f / 60.0f);
    CHECK((turned > from || turned < -Pose::kPi + 0.5f));
    // A hitch doesn't swing him round in one go; a bad step doesn't turn him.
    CHECK(std::abs(Turn(0.0f, cj, { 0.0f, -5.0f, 0.0f }, 5.0f)) == doctest::Approx(kTurnRate * 0.1f));
    CHECK(Turn(0.3f, cj, { 0.0f, -5.0f, 0.0f }, std::nanf("")) == 0.3f);
}

TEST_CASE("melee: on foot the nearest are sent, and the target always")
{
    std::vector<RunOver::Candidate> all;
    for (std::uint32_t i = 0; i < 20; i++) {
        all.push_back({ i, (1.0f + static_cast<float>(i) * 0.5f) * RunOver::kMeter });
    }
    all.push_back({ 99, 30.0f * RunOver::kMeter });  // out of range
    const auto sent = OnFoot(all, all.size());
    REQUIRE(sent.size() == Protocol::kProxyMax);
    CHECK(sent.front() == 0);
    CHECK(std::find(sent.begin(), sent.end(), 20u) == sent.end());
    // The target, 18th nearest, takes the last place.
    const auto withTarget = OnFoot(all, 17);
    REQUIRE(withTarget.size() == Protocol::kProxyMax);
    CHECK(withTarget.back() == 17);
    CHECK(withTarget.front() == 0);
    // Few around: the target joins them, even just past kFootRange.
    const std::vector<RunOver::Candidate> few{ { 1, 2.0f * RunOver::kMeter }, { 2, 13.0f * RunOver::kMeter } };
    const auto                            two = OnFoot(few, 1);
    CHECK(two.size() == 2);
    CHECK(OnFoot(few, few.size()).size() == 1);
    CHECK(kBreakRange * RunOver::kMeter <= kFootRange);
}

TEST_CASE("melee: CJ stops against a guard instead of walking through him")
{
    const auto guard = PersonAt(0.0f, 1.0f);
    const Vec3 back{ 0.0f, -1.0f, 0.0f };
    // Apart: nothing.
    CHECK_FALSE(PushOut(CjAt({ 0.0f, 0.0f, 0.0f }), guard, back));
    // Walking into him from the south: out to the south, to just touching.
    const Vec3 feet{ 0.0f, 0.6f, 0.0f };
    const auto push = PushOut(CjAt(feet), guard, back);
    REQUIRE(push);
    CHECK(push->normal.y == doctest::Approx(-1.0f));
    CHECK(push->move.z == 0.0f);
    CHECK(feet.y + push->move.y == doctest::Approx(1.0f - kCjRadius - guard.radius));
    // His run north is taken away, his sideways step stays.
    const std::array actors{ guard };
    const auto       r = Resolve(feet, { 1.0f, 5.0f, 0.0f }, back, actors);
    CHECK(r.touching == 1);
    CHECK(r.velocity.y == doctest::Approx(0.0f));
    CHECK(r.velocity.x == doctest::Approx(1.0f));
    // Walking away from him isn't held back.
    CHECK(Resolve(feet, { 0.0f, -5.0f, 0.0f }, back, actors).velocity.y == doctest::Approx(-5.0f));
    // Right inside him: back the way he faces from.
    const auto inside = PushOut(CjAt({ 0.0f, 1.0f, 0.0f }), guard, back);
    REQUIRE(inside);
    CHECK(inside->normal.y == doctest::Approx(-1.0f));
}

TEST_CASE("melee: a giant, a wolf and a mammoth block him by their size; the push is capped")
{
    const auto toMetres = [](RunOver::Capsule c) {
        c.a = Scale(c.a, Pose::kMetersPerUnit);
        c.b = Scale(c.b, Pose::kMetersPerUnit);
        c.radius *= Pose::kMetersPerUnit;
        return c;
    };
    const Vec3 back{ 0.0f, -1.0f, 0.0f };
    const auto giant = toMetres(RunOver::Place(RunOver::ShapeOf(RunOver::Build::Giant, 1.0f, 0.0f), { 0.0f, 0.0f, 0.0f }, 0.0f));
    CHECK(PushOut(CjAt({ 0.0f, -1.0f, 0.0f }), giant, back));
    CHECK_FALSE(PushOut(CjAt({ 0.0f, -1.1f, 0.0f }), giant, back));
    const auto wolf = toMetres(RunOver::Place(RunOver::ShapeOf(RunOver::Build::Beast, 0.6f, 0.0f), { 0.0f, 0.0f, 0.0f }, 0.0f));
    CHECK(PushOut(CjAt({ 0.5f, 0.0f, 0.0f }), wolf, back));
    // A mammoth lying north-south: beside its flank 1.6 m out he touches, past its tail end he doesn't.
    const auto mammoth = toMetres(RunOver::Place(RunOver::ShapeOf(RunOver::Build::Mammoth, 1.0f, 0.0f), { 0.0f, 0.0f, 0.0f }, 0.0f));
    CHECK(PushOut(CjAt({ 1.6f, 1.0f, 0.0f }), mammoth, back));
    CHECK_FALSE(PushOut(CjAt({ 0.0f, -3.6f, 0.0f }), mammoth, back));
    // Caught deep inside the giant (it walked onto him): he comes out kPushMax a frame.
    const std::array actors{ giant };
    const auto       r = Resolve({ 0.0f, -0.1f, 0.0f }, {}, back, actors);
    CHECK(RunOver::Length(r.move) == doctest::Approx(kPushMax));
    CHECK(r.move.y < 0.0f);
    // A person on a ledge 3 m above doesn't touch him.
    auto above = PersonAt(0.0f, 0.5f);
    above.a.z += 3.0f;
    above.b.z += 3.0f;
    CHECK_FALSE(PushOut(CjAt({ 0.0f, 0.0f, 0.0f }), above, back));
}

TEST_CASE("melee: two guards side by side hold him between their pushes")
{
    const std::array actors{ PersonAt(-0.4f, 1.0f), PersonAt(0.4f, 1.0f) };
    const auto       r = Resolve({ 0.0f, 0.7f, 0.0f }, { 0.0f, 4.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, actors);
    CHECK(r.touching == 2);
    CHECK(r.move.y < 0.0f);
    CHECK(r.velocity.y <= 0.01f);
}
