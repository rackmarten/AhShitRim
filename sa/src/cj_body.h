#pragma once

class CPlayerPed;
class CVector;

// CJ's body for Skyrim to draw (protocol v5).
// - His model: the skinned geometry of his clump (as CClothes built it), its textures read from
//   their Direct3D 9 textures and its skin, sent once over the ring to Skyrim (ModelChunk) every
//   time the link comes up, and again whenever his clump changes.
// - His skeleton: San Andreas only poses a ped's bones when it is on screen and drawn, and nothing
//   is drawn while hidden, so CJ's animations are always updated in full (CEntity::UpdateAnim
//   hook) and his bones posed by hand after each frame; with San Andreas's active camera they go
//   to Skyrim every frame as a BodyState.
// Game thread only.
namespace AhShitRim::CjBody
{
    void Install();  // at ASI load

    // Once a frame after the simulation (and after ChunkWorld::Update moved the origin): a_spawn is
    // where this handover put CJ (his position, not his feet), a_lift how far above the ground he
    // was put there.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame);

    // CJ's pelvis joint relative to his position as of the last Update (metres); false before one.
    bool Pelvis(CVector& a_offset);
}
