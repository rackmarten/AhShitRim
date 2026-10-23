#pragma once

#include <cstdint>

namespace AhShitRim::Protocol
{
    struct PlayerState;
}

// Skyrim's people react to CJ's guns and chainsaw as San Andreas's peds do (the rules in
// protocol/cpp/reactions.h). No plugin: vanilla behaviour, started from here.
//  - A civilian CJ aims a gun at (San Andreas's aim ray through its crosshair, line of sight from the player)
//    cowers: vanilla's cowering idle (IdleCoweringLoose), played through its AI process, let go 3 s after he
//    stops aiming, and may say one of Skyrim's lines about the player's magic. One sitting or busy that won't
//    take the idle flees instead.
//  - Each shot is noise: civilians within its weapon's hearing range (the silenced pistol's few metres, or
//    further if they face CJ and see him) flee from the player (Actor::InitiateFlee, a flee package outside
//    combat; let go with EvaluatePackage, or its AI reset if the flee outlives that), and the player makes a
//    detection event of the weapon's sound level, so everyone in earshot hears it as Skyrim's own sounds are
//    heard. A blast (Projectiles) is noise too.
//  - To Skyrim a gun is magic, and magic in town is no crime: the nearest guard who hears it says "Whoa,
//    whoa, whoa! Watch the magic!", and one CJ aims at "Go cast your fancy magic someplace else." (the
//    vanilla voice files, spoken with SpeakSound, shown as San Andreas's subtitles). Each shot is also a
//    spell cast from the player as far as Skyrim knows (TESSpellCastEvent of Flames, every half second of
//    a burst at most), for anything that watches the player's magic.
//  - The chainsaw running in CJ's hands makes civilians close by flee, further while it cuts.
// Followers, the commanded, anyone fighting or hostile, the foolhardy and creatures are left alone.
// Main thread only.
namespace AhShitRim::People
{
    void OnDataLoaded();  // looks up the idle, reads the ini

    // Every frame San Andreas has the player: a_pose is the frame's pose (nullptr if none), a_anchor the
    // handover's anchor, a_quiet while a scene or a menu holds CJ.
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_quiet);

    // A blast went off at a_at (Projectiles): heard out to Reactions::kBlastHeard.
    void Blast(const RE::NiPoint3& a_at);

    // A chainsaw blow (Shots): the saw cuts.
    void ChainsawCut();

    // Back to Skyrim's own player: everyone cowering stands up and everyone fleeing goes back to what it did.
    void Clear();
}
