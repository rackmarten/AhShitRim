#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../ahshitrim_protocol.h"

class CPlayerPed;
class CVector;
struct RpClump;
struct RwMatrixTag;

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

    // Where his shots from the hip go and his arms point: straight ahead of his chest, level (Aim::ChestFacing),
    // from his bones as last posed (his heading before they are).
    CVector HipFacing(CPlayerPed* a_ped);

    // CJ's pelvis joint relative to his position as of the last Update (metres); false before one.
    bool Pelvis(CVector& a_offset);

    // The world matrix of the bone with this id (San Andreas's ePedBones: 24 is the right hand, 34 the
    // left) as last posed; false when he has no such bone.
    bool BoneWorld(CPlayerPed* a_ped, int a_boneId, RwMatrixTag& a_out);

    // San Andreas's real-time clock the body slots are stamped with (milliseconds since the first call).
    std::uint32_t NowMs();

    // Another skinned clump read the way CJ's is (v19: the parachute's canopy): its model as a SkinModel of kind
    // a_kind, empty (logged) when it can't be read, and its bone count. a_keepWinding: San Andreas's own winding kept
    // (SkinBuild::Geometry::keepWinding: the canopy's back-to-back faces).
    std::vector<std::uint8_t> ReadSkinnedModel(RpClump* a_clump, const std::string& a_name, std::uint32_t a_kind, std::uint32_t& a_boneCount, bool a_keepWinding = false);

    // That clump's bones as its skeleton has them now, relative to a_feet (San Andreas metres) as CJ's are, into
    // a_out.bones; false when it has no skeleton or skin.
    bool PoseSkinned(RpClump* a_clump, std::uint32_t a_boneCount, const CVector& a_feet, Protocol::BodyState& a_out);
}
