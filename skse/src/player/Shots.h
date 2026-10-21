#pragma once

#include <cstdint>

// What CJ's weapons hit (protocol v13). San Andreas sends each bullet, shotgun pellet and melee blow as a
// Shot; this
//  - ray tests each bullet through Skyrim's Havok world from where San Andreas fired it: statics and the land
//    stop it, the first living actor takes it, the bone it hit gives the body part (the head is a headshot),
//  - finds the actors a blow reaches around where it lands, once per blow (the chainsaw over and over),
//  - hurts them through Skyrim's own hit (HitData, the player as the aggressor: kill credit, crime, aggro)
//    by Damage::ToSkyrim, never scaled by the difficulty,
//  - plays Skyrim's impacts: the race's blood on actors, the surface's effect and sound on statics,
//  - counts the hits per weapon and sends them back as Hit commands, which raise San Andreas's skills.
// Main thread only.
namespace AhShitRim::Shots
{
    void OnDataLoaded();  // looks up the impact sets, reads the ini

    // The ring brought a Shot message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Every frame San Andreas has the player: the shots of a_handover, relative to a_anchor.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover);

    // Back to Skyrim's own player: shots still waiting are dropped.
    void Clear();
}
