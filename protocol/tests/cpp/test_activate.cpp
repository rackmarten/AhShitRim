#include <doctest/doctest.h>

#include "../../cpp/activate.h"

using namespace AhShitRim;
using namespace AhShitRim::Activate;

namespace
{
    constexpr float kIdentity[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
}

TEST_CASE("a heading and pitch give back the direction they came from")
{
    for (const float heading : { 0.0f, 1.0f, 3.0f, 5.5f }) {
        for (const float pitch : { -0.6f, 0.0f, 0.4f }) {
            const auto d = Direction(heading, pitch);
            CHECK(std::hypot(std::hypot(d.x, d.y), d.z) == doctest::Approx(1.0f));
            CHECK(Pose::HeadingOf(d.x, d.y) == doctest::Approx(heading).epsilon(1e-4));
            CHECK(Pose::PitchOf(d) == doctest::Approx(pitch).epsilon(1e-4));
        }
    }
    const auto north = Direction(0.0f, 0.0f);
    CHECK(north.y == doctest::Approx(1.0f));
    const auto down = Direction(0.0f, Pose::kPi / 2);  // positive pitch looks down
    CHECK(down.z == doctest::Approx(-1.0f));
}

TEST_CASE("a reference's box comes from its bounds and its 3D's transform")
{
    // A door 100 wide, 10 thick, 200 high from its hinge at the origin of its model, turned a
    // quarter (its model's x along the world's y), scaled 1.5.
    const float turned[3][3] = { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
    const auto  box = BoxOf({ 0, -5, 0 }, { 100, 5, 200 }, turned, { 1000, 2000, 300 }, 1.5f);
    CHECK(box.center.x == doctest::Approx(1000.0f));
    CHECK(box.center.y == doctest::Approx(2075.0f));
    CHECK(box.center.z == doctest::Approx(450.0f));
    CHECK(box.half.x == doctest::Approx(75.0f));
    CHECK(box.half.y == doctest::Approx(7.5f));
    CHECK(box.half.z == doctest::Approx(150.0f));
    CHECK(box.axes[0].y == doctest::Approx(1.0f));  // model x is world y
    CHECK(Usable(box));
    CHECK_FALSE(Usable(BoxOf({ 0, 0, 0 }, { 0, 0, 0 }, kIdentity, {}, 1.0f)));       // no bounds
    CHECK_FALSE(Usable(BoxOf({ 0, 0, 0 }, { 50, 0, 0 }, kIdentity, {}, 1.0f)));      // a line
    CHECK(Usable(BoxOf({ 0, 0, 0 }, { 50, 50, 0 }, kIdentity, {}, 1.0f)));           // a flat plate still counts
}

TEST_CASE("a ray enters a box at the near face, from inside at once, and misses past it")
{
    const auto box = BoxOf({ -10, -10, -10 }, { 10, 10, 10 }, kIdentity, { 0, 100, 0 }, 1.0f);
    const auto t = Hit({ 0, 0, 0 }, { 0, 1, 0 }, box);
    REQUIRE(t);
    CHECK(*t == doctest::Approx(90.0f));
    CHECK(*Hit({ 0, 100, 5 }, { 0, 1, 0 }, box) == 0.0f);     // inside
    CHECK_FALSE(Hit({ 0, 0, 0 }, { 0, -1, 0 }, box));         // behind
    CHECK_FALSE(Hit({ 20, 0, 0 }, { 0, 1, 0 }, box));         // parallel, beside it
    CHECK_FALSE(Hit({ 0, 0, 0 }, Direction(0.5f, 0.0f), box));  // looking off to the side

    // A turned box: a quarter turn about Z.
    const float turned[3][3] = { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
    const auto  door = BoxOf({ 0, -5, 0 }, { 100, 5, 200 }, turned, { 0, 100, 0 }, 1.0f);  // a door from (0,100) north to (0,200)
    CHECK(*Hit({ 0, 0, 100 }, { 0, 1, 0 }, door) == doctest::Approx(100.0f));  // walking up to it edge on
    const auto side = Hit({ -100, 150, 100 }, { 1, 0, 0 }, door);
    REQUIRE(side);
    CHECK(*side == doctest::Approx(95.0f));  // its 10-unit thickness straddles x = 0
}

TEST_CASE("the nearest box the ray enters within reach is picked")
{
    std::vector<Candidate> c;
    c.push_back({ BoxOf({ -10, -10, -10 }, { 10, 10, 10 }, kIdentity, { 0, 150, 0 }, 1.0f), 1 });
    c.push_back({ BoxOf({ -10, -10, -10 }, { 10, 10, 10 }, kIdentity, { 0, 80, 0 }, 1.0f), 2 });
    c.push_back({ BoxOf({ -10, -10, -10 }, { 10, 10, 10 }, kIdentity, { 50, 50, 0 }, 1.0f), 3 });  // off the ray
    auto picked = Pick({ 0, 0, 0 }, { 0, 1, 0 }, kDefaultReach, c);
    REQUIRE(picked);
    CHECK(c[picked->index].id == 2);
    CHECK(picked->distance == doctest::Approx(70.0f));

    picked = Pick({ 0, 0, 0 }, { 0, 1, 0 }, 60.0f, c);  // out of reach
    CHECK_FALSE(picked);
    CHECK_FALSE(Pick({ 0, 0, 0 }, { 0, 1, 0 }, kDefaultReach, {}));
}

TEST_CASE("someone on a throne beats the throne, though the ray enters the throne first")
{
    std::vector<Candidate> c;
    c.push_back({ BoxOf({ -50, -50, 0 }, { 50, 50, 150 }, kIdentity, { 0, 100, 0 }, 1.0f), 1, Kind::Furniture });  // the throne, from y = 50
    c.push_back({ BoxOf({ -20, -20, 0 }, { 20, 20, 130 }, kIdentity, { 0, 110, 0 }, 1.0f), 2, Kind::Person });     // the Jarl in it, from y = 90
    auto picked = Pick({ 0, 0, 100 }, { 0, 1, 0 }, kDefaultReach, c);
    REQUIRE(picked);
    CHECK(c[picked->index].id == 2);
    CHECK(picked->distance == doctest::Approx(90.0f));

    // An empty throne is still the throne, and an item in front of someone is still the item.
    c.pop_back();
    picked = Pick({ 0, 0, 100 }, { 0, 1, 0 }, kDefaultReach, c);
    REQUIRE(picked);
    CHECK(c[picked->index].id == 1);
    c[0].kind = Kind::Other;
    c.push_back({ BoxOf({ -20, -20, 0 }, { 20, 20, 130 }, kIdentity, { 0, 110, 0 }, 1.0f), 2, Kind::Person });
    picked = Pick({ 0, 0, 100 }, { 0, 1, 0 }, kDefaultReach, c);
    REQUIRE(picked);
    CHECK(c[picked->index].id == 1);
}

TEST_CASE("CJ looking down at an item on the floor picks it")
{
    // A sweetroll at his feet, a step ahead: the ray from his chest, pitched down by San Andreas's camera.
    const Vec3 origin{ 0, 0, kRayHeight };
    const auto item = BoxOf({ -6, -6, 0 }, { 6, 6, 5 }, kIdentity, { 0, 60, 0 }, 1.0f);
    const auto dir = Direction(0.0f, std::atan2(kRayHeight, 60.0f));
    std::vector<Candidate> c{ { item, 7 } };
    const auto picked = Pick(origin, dir, kDefaultReach, c);
    REQUIRE(picked);
    CHECK(picked->distance < kDefaultReach);
    CHECK_FALSE(Pick(origin, Direction(0.0f, 0.0f), kDefaultReach, c));  // looking straight ahead goes over it
}

TEST_CASE("a person without bounds gets a standing box")
{
    const auto box = StandingBox({ 100, 100, 0 }, 0.0f, 1.0f);
    CHECK(box.center.z == doctest::Approx(64.0f));
    CHECK(Hit({ 100, 0, kRayHeight }, { 0, 1, 0 }, box));
    CHECK_FALSE(Hit({ 100, 0, 200 }, { 0, 1, 0 }, box));  // over their head
    const auto sphere = SphereBox({ 0, 0, 0 }, 20.0f);
    CHECK(sphere.half.x == 20.0f);
    CHECK(Usable(sphere));
}

TEST_CASE("a wall between CJ and the target hides it, clutter and the target itself don't")
{
    CHECK(InSight(false, 0.0f, false, 100.0f));           // the sight ray met nothing
    CHECK(InSight(true, 100.0f, true, 100.0f));           // met the target
    CHECK(InSight(true, 140.0f, false, 100.0f));          // met the wall behind it
    CHECK(InSight(true, 90.0f, false, 100.0f));           // within the box's slack
    CHECK_FALSE(InSight(true, 50.0f, false, 100.0f));     // a wall in between
}

TEST_CASE("the prompt is Skyrim's activate text on one line, with the key")
{
    CHECK(Prompt("Open\nBreezehome", "Breezehome") == "E: Open Breezehome");
    CHECK(Prompt("<font color='#FF0000'>Steal</font>\nSweetroll", "Sweetroll") == "E: Steal Sweetroll");
    CHECK(Prompt("Talk\nLydia\n", "Lydia") == "E: Talk Lydia");
    CHECK(Prompt("  Search \r\n Chest ", "Chest") == "E: Search Chest");
    CHECK(Prompt("", "Lever") == "E: Lever");
    CHECK(Prompt("", "") == "");
    CHECK(Prompt("<font></font>", "Chest") == "");
}

TEST_CASE("a reference without a name gets no prompt, as in Skyrim")
{
    // The trigger boxes that showed "E: Activate" whichever way CJ looked, standing inside them.
    CHECK(Prompt("Activate", "") == "");
    CHECK(Prompt("Activate", "  ") == "");
    CHECK(Prompt("Activate\n", "\n") == "");
}

namespace
{
    // A claw door's three rings, one inside the other on a door facing south at y = 0: each ring's box is the
    // whole of its ring, so the inner ones sit inside the outer one's box.
    std::vector<Candidate> ClawRings()
    {
        std::vector<Candidate> rings;
        for (const float radius : { 90.0f, 60.0f, 30.0f }) {
            Candidate c;
            c.box.center = { 0.0f, 0.0f, 150.0f };
            c.box.half = { radius, 6.0f, radius };
            c.id = static_cast<std::uint32_t>(radius);
            rings.push_back(c);
        }
        return rings;
    }
}

TEST_CASE("E acts on the reference the crosshair is on, not on whichever box comes first")
{
    const auto  rings = ClawRings();
    const Vec3  camera{ 0.0f, -250.0f, 170.0f }, chest{ 0.0f, -80.0f, 140.0f };
    const Vec3  dir = Direction(0.0f, 0.0f);  // north, at the door
    // The ray hit the middle ring (its own collision): that one, though it is inside the outer ring's box.
    CHECK(Aimed(camera, dir, chest, 180.0f, rings, { 1, 244.0f }) == std::optional<std::size_t>(1));
    CHECK(Aimed(camera, dir, chest, 180.0f, rings, { 2, 244.0f }) == std::optional<std::size_t>(2));
    // Out of CJ's reach: not that one, nor anything else the ray meets beyond reach.
    const Vec3 far{ 0.0f, -600.0f, 140.0f };
    CHECK_FALSE(Aimed(camera, dir, far, 180.0f, rings, { 1, 244.0f }).has_value());
}

TEST_CASE("without a reference hit, the crosshair's box in front of the wall and in reach")
{
    // A person in front of a wall: the sight layer passes through people, so only their box says.
    std::vector<Candidate> people(1);
    people[0].box = StandingBox({ 0.0f, 100.0f, 0.0f }, 0.0f, 1.0f);
    people[0].kind = Kind::Person;
    const Vec3 camera{ 0.0f, -150.0f, 120.0f }, chest{ 0.0f, 0.0f, 100.0f };
    const Vec3 dir = Direction(0.0f, 0.0f);
    CHECK(Aimed(camera, dir, chest, 180.0f, people, { std::nullopt, 400.0f }) == std::optional<std::size_t>(0));
    // The wall comes first: hidden behind it.
    CHECK_FALSE(Aimed(camera, dir, chest, 180.0f, people, { std::nullopt, 150.0f }).has_value());
    // The ray met nothing at all: still them.
    CHECK(Aimed(camera, dir, chest, 180.0f, people, {}) == std::optional<std::size_t>(0));
    // Looking away from them: nothing, so E falls back to what CJ faces.
    CHECK_FALSE(Aimed(camera, Direction(Pose::kPi / 2, 0.0f), chest, 180.0f, people, {}).has_value());
    // A box the camera is inside (a trigger round CJ) is never what the crosshair is on.
    std::vector<Candidate> trigger(1);
    trigger[0].box = SphereBox(camera, 300.0f);
    CHECK_FALSE(Aimed(camera, dir, chest, 180.0f, trigger, {}).has_value());
}
