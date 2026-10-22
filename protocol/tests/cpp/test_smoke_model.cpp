#include <doctest/doctest.h>

#include "../../cpp/smoke_model.h"
#include "../../cpp/txd.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace AhShitRim;
using namespace AhShitRim::SmokeModel;

namespace
{
    SkinModel::TextureIn Sprite()
    {
        SkinModel::TextureIn t{ 4, 4, std::vector<std::uint8_t>(4 * 4 * 4, 255) };
        return t;
    }
}

TEST_CASE("smoke model: BlackSmoke's keys: it grows fast, then slowly, and fades in and out")
{
    CHECK(HalfSizeAt(0.0f) == doctest::Approx(0.1f));
    CHECK(HalfSizeAt(0.2f) == doctest::Approx(0.5f));
    CHECK(HalfSizeAt(1.0f) == doctest::Approx(0.6f));
    CHECK(AlphaAt(0.0f) == doctest::Approx(0.0f));
    CHECK(AlphaAt(0.2f) == doctest::Approx(128.0f));
    CHECK(AlphaAt(1.0f) == doctest::Approx(0.0f));
    CHECK(AlphaAt(0.6f) == doctest::Approx(64.0f));
}

TEST_CASE("smoke model: BlackSmoke's rate grows from none at a strength of 0.3 to 60 a second")
{
    CHECK(RateAt(0.0f) == 0.0f);
    CHECK(RateAt(0.3f) == 0.0f);
    CHECK(RateAt(0.65f) == doctest::Approx(30.0f));
    CHECK(RateAt(1.0f) == doctest::Approx(60.0f));
    CHECK(RateAt(2.0f) == doctest::Approx(60.0f));

    Trail half;
    for (int i = 0; i < 30; ++i) {
        half.Step(1.0f / 60.0f, RateAt(0.65f), { 0.0f, 0.0f, 0.0f });
    }
    CHECK(half.Sprites().size() >= 14);  // half a second at 30 a second
    CHECK(half.Sprites().size() <= 17);
}

TEST_CASE("smoke model: a black sprite of every size on its own bone, every size a blended mesh of its own fade")
{
    const auto model = Build(Sprite());
    CHECK(model.kind == Protocol::kSkinModelVehicle);
    REQUIRE(model.textures.size() == 1);
    REQUIRE(model.meshes.size() == static_cast<std::size_t>(kAges));
    CHECK(model.boneIds.size() == static_cast<std::size_t>(kBones));
    for (int age = 0; age < kAges; ++age) {
        const auto& mesh = model.meshes[age];
        CHECK(mesh.flags == Protocol::kSkinMeshBlend);
        CHECK(mesh.color[0] == 0);  // black
        CHECK(mesh.color[3] == AlphaOf(age));
        CHECK(mesh.vertices.size() == static_cast<std::size_t>(kPerAge) * 4);
        CHECK(mesh.indices.size() == static_cast<std::size_t>(kPerAge) * 6);
        for (const auto& v : mesh.vertices) {
            CHECK(v.bones[0] / kPerAge == age);
            CHECK(v.position[1] == 0.0f);  // in the bone's XZ plane
            CHECK(v.normal[1] == -1.0f);
        }
    }
    CHECK(SizeOf(kAges - 1) > SizeOf(0));  // they grow
    CHECK(AlphaOf(1) > 100);
    CHECK(AlphaOf(kAges - 1) < 20);  // and fade
    const auto blob = SkinModel::Serialize(model);
    CHECK(SkinModel::Parse(blob.data(), blob.size()));
}

