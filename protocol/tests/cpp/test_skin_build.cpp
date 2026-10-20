#include <doctest/doctest.h>

#include "../../cpp/skin_build.h"

using namespace AhShitRim;
using namespace AhShitRim::Protocol;

namespace
{
    // A square of two triangles in the XY plane, facing up (normals +Z), on two materials; the
    // second triangle winds the other way round.
    struct Square
    {
        float         positions[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        float         normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        float         uvs[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
        std::uint32_t bones[4] = { 0x00000100, 0x00000001, 0x00000002, 0x00000003 };
        float         weights[16] = { 0.5f, 1.5f, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };
        std::uint16_t triangles[8] = { 0, 1, 2, 0, 0, 3, 2, 1 };

        SkinBuild::Geometry Geometry(bool a_withNormals = true)
        {
            SkinBuild::Geometry g;
            g.vertexCount = 4;
            g.positions = positions;
            g.normals = a_withNormals ? normals : nullptr;
            g.uvs = uvs;
            g.boneIndices = bones;
            g.weights = weights;
            g.triangleCount = 2;
            g.triangles = triangles;
            return g;
        }
    };

    Body::Vec3 Face(const SkinModel::MeshIn& a_mesh, int a_triangle)
    {
        auto at = [&](int k) {
            const auto& v = a_mesh.vertices[a_mesh.indices[a_triangle * 3 + k]];
            return Body::Vec3{ v.position[0], v.position[1], v.position[2] };
        };
        return Body::Cross(Body::Sub(at(1), at(0)), Body::Sub(at(2), at(0)));
    }
}

TEST_CASE("each material becomes a mesh with only its own vertices, in Skyrim units")
{
    Square                 square;
    SkinBuild::Stats       stats;
    std::vector<SkinBuild::Material> materials(2);
    materials[0].texture = 0;
    materials[1].texture = 1;
    materials[1].flags = kSkinMeshCutout;
    const auto meshes = SkinBuild::Meshes(square.Geometry(), materials, &stats);
    REQUIRE(meshes.size() == 2);
    CHECK(meshes[0].texture == 0);
    CHECK(meshes[1].flags == kSkinMeshCutout);
    CHECK(meshes[0].vertices.size() == 3);
    CHECK(meshes[0].indices.size() == 3);
    CHECK(meshes[1].vertices.size() == 3);
    CHECK(meshes[0].vertices[1].position[0] == doctest::Approx(Pose::kUnitsPerMeter));
    CHECK(meshes[0].vertices[2].uv[1] == 1.0f);
}

TEST_CASE("triangles face the way their normals do, in Skyrim's winding")
{
    Square           square;
    SkinBuild::Stats stats;
    const auto       meshes = SkinBuild::Meshes(square.Geometry(), std::vector<SkinBuild::Material>(2), &stats);
    REQUIRE(meshes.size() == 2);
    CHECK(Face(meshes[0], 0).z > 0.0f);
    CHECK(Face(meshes[1], 0).z > 0.0f);  // was wound the other way
    CHECK(stats.flipped == 1);
}

TEST_CASE("bone indices unpack lowest byte first, weights add up to one, and an unweighted vertex follows bone 0")
{
    Square           square;
    SkinBuild::Stats stats;
    const auto       meshes = SkinBuild::Meshes(square.Geometry(), std::vector<SkinBuild::Material>(2), &stats);
    const auto&      v0 = meshes[0].vertices[0];
    CHECK(v0.bones[0] == 0);
    CHECK(v0.bones[1] == 1);
    CHECK(v0.weights[0] == doctest::Approx(0.25f));
    CHECK(v0.weights[1] == doctest::Approx(0.75f));
    CHECK(meshes[0].vertices[2].bones[0] == 2);
    // Vertex 3 (in the second mesh) has no weight at all.
    CHECK(stats.unweighted == 1);
    bool found = false;
    for (const auto& v : meshes[1].vertices) {
        if (v.position[1] > 0.0f && v.position[0] == 0.0f) {
            found = true;
            CHECK(v.bones[0] == 0);
            CHECK(v.weights[0] == 1.0f);
        }
    }
    CHECK(found);
}

TEST_CASE("a geometry without normals gets them from its triangles, and bad triangles are dropped")
{
    Square square;
    square.triangles[5] = 9;  // a vertex out of range
    square.triangles[7] = 5;  // and a material out of range
    SkinBuild::Stats stats;
    const auto       meshes = SkinBuild::Meshes(square.Geometry(false), std::vector<SkinBuild::Material>(2), &stats);
    REQUIRE(meshes.size() == 1);
    CHECK(meshes[0].vertices[0].normal[2] == doctest::Approx(1.0f));
    CHECK(stats.dropped == 1);
}

TEST_CASE("weights on a bone the skeleton doesn't have are dropped, NaN weights too")
{
    Square square;
    square.weights[4] = std::nanf("");  // vertex 1: its only weight
    auto geometry = square.Geometry();
    geometry.boneCount = 1;  // vertex 0's second bone (1) and vertex 2's bone (2) are past it
    SkinBuild::Stats stats;
    const auto       meshes = SkinBuild::Meshes(geometry, std::vector<SkinBuild::Material>(2), &stats);
    CHECK(stats.foreignBones == 4);  // vertices 0 and 2, once in each mesh
    const auto& v0 = meshes[0].vertices[0];
    CHECK(v0.weights[0] == doctest::Approx(1.0f));
    CHECK(v0.weights[1] == 0.0f);
    CHECK(meshes[0].vertices[1].weights[0] == 1.0f);  // unweighted now: bone 0 takes it
    CHECK(meshes[0].vertices[1].bones[0] == 0);
}
