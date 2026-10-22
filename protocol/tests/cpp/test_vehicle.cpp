#include <doctest/doctest.h>

#include "../../cpp/vehicle.h"

using namespace AhShitRim;
using namespace AhShitRim::Vehicle;

namespace
{
    Ground Flat(float a_z)
    {
        Ground g;
        g.hit.fill(true);
        g.z.fill(a_z);
        return g;
    }

    // A one-bone vertex at p with normal n.
    Protocol::SkinVertex Vertex(Vec3 a_p, Vec3 a_n = { 0, 0, 1 })
    {
        Protocol::SkinVertex v{};
        v.position[0] = a_p.x;
        v.position[1] = a_p.y;
        v.position[2] = a_p.z;
        v.normal[0] = a_n.x;
        v.normal[1] = a_n.y;
        v.normal[2] = a_n.z;
        v.weights[0] = 1.0f;
        return v;
    }

    SkinModel::MeshIn Triangle(std::int32_t a_texture, std::uint8_t a_bone)
    {
        SkinModel::MeshIn m;
        m.texture = a_texture;
        for (const Vec3& p : { Vec3{ 0, 0, 0 }, Vec3{ 1, 0, 0 }, Vec3{ 0, 1, 0 } }) {
            m.vertices.push_back(Vertex(p));
            m.vertices.back().bones[0] = a_bone;
        }
        m.indices = { 0, 1, 2 };
        return m;
    }
}

