#include <doctest/doctest.h>

#include "../../cpp/smoke_model.h"

using namespace AhShitRim;
using namespace AhShitRim::SmokeModel;

TEST_CASE("smoke model: a black puff texture, soft to nothing at the edge")
{
    const auto rgba = Texture();
    REQUIRE(rgba.size() == static_cast<std::size_t>(kTextureSize) * kTextureSize * 4);
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return &rgba[(static_cast<std::size_t>(y) * kTextureSize + x) * 4]; };
    const auto* middle = at(kTextureSize / 2, kTextureSize / 2);
    CHECK(middle[0] == kShade);
    CHECK(middle[0] < 40);  // black
    CHECK(middle[3] > 200);
    CHECK(at(0, 0)[3] == 0);  // the corners are clear
    CHECK(at(kTextureSize - 1, kTextureSize / 2)[3] < 20);
}

TEST_CASE("smoke model: puffs of every size, each its own bone, every size a blended mesh of its own fade")
{
    const auto model = Build();
    CHECK(model.kind == Protocol::kSkinModelVehicle);
    REQUIRE(model.meshes.size() == static_cast<std::size_t>(kAges));
    CHECK(model.boneIds.size() == static_cast<std::size_t>(kBones));
    for (int age = 0; age < kAges; ++age) {
        const auto& mesh = model.meshes[age];
        CHECK(mesh.flags == Protocol::kSkinMeshBlend);
        CHECK(mesh.vertices.size() == static_cast<std::size_t>(kPerAge) * 12);
        CHECK(mesh.indices.size() == static_cast<std::size_t>(kPerAge) * 18);
        CHECK(mesh.color[3] == AlphaOf(age));
        for (const auto& v : mesh.vertices) {
            CHECK(v.bones[0] / kPerAge == age);
        }
    }
    CHECK(SizeOf(kAges - 1) > SizeOf(0) * 3.0f);  // they grow
    CHECK(AlphaOf(0) > 200);
    CHECK(AlphaOf(kAges - 1) < AlphaOf(0) / 3);  // and fade
    const auto blob = SkinModel::Serialize(model);
    CHECK(SkinModel::Parse(blob.data(), blob.size()));
}

TEST_CASE("smoke model: a trail lets out puffs that rise, grow old and go")
{
    Trail      trail;
    const Vec3 engine{ 100.0f, 200.0f, 50.0f };
    trail.Step(0.016f, true, engine);
    REQUIRE(trail.Puffs().size() == 1);  // the first at once
    for (int i = 0; i < 60; ++i) {
        trail.Step(1.0f / 60.0f, true, engine);
    }
    const auto count = trail.Puffs().size();
    CHECK(count >= 13);  // about one every kEvery
    CHECK(count <= 16);
    const auto& oldest = trail.Puffs().front();
    CHECK(oldest.at.z > engine.z + 1.0f * kMeter);  // rose
    // Stops letting out puffs, the ones out go after kLife.
    for (int i = 0; i < 60 * 4; ++i) {
        trail.Step(1.0f / 60.0f, false, engine);
    }
    CHECK(trail.Empty());
}

TEST_CASE("smoke model: a moving vehicle leaves its puffs behind")
{
    Trail trail;
    for (int i = 0; i < 30; ++i) {
        trail.Step(1.0f / 30.0f, true, { static_cast<float>(i) * 50.0f, 0.0f, 0.0f });
    }
    CHECK(trail.Puffs().front().at.x < trail.Puffs().back().at.x - 1000.0f);
}

TEST_CASE("smoke model: the pose puts each puff on a bone of its age, the rest far below")
{
    Trail trail;
    for (int i = 0; i < 90; ++i) {
        trail.Step(1.0f / 30.0f, true, { 0.0f, 0.0f, 0.0f });
    }
    Protocol::BodyState pose{};
    const Vec3          origin{ 10.0f, 20.0f, 30.0f };
    trail.Pose(origin, pose);
    CHECK(pose.boneCount == static_cast<std::uint32_t>(kBones));
    int shown = 0;
    for (int b = 0; b < kBones; ++b) {
        const auto& bone = pose.bones[b];
        const float q = bone.rotation[0] * bone.rotation[0] + bone.rotation[1] * bone.rotation[1] + bone.rotation[2] * bone.rotation[2] +
                        bone.rotation[3] * bone.rotation[3];
        CHECK(q == doctest::Approx(1.0f));
        if (bone.translation[2] > kHidden / 2.0f) {
            shown++;
            CHECK(std::abs(bone.translation[0] + 10.0f) <= kSpread * kLife + 1.0f);  // let out at 0, drifted a little
        }
    }
    CHECK(shown == static_cast<int>(trail.Puffs().size()));  // every puff found a bone of its age
    // The youngest puff sits on a bone of the first size.
    CHECK(pose.bones[0].translation[2] > kHidden / 2.0f);
}
