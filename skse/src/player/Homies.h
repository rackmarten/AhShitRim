#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../ahshitrim_protocol.h"

// The Grove Street crew (protocol v39; docs/DESIGN.md, Grove Street crew). Skyrim owns every homie: an actor of a
// vanilla, non-unique base with its 3D hidden as the player's is, in the player's faction. San Andreas only plays him
// (sa/src/crew_puppets.cpp), and ModelView draws San Andreas's Families model at the actor's feet. This
//  - fills the spots of the holds where the player is Thane while CJ has the player: about 15 homies round the way into
//    the capital (the stable, the gate outside, past the gate inside; a town's way in where it has no walls), standing
//    about with San Andreas's idles, their AI off; a spot fills out of view and empties far away,
//  - G on foot while CJ aims recruits the homie he aims at (up to the crew size San Andreas's respect table gives the
//    Thane titles), or dismisses him if he's of the crew; G without aiming does nothing,
//  - followers run Skyrim's own follower package (Lydia's, pathing after the player), their Skyrim gear taken off, and
//    never fight Skyrim's way:
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
    // for San Andreas and the drawing. a_pose is null while it isn't valid; a_onFoot: CJ walks (G works);
    // a_target: CJ's lock-on (0 for none).
    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, std::uint32_t a_handover, const Protocol::PlayerState* a_pose,
        bool a_onFoot, RE::FormID a_target, float a_delta);

    // Every frame, whoever has the player: takes out the homies that are to go, and after a load the ones a save kept.
    void Sweep(float a_delta);

    // G pressed on foot, a_aiming: the aim key held. It acts at the next Update, only while CJ aims (San Andreas's aim
    // counts too), so a G typed in a cheat does nothing.
    void OnKey(bool a_aiming);

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

    // The hit test's crew scenario (scripts/hit-test.ps1 -Scenario crew): the Thane count it plays with, a_count homies
    // recruited (the nearest, wherever CJ faces), how many are about (a_followers: following), and one follower.
    void       DebugThanes(int a_count);
    int        DebugRecruit(RE::PlayerCharacter* a_player, int a_count);
    int        DebugCount(bool a_followers);
    RE::Actor* DebugFollower();
    // The ridecrew scenario: every follower.
    std::vector<RE::Actor*> DebugFollowers();
    int        DebugRegrouped();  // followers put back near CJ so far
    bool       DebugTargets(const RE::Actor* a_actor);  // whether a follower has a_actor as his target
    unsigned   DebugHushed();        // their Skyrim subtitles taken off so far (a line each)
    unsigned   DebugHushedSounds();  // their Skyrim voice handles stopped so far

    // The test kit's crew commands (skse/src/debug/TestKit.cpp): G as if pressed (a_aiming: CJ aims, along a_heading, his
    // lock-on on a_target or 0), the a_index-th nearest homie standing about, what a homie carries and runs, and the
    // crew's counters (a_reset: from zero again after).
    void       DebugKey(RE::PlayerCharacter* a_player, bool a_aiming, float a_heading, RE::FormID a_target);
    RE::Actor* DebugFree(RE::PlayerCharacter* a_player, int a_index);
    struct DebugProbe
    {
        std::vector<std::string> gear;    // Skyrim weapons, ammo, shields and torches in his inventory
        std::vector<std::string> hands;   // what his hands hold, Skyrim's
        bool                     weaponDrawn = false, followPackage = false, canTalk = true, rides = false, follower = false;
        RE::FormID               target = 0;  // his crew target
        float                    waiting = 0.0f;  // his WaitingForPlayer actor value
        float                    greetingTimer = 0.0f, detectGreetTimer = 0.0f, checkToTalkTimer = 0.0f, voiceTimer = 0.0f;  // HighProcessData's
        bool                     greeting = false, greetingPlayer = false;  // a greeting under way (greetTopic), to the player
    };
    DebugProbe DebugProbeOf(RE::Actor* a_actor);
    struct DebugStats
    {
        unsigned                hushedLines = 0, hushedSounds = 0, followPuts = 0, gearStripped = 0;
        unsigned                drawnFrames = 0;   // frames a homie had a Skyrim weapon in a hand
        unsigned                fistsFrames = 0;   // frames one had his empty hands raised for a Skyrim fight
        unsigned                unwaited = 0;      // followers told to stop waiting
        std::vector<RE::FormID> targeted;  // everyone a follower has had as his target
    };
    DebugStats DebugStatsOf(bool a_reset);
    void       DebugDismissAll();  // every follower sent off, none to come back
    void       DebugFresh();       // the spots fill in view for 3 s, as after a handover (a test can't wait for the camera to turn)
    struct DebugFoeInfo
    {
        bool       hostile = false, fightsUs = false, fleeing = false, animal = false, aggressive = false, foe = false;
        RE::FormID combatTarget = 0;
        float      metres = 0.0f;  // from the player
        struct Follower
        {
            RE::FormID id = 0;
            float      metres = 0.0f;
            bool       sight = false, mayShoot = false;
            int        weapon = 0;
        };
        std::vector<Follower> followers;
    };
    DebugFoeInfo DebugFoe(RE::PlayerCharacter* a_player, RE::Actor* a_actor);  // why the crew shoots a_actor or not
    float      DebugStray(RE::PlayerCharacter* a_player, float a_metres);  // a follower put on the navmesh that far behind; metres, -1 for none
    bool       DebugNavmesh(RE::PlayerCharacter* a_player, RE::NiPoint3& a_nearest);  // the navmesh nearest the player

    inline constexpr std::uint32_t kRecordType = 'ASCR';
    inline constexpr std::uint32_t kRecordVersion = 1;
}
