#include <doctest/doctest.h>

#include "../../cpp/puff_model.h"
#include "../../cpp/txd.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace AhShitRim;
using namespace AhShitRim::PuffModel;

TEST_CASE("puff model: San Andreas's spraycan and extinguisher prims")
{
    CHECK(kSprayCloud.additive);
    CHECK_FALSE(kHoseFroosh.additive);
    CHECK(kSprayCloud.size.At(0.0f) == doctest::Approx(0.1f));
    CHECK(kSprayCloud.size.At(1.0f) == doctest::Approx(2.5f));
    CHECK(kHoseFroosh.size.At(0.5f) == doctest::Approx(1.3f));
    CHECK(kHoseFroosh.force.At(1.0f) == doctest::Approx(5.0f));  // pulled down harder as it goes
    CHECK(kPutOutPuff.force.At(0.5f) < 0.0f);                    // the put-out puff rises
}

TEST_CASE("puff model: each age a mesh of its own shade and fade, the spray's added")
{
    const SkinModel::TextureIn sprite{ 4, 4, std::vector<std::uint8_t>(4 * 4 * 4, 255) };
    const auto                 spray = Build(kSprayCloud, sprite);
    REQUIRE(spray.meshes.size() == static_cast<std::size_t>(kAges));
    CHECK(spray.boneIds.size() == static_cast<std::size_t>(kBones));
    CHECK((spray.meshes[0].flags & Protocol::kSkinMeshAdditive) != 0);
    CHECK((spray.meshes[0].flags & Protocol::kSkinMeshBlend) != 0);
    CHECK(spray.meshes[0].color[1] > spray.meshes[kAges - 1].color[1]);  // green fading to black
    CHECK(spray.meshes[0].color[3] > spray.meshes[kAges - 1].color[3]);
    CHECK(spray.meshes[0].vertices[1].position[0] < spray.meshes[kAges - 1].vertices[1].position[0]);  // growing

    const auto froosh = Build(kHoseFroosh, sprite);
    CHECK((froosh.meshes[0].flags & Protocol::kSkinMeshAdditive) == 0);
    CHECK(froosh.meshes[3].color[0] == 255);
}

TEST_CASE("puff model: a sprite's line strays no more than the prim's angle")
{
    const Vec3 axis{ 0.0f, 1.0f, 0.0f };
    for (int i = 0; i < 50; ++i) {
        const Vec3 dir = Spread(axis, 20.0f, SmokeModel::Hash(i * 2u), SmokeModel::Hash(i * 2u + 1u));
        CHECK(std::sqrt(Body::Dot(dir, dir)) == doctest::Approx(1.0f));
        CHECK(Body::Dot(dir, axis) >= std::cos(20.0f * 0.0174532925f) - 1e-4f);
    }
    const Vec3 straight = Spread(Vec3{ 0.0f, 0.0f, 1.0f }, 0.0f, 0.7f, 0.3f);
    CHECK(straight.z == doctest::Approx(1.0f));
}

TEST_CASE("puff model: a cloud comes out along the nozzle while it sprays, and goes")
{
    Cloud      cloud(kHoseFroosh);
    const Vec3 nozzle{ 0.0f, 0.0f, 100.0f };
    const Vec3 north{ 0.0f, 1.0f, 0.0f };
    for (int i = 0; i < 25; ++i) {
        cloud.Step(0.02f, true, nozzle, north);
    }
    // 0.5 s at 50 a second, the first at once.
    CHECK(cloud.Sprites().size() >= 24);
    CHECK(cloud.Sprites().size() <= 27);
    for (const auto& sprite : cloud.Sprites()) {
        CHECK(sprite.at.y >= 0.0f);           // ahead of the nozzle
        CHECK(sprite.at.y <= 3.0f * kMeter);  // no further than 3 m/s for its age
    }
    CHECK(cloud.Sprites().front().at.z < nozzle.z);  // the oldest pulled down
    for (int i = 0; i < 60; ++i) {
        cloud.Step(0.02f, false);
    }
    CHECK(cloud.Empty());
}

TEST_CASE("puff model: a burst rises where a fire went out, and the pose hides the unused bones")
{
    Cloud cloud(kPutOutPuff);
    cloud.Burst({ 500.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, kPutOutCount, kPutOutPuff.speed, kPutOutPuff.angle);
    CHECK(cloud.Sprites().size() == static_cast<std::size_t>(kPutOutCount));
    cloud.Step(0.5f, false);
    for (const auto& sprite : cloud.Sprites()) {
        CHECK(sprite.at.z > 0.0f);
    }
    Protocol::BodyState pose{};
    cloud.Pose({ 500.0f, 0.0f, 0.0f }, { 0.0f, -500.0f, 100.0f }, pose);
    CHECK(pose.boneCount == static_cast<std::uint32_t>(kBones));
    int shown = 0;
    for (int b = 0; b < kBones; ++b) {
        shown += pose.bones[b].translation[2] > kHidden / 2.0f ? 1 : 0;
    }
    CHECK(shown == kPutOutCount);
}

// The player's own files, never in the repository. Without AHSR_SA_PATH this checks nothing.
TEST_CASE("The San Andreas install's effectsPC.txd has the clouds' sprites")
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
    for (const auto* prim : { &kSprayCloud, &kHoseFroosh, &kHoseChunks, &kPutOutPuff }) {
        INFO(prim->texture);
        CHECK(Txd::Find(textures, prim->texture));
    }
}
