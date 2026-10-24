#include <doctest/doctest.h>

#include "../../cpp/skin_model.h"

using namespace AhShitRim;
using namespace AhShitRim::Protocol;

namespace
{
    SkinModel::ModelIn TwoMeshes()
    {
        SkinModel::ModelIn model;
        model.name = "player";
        model.boneIds = { 0, 1, 2, 3 };
        model.textures.push_back({ 2, 1, { 255, 0, 0, 255, 0, 255, 0, 128 } });
        SkinModel::MeshIn body;
        body.texture = 0;
        body.flags = kSkinMeshCutout;
        for (int i = 0; i < 3; i++) {
            SkinVertex v{};
            v.position[0] = static_cast<float>(i) * 70.0f;
            v.normal[2] = 1.0f;
            v.uv[0] = 0.5f;
            v.bones[0] = static_cast<std::uint8_t>(i);
            v.weights[0] = 1.0f;
            body.vertices.push_back(v);
        }
        body.indices = { 0, 1, 2 };
        model.meshes.push_back(body);
        SkinModel::MeshIn plain = body;
        plain.texture = -1;
        plain.flags = 0;
        plain.color[0] = 10;
        model.meshes.push_back(plain);
        return model;
    }

    std::vector<std::uint8_t> Reassemble(const std::vector<std::vector<std::uint8_t>>& a_chunks, SkinModel::Assembler& a_assembler)
    {
        for (std::size_t i = 0; i < a_chunks.size(); i++) {
            const auto result = a_assembler.Add(a_chunks[i].data(), static_cast<std::uint32_t>(a_chunks[i].size()));
            CHECK(result == (i + 1 == a_chunks.size() ? SkinModel::Assembler::Result::Complete : SkinModel::Assembler::Result::Partial));
        }
        return a_assembler.Blob();
    }
}

TEST_CASE("a model goes through serialize, chunks, the assembler and parse unchanged")
{
    const auto blob = SkinModel::Serialize(TwoMeshes());
    const auto chunks = SkinModel::Chunks(7, blob);
    REQUIRE(chunks.size() == 1);

    SkinModel::Assembler assembler;
    const auto           back = Reassemble(chunks, assembler);
    CHECK(assembler.Id() == 7);
    const auto model = SkinModel::Parse(back.data(), back.size());
    REQUIRE(model);
    CHECK(model->name == "player");
    CHECK(model->boneIds == std::vector<std::uint32_t>{ 0, 1, 2, 3 });
    REQUIRE(model->textures.size() == 1);
    CHECK(model->textures[0].width == 2);
    CHECK(model->textures[0].rgba[7] == 128);
    REQUIRE(model->meshes.size() == 2);
    CHECK(model->meshes[0].texture == 0);
    CHECK(model->meshes[0].flags == kSkinMeshCutout);
    CHECK(model->meshes[0].vertexCount == 3);
    CHECK(model->meshes[0].Vertex(2).position[0] == 140.0f);
    CHECK(model->meshes[0].Vertex(2).bones[0] == 2);
    CHECK(model->meshes[0].Index(1) == 1);
    CHECK(model->meshes[1].texture == -1);
    CHECK(model->meshes[1].color[0] == 10);
}

TEST_CASE("a big model is cut into chunks of at most kModelChunkMax and put back together")
{
    auto model = TwoMeshes();
    model.textures[0] = { 512, 512, std::vector<std::uint8_t>(512 * 512 * 4) };
    for (std::size_t i = 0; i < model.textures[0].rgba.size(); i++) {
        model.textures[0].rgba[i] = static_cast<std::uint8_t>(i * 7);
    }
    const auto blob = SkinModel::Serialize(model);
    const auto chunks = SkinModel::Chunks(3, blob);
    CHECK(chunks.size() == (blob.size() + kModelChunkMax - 1) / kModelChunkMax);
    for (const auto& chunk : chunks) {
        CHECK(chunk.size() <= sizeof(ModelChunkHeader) + kModelChunkMax);
    }
    SkinModel::Assembler assembler;
    CHECK(Reassemble(chunks, assembler) == blob);
    CHECK(SkinModel::Parse(blob.data(), blob.size()));
}

TEST_CASE("chunks out of order drop the model, and the next one still arrives")
{
    auto model = TwoMeshes();
    model.textures[0] = { 512, 512, std::vector<std::uint8_t>(512 * 512 * 4) };
    const auto chunks = SkinModel::Chunks(5, SkinModel::Serialize(model));
    REQUIRE(chunks.size() >= 3);
    SkinModel::Assembler assembler;
    CHECK(assembler.Add(chunks[0].data(), static_cast<std::uint32_t>(chunks[0].size())) == SkinModel::Assembler::Result::Partial);
    CHECK(assembler.Add(chunks[2].data(), static_cast<std::uint32_t>(chunks[2].size())) == SkinModel::Assembler::Result::Dropped);
    CHECK(assembler.Add(chunks[1].data(), static_cast<std::uint32_t>(chunks[1].size())) == SkinModel::Assembler::Result::Dropped);
    Reassemble(chunks, assembler);
    CHECK(assembler.Id() == 5);
}

