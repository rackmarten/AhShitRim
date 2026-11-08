#pragma once

#include <cstdint>

#include "../ahshitrim_protocol.h"

// The Grove Street crew (protocol v39; docs/DESIGN.md, Grove Street crew). Skyrim owns every homie: an actor of a
// vanilla, non-unique base with its 3D hidden as the player's is, in the player's faction. San Andreas only plays him
// (sa/src/crew_puppets.cpp), and ModelView draws San Andreas's Families model at the actor's feet. This
//  - fills the spots of the holds where the player is Thane while CJ has the player: about 15 homies round the way into
//    the capital (the stable, the gate outside, past the gate inside; a town's way in where it has no walls), standing
//    about with San Andreas's idles, their AI off; a spot fills out of view and empties far away,
//  - G on foot recruits the homie CJ targets or the nearest in front of him (up to the crew size San Andreas's respect
//    table gives the Thane titles), H dismisses one, H with nobody near dismisses all, G with nobody near regroups,
//  - followers keep to San Andreas's formation behind the player (KeepOffsetFromActor) and never fight Skyrim's way:
//    their target is picked here (the nearest enemy of the player's in sight and in range), and they fire San Andreas's
//    guns at it in bursts; the bullets come back as shots with them as the shooter (Shots),
//  - CJ's own hits on them have no aggressor and no crime (Shots::Hurt), so they never turn on him,
//  - a load door or fast travel, F10 out and back, and a loaded save make the followers again behind CJ (the ASCR
//    co-save record keeps them; the actors of an earlier session left in a save are taken out once it has settled).
// Main thread only.
namespace AhShitRim::Homies
{
    void OnDataLoaded();  // reads [Crew] and [Debug] CrewThanes

    // Every frame San Andreas has the player, before ModelView::EndFrame: the spots, the crew, their fights, the input
    // for San Andreas and the drawing. a_pose is null while it isn't valid; a_onFoot: CJ walks (G and H work);
    // a_target: CJ's lock-on (0 for none).
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState* a_pose,
        bool a_onFoot, RE::FormID a_target, float a_delta);

    // Every frame, whoever has the player: takes out the homies that are to go, and after a load the ones a save kept.
    void Sweep(float a_delta);

    // G (a_recruit) or H pressed on foot.
    void OnKey(bool a_recruit);

    // Back to Skyrim's own player (a_loading: a load or a new game, whose world is about to go): every homie goes, the
    // followers are remembered and come back at the next handover.
    void OnRelease(bool a_loading);

    // A load door or fast travel moved the player: the followers come again behind CJ once he is there.
    void OnReanchor();

    // Whether a_actor is one of the homies; the actor of crew slot a_slot (ShotEvent.shooter less one), or null.
    bool        IsHomie(const RE::TESObjectREFR* a_ref);
    RE::Actor*  ActorOf(std::uint32_t a_slot);

    // How much of the damage CJ's shot would do a homie's does ([Crew] DamageFactor).
    float DamageFactor();

    // The co-save record (ASCR).
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();

    inline constexpr std::uint32_t kRecordType = 'ASCR';
    inline constexpr std::uint32_t kRecordVersion = 1;
}
