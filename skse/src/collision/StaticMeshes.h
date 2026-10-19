#pragma once

// Reads the real collision of the statics around the player (buildings, walls, rocks, bridges,
// tree trunks, interior rooms) straight from their Havok shapes, as triangles in Skyrim world
// units. Terrain is left out: the sampler's heightfield covers it. Ported from PerkeleRim.
// Main thread only.
namespace AhShitRim::StaticMeshes
{
    struct Chunk
    {
        std::vector<float> triangles;  // 9 floats each: three corners, counterclockwise seen from outside
        int                refs = 0;     // refs with wanted collision in the chunk
        int                pending = 0;  // statics in the chunk whose 3D hasn't loaded yet
        int                transformed = 0;  // children read through a transform wrapper
        int                rebuilt = 0;      // convex hulls whose planes didn't close, rebuilt from their vertices
        int                skipped = 0;      // shapes we can't turn into triangles (spheres, wrappers that didn't read right)
        std::map<int, int> otherLayers;      // bodies of these refs on layers left out, by COL_LAYER
        std::vector<RE::FormID> movers;      // refs with a body a script or an animation moves (gates, trapdoors), for Pose
    };

    // Triangles of every wanted static collision whose center lies in collision chunk (x, y) of
    // the loaded cells.
    Chunk Collect(std::int32_t a_chunkX, std::int32_t a_chunkY);

    // Where a ref's moving bodies are, in one point that changes with any shift or turn of them (Skyrim
    // units, roughly): false when it has none or no 3D. CollisionSampler reads chunks again once it
    // has changed and settled, since a lever opening a portcullis sends no open event.
    bool Pose(RE::TESObjectREFR* a_ref, RE::NiPoint3& a_pose);
}