TEST_CASE("the first spot is straight ahead, across CJ's path, its near side two metres out")
{
    const Footprint car{ 1.0f, 2.5f };
    const auto      spots = Candidates({ 10.0f, 20.0f, 500.0f }, { 0.0f, 1.0f, 0.0f }, car);  // CJ faces north
    REQUIRE(spots.size() == static_cast<std::size_t>(kRings) * std::size(kTurns));
    CHECK(spots[0].center.x == doctest::Approx(10.0f));
    CHECK(spots[0].center.y == doctest::Approx(23.0f));  // half its width plus the gap
    CHECK(spots[0].center.z == 500.0f);
    // Facing west (across CJ's path), San Andreas heading +90 degrees.
    CHECK(spots[0].forward.x == doctest::Approx(-1.0f));
    CHECK(spots[0].forward.y == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(spots[0].heading == doctest::Approx(1.5708f).epsilon(1e-4));
    // Then turned 30 degrees either way, and the next ring two metres further out.
    CHECK(spots[1].center.x == doctest::Approx(10.0f - 3.0f * 0.5f).epsilon(1e-4));
    CHECK(spots[2].center.x == doctest::Approx(10.0f + 3.0f * 0.5f).epsilon(1e-4));
    CHECK(spots[5].center.y == doctest::Approx(25.0f));
    // A bike parks closer: it is narrow.
    CHECK(Candidates({}, { 1.0f, 0.0f, 0.0f }, { 0.4f, 1.0f })[0].center.x == doctest::Approx(2.4f));
}

TEST_CASE("the ground is sampled at the footprint's corners, sides and middle")
{
    Spot spot;
    spot.center = { 0.0f, 0.0f, 3.0f };
    spot.forward = { 1.0f, 0.0f, 0.0f };  // facing east: length along x
    const auto points = SamplePoints(spot, { 1.0f, 2.0f });
    CHECK(points[0].x == doctest::Approx(-1.8f));
    CHECK(points[0].y == doctest::Approx(0.9f));  // left of the vehicle is north
    CHECK(points[3].x == doctest::Approx(1.8f));
    CHECK(points[3].y == doctest::Approx(-0.9f));
    CHECK(points[8].x == doctest::Approx(0.0f));
    CHECK(points[8].y == doctest::Approx(0.0f));
    CHECK(points[8].z == 3.0f);
}

TEST_CASE("a spot will do when its ground is all there, flat enough and near CJ's height")
{
    CHECK(GroundHeight(Flat(500.2f), 500.0f) == doctest::Approx(500.2f));

    Ground slope = Flat(500.0f);
    slope.z[2] = slope.z[3] = slope.z[5] = 500.7f;  // the far side 0.7 m higher: a gentle slope
    CHECK(GroundHeight(slope, 500.0f).has_value());

    Ground rock = Flat(500.0f);
    rock.z[8] = 501.5f;  // a rock in the middle
    CHECK_FALSE(GroundHeight(rock, 500.0f).has_value());
    CHECK(GroundHeight(rock, 500.0f, 1.5f).has_value());  // an aircraft's wider footprint takes more (VehicleKinds::Traits)

    Ground hole = Flat(500.0f);
    hole.hit[4] = false;
    CHECK_FALSE(GroundHeight(hole, 500.0f).has_value());

    CHECK_FALSE(GroundHeight(Flat(498.0f), 500.0f).has_value());  // down a bank
    CHECK_FALSE(GroundHeight(Flat(502.0f), 500.0f).has_value());  // up on a ledge

    Ground nan = Flat(500.0f);
    nan.z[0] = std::nanf("");
    CHECK_FALSE(GroundHeight(nan, 500.0f).has_value());
}

TEST_CASE("paint placeholders take the vehicle's colours and the lights turn white")
{
    const std::array<Rgb, 4> colours = { Rgb{ 10, 20, 30 }, Rgb{ 40, 50, 60 }, Rgb{ 70, 80, 90 }, Rgb{ 1, 2, 3 } };
    CHECK(MaterialColour({ 60, 255, 0 }, false, colours) == colours[0]);
    CHECK(MaterialColour({ 255, 0, 175 }, false, colours) == colours[1]);
    CHECK(MaterialColour({ 0, 255, 255 }, false, colours) == colours[2]);
    CHECK(MaterialColour({ 255, 0, 255 }, false, colours) == colours[3]);
    CHECK(MaterialColour({ 255, 175, 0 }, true, colours) == Rgb{});  // a headlight placeholder
    CHECK(MaterialColour({ 30, 30, 30 }, false, colours) == Rgb{ 30, 30, 30 });
}

TEST_CASE("a plain rotation bakes nothing; a scaled or mirrored wheel bakes its scale and mirror")
{
    Body::RwRows turned;  // 90 degrees about Z
    turned.right = { 0, 1, 0 };
    turned.up = { -1, 0, 0 };
    turned.at = { 0, 0, 1 };
    CHECK(IsIdentity(Bake(turned)));

    Body::RwRows wheel = turned;  // the same, scaled to a 0.7 m wheel and mirrored across its axle
    wheel.right = Body::Scale(turned.right, -0.7f);
    wheel.up = Body::Scale(turned.up, 0.7f);
    wheel.at = Body::Scale(turned.at, 0.7f);
    const auto b = Bake(wheel);
    CHECK(b.Determinant() == doctest::Approx(-0.343f).epsilon(1e-4));

    // The rotation Body::ToBone sends every frame, applied to the baked vertex, lands where the
    // frame itself takes the vertex.
    const auto bone = Body::ToBone(wheel, {});
    const auto r = Body::ToMatrix(bone);
    const Vec3 v{ 0.3f, -0.2f, 0.5f };
    const Vec3 baked = b * v;
    const Vec3 posed{ r.m[0][0] * baked.x + r.m[0][1] * baked.y + r.m[0][2] * baked.z, r.m[1][0] * baked.x + r.m[1][1] * baked.y + r.m[1][2] * baked.z,
        r.m[2][0] * baked.x + r.m[2][1] * baked.y + r.m[2][2] * baked.z };
    const Vec3 direct = Body::RotateBy(wheel, v);
    CHECK(posed.x == doctest::Approx(direct.x));
    CHECK(posed.y == doctest::Approx(direct.y));
    CHECK(posed.z == doctest::Approx(direct.z));
}

TEST_CASE("baking a mirror turns triangles round and keeps normals pointing out")
{
    Matrix33 mirror;
    mirror.m[0][0] = -2.0f;  // mirrored and stretched along x
    std::vector<SkinModel::MeshIn> meshes{ Triangle(0, 0) };
    meshes[0].vertices[1].normal[0] = 1.0f;  // a normal pointing +x...
    meshes[0].vertices[1].normal[2] = 0.0f;
    BakeMeshes(meshes, mirror);
    CHECK(meshes[0].vertices[1].position[0] == doctest::Approx(-2.0f));
    CHECK(meshes[0].vertices[1].normal[0] == doctest::Approx(-1.0f));  // ... now points -x, still unit length
    CHECK(meshes[0].vertices[0].normal[2] == doctest::Approx(1.0f));
    CHECK(meshes[0].indices == std::vector<std::uint16_t>{ 0, 2, 1 });
}

TEST_CASE("meshes that look the same are merged, the rest kept apart")
{
    std::vector<SkinModel::MeshIn> parts{ Triangle(0, 0), Triangle(1, 1), Triangle(0, 2) };
    parts[2].color[0] = 128;  // tinted: a mesh of its own
    parts.push_back(Triangle(0, 3));
    const auto merged = Merge(parts);
    REQUIRE(merged.size() == 3);
    CHECK(merged[0].texture == 0);
    CHECK(merged[0].vertices.size() == 6);
    CHECK(merged[0].indices == std::vector<std::uint16_t>{ 0, 1, 2, 3, 4, 5 });
    CHECK(merged[0].vertices[3].bones[0] == 3);  // each vertex keeps its part
    CHECK(merged[1].texture == 1);
    CHECK(merged[2].color[0] == 128);
}

TEST_CASE("a vehicle model says so, and survives serialize and parse")
{
    SkinModel::ModelIn model;
    model.name = "Sandking";
    model.kind = Protocol::kSkinModelVehicle;
    model.boneIds = { 1, 2 };
    model.meshes = { Triangle(-1, 1) };
    const auto blob = SkinModel::Serialize(model);
    const auto parsed = SkinModel::Parse(blob.data(), blob.size());
    REQUIRE(parsed);
    CHECK(parsed->kind == Protocol::kSkinModelVehicle);
    CHECK(parsed->name == "Sandking");

    auto bad = blob;
    bad[44] = 7;  // SkinModelHeader.kind: unknown
    CHECK_FALSE(SkinModel::Parse(bad.data(), bad.size()));
}

TEST_CASE("in the air the player's car is pushed half as hard again as San Andreas does, up to half as fast again")
{
    // At rest: San Andreas pushes once, AhShitRim half a push more, either way.
    CHECK(ExtraAirPush(0.0f, -1.0f) == doctest::Approx(kAirTurnGain - 1.0f));
    CHECK(ExtraAirPush(0.0f, 1.0f) == doctest::Approx(kAirTurnGain - 1.0f));
    // Past San Andreas's cap that way, AhShitRim pushes alone, all of it.
    CHECK(ExtraAirPush(-0.025f, 1.0f) == doctest::Approx(kAirTurnGain));
    CHECK(ExtraAirPush(0.025f, -1.0f) == doctest::Approx(kAirTurnGain));
    // Past AhShitRim's cap that way, nothing.
    CHECK(ExtraAirPush(-0.04f, 1.0f) == 0.0f);
    CHECK(ExtraAirPush(0.04f, -1.0f) == 0.0f);
    // Against a spin, however fast, as San Andreas: both push.
    CHECK(ExtraAirPush(0.2f, 1.0f) == doctest::Approx(kAirTurnGain - 1.0f));
    CHECK(ExtraAirPush(-0.2f, -1.0f) == doctest::Approx(kAirTurnGain - 1.0f));
    // No steering, no push.
    CHECK(ExtraAirPush(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("the handbrake makes left and right yaw; on the gas AhShitRim gives all of roll and pitch")
{
    const AirSteer steer{ 1.0f, -1.0f };
    const auto     free = ExtraAirPushes(0.0f, 0.0f, 0.0f, steer, false, false);
    CHECK(free.yaw == 0.0f);
    CHECK(free.roll > 0.0f);
    CHECK(free.pitch > 0.0f);

    const auto handbrake = ExtraAirPushes(0.0f, 0.0f, 0.0f, steer, true, false);
    CHECK(handbrake.yaw > 0.0f);
    CHECK(handbrake.roll == 0.0f);
    CHECK(handbrake.pitch > 0.0f);

    // San Andreas pushes no roll or pitch on the gas: AhShitRim's are the whole of it.
    const auto gas = ExtraAirPushes(0.0f, 0.0f, 0.0f, steer, false, true);
    CHECK(gas.yaw == 0.0f);
    CHECK(gas.roll == doctest::Approx(kAirTurnGain));
    CHECK(gas.pitch == doctest::Approx(kAirTurnGain));
    CHECK(ExtraAirPushes(0.0f, 0.0f, 0.04f, steer, false, true).pitch == 0.0f);  // still up to kAirSpin

    CHECK(ExtraAirPushes(0.0f, 0.0f, 0.0f, steer, true, true).yaw > 0.0f);  // yaw still works on the gas
}

TEST_CASE("a bike yaws in the air with left and right, all of it AhShitRim's")
{
    CHECK(BikeAirYaw(0.0f, -1.0f) == doctest::Approx(kAirTurnGain));
    CHECK(BikeAirYaw(0.0f, 1.0f) == doctest::Approx(kAirTurnGain));
    CHECK(BikeAirYaw(0.025f, -1.0f) == doctest::Approx(kAirTurnGain));  // San Andreas's cap means nothing here
    CHECK(BikeAirYaw(-0.04f, 1.0f) == 0.0f);                            // up to kAirSpin
    CHECK(BikeAirYaw(0.04f, 1.0f) == doctest::Approx(kAirTurnGain));     // against a spin, always
    CHECK(BikeAirYaw(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("the mouse steers in the air only while the keys don't")
{
    const auto keys = AirSteering(-128, 0, true, 100.0f, 100.0f);
    CHECK(keys.leftRight == doctest::Approx(-1.0f));
    CHECK(keys.upDown == 0.0f);

    const auto mouse = AirSteering(0, 0, true, 25.0f, -500.0f);
    CHECK(mouse.leftRight == doctest::Approx(0.5f));
    CHECK(mouse.upDown == doctest::Approx(-1.5f));

    const auto off = AirSteering(0, 0, false, 25.0f, -500.0f);
    CHECK(off.leftRight == 0.0f);
    CHECK(off.upDown == 0.0f);
}

TEST_CASE("a push's force counts the turn mass up to 3000")
{
    CHECK(AirPushForce(0.0007f, 2000.0f) == doctest::Approx(1.4f));
    CHECK(AirPushForce(0.0007f, 4000.0f) == doctest::Approx(2.1f));
}
