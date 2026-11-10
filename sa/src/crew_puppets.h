#pragma once

#include <cstdint>

#include "../../protocol/cpp/crew.h"

class CPlayerPed;
class CVector;

// The Grove Street crew's puppets (protocol v39). Skyrim owns each homie: CrewInput says where he is, which way he
// faces, what he aims at and whether he fires. San Andreas plays him: a Families ped of its own, made as a mission
// ped with no decisions, collision or gravity (nothing of San Andreas's moves or hurts him), put every frame where
// Skyrim says, walking or running by how fast he moves (San Andreas's walk and run blended straight onto him, no task),
// facing the way he goes (Crew::Facing), his right arm on his aim, his gun on his hand. His shots are
// San Andreas's own (CWeapon::Fire: its sound and rate), and CjShots sends their bullets to Skyrim with him as the
// shooter instead of testing them here. His skeleton and his gun go back to Skyrim in crew slot i; his model and his
// gun's go over the ring once, Skyrim copies them for every homie. Game thread only.
namespace AhShitRim::CrewPuppets
{
    // Once a frame after CjBody::Update (a_spawn and a_lift as for it). Everything goes while a_owned is false.
    void Update(CPlayerPed* a_cj, const CVector& a_spawn, float a_lift, unsigned a_handover, unsigned a_frame, bool a_owned);

    // Whether a_entity is one of the puppets, and its crew slot (-1 for none).
    bool IsPuppet(const void* a_entity);
    int  SlotOf(const void* a_entity);

    // Right before CjBody's UpdateAnim hook updates puppet a_ped's animations by a_step seconds: if none of them would
    // outlive that update (Crew::LeavesANode), his idle goes on first, since San Andreas's update crashes on a clump
    // left with no node.
    void KeepAnimated(void* a_ped, float a_step);

    // How far off his aim a homie's shot a_seed goes, radians (San Andreas's gang peds are poor shots).
    float SpreadRadians(std::uint32_t a_seed);

    void Clear();
}
