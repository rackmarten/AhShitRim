#include <doctest/doctest.h>

#include "../../cpp/corona_model.h"

using namespace AhShitRim;

namespace
{
    // A car standing at the world's origin facing +Y (x right, y forward, z up), its lamps where the ZR-350's are.
    Protocol::BodyLights ZrLamps(std::uint8_t a_lit)
    {
        Protocol::BodyLights lights{};
        lights.front[0] = 50;
        lights.front[1] = 169;
        lights.front[2] = 11;
        lights.rear[0] = 56;
        lights.rear[1] = -193;
        lights.rear[2] = 7;
        lights.lit = a_lit;
        return lights;
    }

    VehicleHits::Frame Upright()
    {
        VehicleHits::Frame frame;
        frame.pos = { 0.0f, 0.0f, 0.0f };
        frame.right = { 1.0f, 0.0f, 0.0f };
        frame.forward = { 0.0f, 1.0f, 0.0f };
        frame.up = { 0.0f, 0.0f, 1.0f };
        return frame;
    }

    bool Hidden(const Protocol::BodyState& a_pose, std::uint32_t a_bone) { return Body::Hidden(a_pose.hiddenParts, a_bone); }

    Body::Vec3 At(const Protocol::BodyState& a_pose, std::uint32_t a_bone)
    {
        return { a_pose.bones[a_bone].translation[0], a_pose.bones[a_bone].translation[1], a_pose.bones[a_bone].translation[2] };
    }
}

TEST_CASE("coronas: a quad per lamp, each a lamp's mesh on a bone of its own, clear until it burns")
{
    const auto model = CoronaModel::Build(28.0f, 20.0f);
    REQUIRE(model.meshes.size() == 4);
    CHECK(model.kind == Protocol::kSkinModelVehicle);
    CHECK(model.textures.size() == 2);
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto& mesh = model.meshes[i];
        CHECK(VehicleLights::LampOfMesh(mesh.flags) == VehicleLights::kLampBits[i]);
        CHECK((mesh.flags & Protocol::kSkinMeshAdditive) != 0);
        CHECK(mesh.color[3] == 0);
        CHECK(mesh.texture == (i < 2 ? 0 : 1));
        REQUIRE(mesh.vertices.size() == 4);
        for (const auto& v : mesh.vertices) {
            CHECK(v.bones[0] == i);
            CHECK(v.position[1] == 0.0f);  // in its bone's XZ plane, facing +Y
            CHECK(std::abs(v.position[0]) == doctest::Approx(i < 2 ? 28.0f : 20.0f));
        }
    }
    // The texture: clear at the corner, solid and nearly white in the middle; the red one red out from the middle.
    const auto& red = model.textures[1].rgba;
    const auto  pixel = [&](std::uint32_t x, std::uint32_t y) { return &red[(static_cast<std::size_t>(y) * CoronaModel::kTextureSize + x) * 4]; };
    CHECK(pixel(0, 0)[3] == 0);
    CHECK(pixel(32, 32)[3] == 255);
    CHECK(pixel(32, 32)[1] > 200);
    CHECK(pixel(32, 44)[0] > pixel(32, 44)[1] * 3);
}

TEST_CASE("coronas: each sits just out from its lamp, the left ones mirrored, and faces the camera")
{
    Protocol::BodyState pose{};
    const auto          lights = ZrLamps(0x0F);
    const Body::Vec3    camera{ 0.0f, 900.0f, 100.0f };  // ahead of the car
    const Body::Vec3    origin{ 0.0f, 0.0f, -50.0f };
    CHECK(CoronaModel::Pose(lights, Upright(), camera, origin, pose) == 2);
    CHECK(pose.boneCount == 4);
    // kToward along the line to the camera from just out of the lamp (relative to the origin).
    const auto expect = [&](const Body::Vec3& a_lamp) {
        const Body::Vec3 to = Body::Normalize(Body::Sub(camera, a_lamp));
        return Body::Sub(Body::Add(a_lamp, Body::Scale(to, CoronaModel::kToward)), origin);
    };
    const auto right = expect({ 50.0f, 169.0f + CoronaModel::kOut, 11.0f });
    const auto left = expect({ -50.0f, 169.0f + CoronaModel::kOut, 11.0f });
    CHECK(At(pose, 0).x == doctest::Approx(right.x));
    CHECK(At(pose, 0).y == doctest::Approx(right.y));
    CHECK(At(pose, 0).z == doctest::Approx(right.z));
    CHECK(At(pose, 1).x == doctest::Approx(left.x));
    CHECK(At(pose, 0).x > 45.0f);  // still by its own lamp, the left one mirrored
    CHECK(At(pose, 1).x < -45.0f);
    CHECK(!Hidden(pose, 0));
    CHECK(!Hidden(pose, 1));
    CHECK(Hidden(pose, 2));  // the tail lights' coronas, seen from in front of the car
    CHECK(Hidden(pose, 3));

    // The quad's +Y (its face) points at the camera.
    const auto       m = Body::ToMatrix(pose.bones[0]);
    const Body::Vec3 faceY{ m.m[0][1], m.m[1][1], m.m[2][1] };
    const Body::Vec3 lamp{ 50.0f, 173.0f, 11.0f };
    const Body::Vec3 want = Body::Normalize(Body::Sub(camera, lamp));
    CHECK(Body::Dot(faceY, want) == doctest::Approx(1.0f).epsilon(0.001));
    const Body::Vec3 faceX{ m.m[0][0], m.m[1][0], m.m[2][0] };
    CHECK(faceX.z == doctest::Approx(0.0f).epsilon(0.001));  // level
}

TEST_CASE("coronas: hidden while their lamp is out, behind the camera's side, or missing")
{
    Protocol::BodyState pose{};
    const Body::Vec3    behind{ 0.0f, -900.0f, 100.0f };
    CHECK(CoronaModel::Pose(ZrLamps(0x0F), Upright(), behind, {}, pose) == 2);
    CHECK(Hidden(pose, 0));
    CHECK(Hidden(pose, 1));
    CHECK(!Hidden(pose, 2));
    CHECK(!Hidden(pose, 3));

    CHECK(CoronaModel::Pose(ZrLamps(Protocol::kLightRearLeft), Upright(), behind, {}, pose) == 1);  // one smashed
    CHECK(Hidden(pose, 2));
    CHECK(!Hidden(pose, 3));

    auto bike = ZrLamps(0x0F);
    bike.rear[0] = bike.rear[1] = bike.rear[2] = 0;  // no tail light dummy (a BMX)
    CHECK(CoronaModel::Pose(bike, Upright(), behind, {}, pose) == 0);
}
