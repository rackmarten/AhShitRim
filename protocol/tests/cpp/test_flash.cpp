#include <doctest/doctest.h>

#include "../../cpp/flash_model.h"

using namespace AhShitRim;

TEST_CASE("the muzzle flash is a model Skyrim can build: one bone, three crossed blended quads")
{
    const auto blob = SkinModel::Serialize(FlashModel::Build());
    const auto model = SkinModel::Parse(blob.data(), blob.size());
    REQUIRE(model);
    CHECK(model->kind == Protocol::kSkinModelWeapon);
    CHECK(model->boneIds.size() == 1);
    REQUIRE(model->textures.size() == 1);
    CHECK(model->textures[0].width == FlashModel::kTextureSize);
    REQUIRE(model->meshes.size() == 1);
    CHECK(model->meshes[0].vertexCount == 12);
    CHECK(model->meshes[0].indexCount == 18);
    CHECK((model->meshes[0].flags & Protocol::kSkinMeshBlend) != 0);
    for (std::uint32_t i = 0; i < model->meshes[0].vertexCount; ++i) {
        const auto v = model->meshes[0].Vertex(i);
        CHECK(v.weights[0] == 1.0f);
        CHECK(v.bones[0] == 0);
        for (int k = 0; k < 3; ++k) {
            CHECK(std::abs(v.position[k]) <= FlashModel::kHalfSize);
        }
    }
}

TEST_CASE("the glow is opaque in the middle and clear at the corners")
{
    const auto  rgba = FlashModel::Texture();
    const auto  at = [&](std::uint32_t x, std::uint32_t y) { return rgba[(y * FlashModel::kTextureSize + x) * 4 + 3]; };
    const auto  mid = FlashModel::kTextureSize / 2;
    CHECK(at(mid, mid) > 230);
    CHECK(at(0, 0) == 0);
    CHECK(at(FlashModel::kTextureSize - 1, FlashModel::kTextureSize - 1) == 0);
    CHECK(at(mid, 4) > 0);  // a spike up the middle column
}
