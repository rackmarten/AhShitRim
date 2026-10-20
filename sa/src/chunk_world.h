#pragma once

#include <cstdint>

class CPlayerPed;
class CVector;

// Skyrim's ground in San Andreas: builds the CollisionChunks Skyrim streams as collision objects
// around CJ, one invisible CBuilding with its own CColModel (triangles) per chunk, under the
// floating origin of protocol/cpp/ground.h (namespace Ground). Chunks more than Ground::kKeepRadius from CJ go away.
// Also holds CJ at the handover spot until the ground under him has arrived, and puts him back
// where he last stood when he ends up below it (FallRescue). Game thread only.
namespace AhShitRim::ChunkWorld
{
    // A new handover: drops every chunk and puts the origin's base at a_anchor (Skyrim units).
    void Begin(std::uint32_t a_handover, const float (&a_anchor)[3]);

    // Builds the chunks waiting in the link's ring. Call after the input was read this frame.
    void Receive();

    // Once a frame after the simulation: moves the origin under CJ when he is far out (a_spawn,
    // the handover spot, moves with it), drops far chunks, holds CJ at a_spawn until the ground
    // under it is in, and catches him when he falls through, unless a_following (a scene puts him
    // where Skyrim's player is, collision or not).
    void Update(CPlayerPed* a_ped, CVector& a_spawn, bool a_following = false);

    // Whether the ground under the handover spot is in and CJ has been let go.
    bool Ready();
}