TEST_CASE("parse refuses a truncated blob, a bone out of range and an index out of range")
{
    const auto blob = SkinModel::Serialize(TwoMeshes());
    CHECK_FALSE(SkinModel::Parse(blob.data(), blob.size() - 1));
    auto longer = blob;
    longer.push_back(0);
    CHECK_FALSE(SkinModel::Parse(longer.data(), longer.size()));

    auto badBone = TwoMeshes();
    badBone.meshes[0].vertices[0].bones[1] = 4;
    badBone.meshes[0].vertices[0].weights[1] = 0.5f;
    auto bytes = SkinModel::Serialize(badBone);
    CHECK_FALSE(SkinModel::Parse(bytes.data(), bytes.size()));

    auto unweightedBone = TwoMeshes();
    unweightedBone.meshes[0].vertices[0].bones[1] = 200;  // weight 0: ignored
    bytes = SkinModel::Serialize(unweightedBone);
    CHECK(SkinModel::Parse(bytes.data(), bytes.size()));

    auto badIndex = TwoMeshes();
    badIndex.meshes[1].indices[2] = 3;
    bytes = SkinModel::Serialize(badIndex);
    CHECK_FALSE(SkinModel::Parse(bytes.data(), bytes.size()));

    auto noBones = TwoMeshes();
    noBones.boneIds.clear();
    bytes = SkinModel::Serialize(noBones);
    CHECK_FALSE(SkinModel::Parse(bytes.data(), bytes.size()));
}

TEST_CASE("parse refuses NaN weights and positions")
{
    auto model = TwoMeshes();
    model.meshes[0].vertices[1].weights[2] = std::nanf("");
    auto bytes = SkinModel::Serialize(model);
    CHECK_FALSE(SkinModel::Parse(bytes.data(), bytes.size()));
    model = TwoMeshes();
    model.meshes[1].vertices[0].position[1] = std::nanf("");
    bytes = SkinModel::Serialize(model);
    CHECK_FALSE(SkinModel::Parse(bytes.data(), bytes.size()));
}

TEST_CASE("a weapon model keeps its kind, and a kind past the weapon's is refused")
{
    auto in = TwoMeshes();
    in.kind = kSkinModelWeapon;
    const auto blob = SkinModel::Serialize(in);
    const auto model = SkinModel::Parse(blob.data(), blob.size());
    REQUIRE(model);
    CHECK(model->kind == kSkinModelWeapon);

    in.kind = kSkinModelWeapon + 1;
    const auto bad = SkinModel::Serialize(in);
    CHECK_FALSE(SkinModel::Parse(bad.data(), bad.size()));
}

TEST_CASE("triangles back to back over the same corners are twins, whatever their vertices and order")
{
    auto in = TwoMeshes();
    auto& mesh = in.meshes[0];
    // The same corners again as their own vertices, normal turned round, wound the other way.
    for (int i = 0; i < 3; i++) {
        auto v = mesh.vertices[i];
        v.normal[2] = -1.0f;
        mesh.vertices.push_back(v);
    }
    mesh.indices.insert(mesh.indices.end(), { 3, 5, 4 });
    // A fourth corner and a triangle sharing an edge only.
    auto apart = mesh.vertices[0];
    apart.position[1] = 70.0f;
    mesh.vertices.push_back(apart);
    mesh.indices.insert(mesh.indices.end(), { 0, 1, 6 });

    const auto blob = SkinModel::Serialize(in);
    const auto model = SkinModel::Parse(blob.data(), blob.size());
    REQUIRE(model);
    CHECK(SkinModel::Twins(model->meshes[0]) == 1);
    CHECK(SkinModel::Twins(model->meshes[1]) == 0);
    // One pair among three triangles: still two-sided (the canopy's few twins hid its single faces from below).
    CHECK_FALSE(SkinModel::OneSided(model->meshes[0]));
    CHECK_FALSE(SkinModel::OneSided(model->meshes[1]));

    // Every face doubled, as a weapon's: one-sided.
    auto doubled = TwoMeshes();
    auto& faces = doubled.meshes[0];
    for (int i = 0; i < 3; i++) {
        auto v = faces.vertices[i];
        v.normal[2] = -1.0f;
        faces.vertices.push_back(v);
    }
    faces.indices.insert(faces.indices.end(), { 3, 5, 4 });
    const auto twoBlob = SkinModel::Serialize(doubled);
    const auto twoModel = SkinModel::Parse(twoBlob.data(), twoBlob.size());
    REQUIRE(twoModel);
    CHECK(SkinModel::OneSided(twoModel->meshes[0]));
}

TEST_CASE("model ids are odd and never the same twice, also within one tick of San Andreas's clock")
{
    std::uint32_t last = 0;
    std::uint32_t ids[5];
    for (auto& id : ids) {
        last = id = SkinModel::NextId(last, 1000);  // five models in one tick (the pickups near one spot, asked for in one frame)
    }
    for (int i = 0; i < 5; ++i) {
        CHECK(ids[i] % 2 == 1);
        for (int j = 0; j < i; ++j) {
            CHECK(ids[i] != ids[j]);
        }
    }
    CHECK(ids[1] > ids[0]);
    // A later tick starts from the clock again, still above the last.
    const auto later = SkinModel::NextId(last, 1016);
    CHECK(later > last);
    CHECK(later == (1016u << 4 | 1u));
    // The old rule, (tick << 4 | n) | 1, gave the 2nd and the 3rd model of a tick the same id.
    CHECK(((1000u << 4 | 2u) | 1u) == ((1000u << 4 | 3u) | 1u));
}