TEST_CASE("smoke model: a sprite's quad faces the camera")
{
    const Vec3 at{ 0.0f, 0.0f, 0.0f };
    for (const Vec3 camera : { Vec3{ 0.0f, -500.0f, 0.0f }, Vec3{ 300.0f, 200.0f, 100.0f }, Vec3{ 0.0f, 0.0f, 800.0f } }) {
        const Body::Quat q = Facing(at, camera, 0.7f);
        Protocol::BodyBone bone{ { q.x, q.y, q.z, q.w }, { 0.0f, 0.0f, 0.0f } };
        const auto         m = Body::ToMatrix(bone);
        // The quad's normal (-Y) turned: towards the camera.
        Vec3 position, normal;
        Protocol::SkinVertex v{};
        v.normal[1] = -1.0f;
        v.weights[0] = 1.0f;
        Body::Skin(&m, 1, v, position, normal);
        const Vec3 toCamera = Body::Normalize(Body::Sub(camera, at));
        CHECK(Body::Dot(normal, toCamera) == doctest::Approx(1.0f).epsilon(0.001));
    }
}

TEST_CASE("smoke model: a trail lets out 60 sprites a second that rise, carried along, and go after 0.8 s")
{
    Trail      trail;
    const Vec3 engine{ 100.0f, 200.0f, 50.0f };
    trail.Step(0.016f, kRateMax, engine);
    REQUIRE(trail.Sprites().size() == 1);  // the first at once
    for (int i = 0; i < 30; ++i) {
        trail.Step(1.0f / 60.0f, kRateMax, engine);
    }
    const auto count = trail.Sprites().size();
    CHECK(count >= 29);
    CHECK(count <= 32);
    CHECK(trail.Sprites().front().at.z > engine.z + 0.5f * kMeter);  // rose
    for (int i = 0; i < 60; ++i) {
        trail.Step(1.0f / 60.0f, 0.0f, engine);
    }
    CHECK(trail.Empty());

    Trail carried;
    for (int i = 0; i < 10; ++i) {
        carried.Step(1.0f / 30.0f, kRateMax, engine, { 20.0f * kMeter, 0.0f, 0.0f });
    }
    CHECK(carried.Sprites().front().at.x > engine.x + 3.0f * kMeter);  // a moving vehicle's smoke goes with it
}

TEST_CASE("smoke model: the pose puts each sprite on a bone of its age, the rest far below")
{
    Trail trail;
    for (int i = 0; i < 90; ++i) {
        trail.Step(1.0f / 60.0f, kRateMax, { 0.0f, 0.0f, 0.0f });
    }
    Protocol::BodyState pose{};
    trail.Pose({ 10.0f, 20.0f, 30.0f }, { 0.0f, -1000.0f, 100.0f }, pose);
    CHECK(pose.boneCount == static_cast<std::uint32_t>(kBones));
    int shown = 0;
    for (int b = 0; b < kBones; ++b) {
        const auto& bone = pose.bones[b];
        const float q = bone.rotation[0] * bone.rotation[0] + bone.rotation[1] * bone.rotation[1] + bone.rotation[2] * bone.rotation[2] +
                        bone.rotation[3] * bone.rotation[3];
        CHECK(q == doctest::Approx(1.0f));
        if (bone.translation[2] > kHidden / 2.0f) {
            shown++;
        }
    }
    CHECK(shown == static_cast<int>(trail.Sprites().size()));  // every sprite found a bone of its age
    CHECK(pose.bones[0].translation[2] > kHidden / 2.0f);   // the youngest on a bone of the first size
}

// The player's own files, never in the repository. Without AHSR_SA_PATH this checks nothing.
TEST_CASE("The San Andreas install's effectsPC.txd has the black smoke's sprite")
{
    const char* sa = std::getenv("AHSR_SA_PATH");
    if (!sa || !*sa) {
        MESSAGE("AHSR_SA_PATH not set, skipped");
        return;
    }
    std::ifstream file(std::filesystem::path(sa) / "models/effectsPC.txd", std::ios::binary);
    REQUIRE(file);
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto                      textures = Txd::Read(data.data(), data.size());
    const auto*                     sprite = Txd::Find(textures, kTextureName);
    REQUIRE(sprite);
    CHECK(sprite->width >= 16);
    CHECK(sprite->rgba.size() == static_cast<std::size_t>(sprite->width) * sprite->height * 4);
}
