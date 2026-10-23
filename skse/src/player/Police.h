#pragma once

#include <cstddef>
#include <cstdint>

namespace AhShitRim::Protocol
{
    struct PlayerState;
}

// San Andreas's wanted level on top of Skyrim's crime system, and Busted (protocol v22; the rules in
// protocol/cpp/wanted.h). The bounty stays Skyrim's; the stars are the pursuit.
// - Stars (0 to 6) show in San Andreas's HUD while CJ has the player, from heat: each crime (any of the nine holds'
//   bounty on the player went up) adds some, by how bad it was. Out of every pursuer's sight for 5 s they flash,
//   then go one at a time; the bounty stays, and a guard of the hold who fights CJ later brings a star or two back.
//   No bounty (the fine paid, Wasted) is no stars.
// - The response: from 2 stars the hold's guards round about join the fight, from 3 mounted guards (a copy of
//   one of the hold's guards on a saddled horse, spawned behind CJ) chase him, from 5 the hold's soldiers
//   (levelled legionnaires or Stormcloaks, by who holds it when the war starts) join. When the stars go they stop
//   fighting and are taken out a step a frame once the player can't see them (Sweep).
// - AEZAKMI (San Andreas's toggle, CheatCodes) keeps the stars at 0, puts any bounty a crime adds back as it
//   was, and calls off the fight.
// - Busted in a vehicle: wanted, the car nearly stopped for 1.5 s, a guard of the hold at the driver's door:
//   San Andreas drags CJ out (kInputBusted, its jacked animation). A bike, bicycle or quad has no door: a guard
//   reaching him knocks him off. Moving keeps him safe.
// - On foot Skyrim's guard dialogue stays (the fine, resisting, persuading); only "go to jail" changes: the
//   player's GoToPrison (Actor's vfunc 0xB9) is hooked while CJ has the player, and Busted runs instead.
// - Busted: the arrest, "busted", the fade to black, the player taken to the hold's barracks (its crime
//   faction's exterior jail marker, `player.moveto`), the bounty paid from gold (all of it if short) and
//   cleared, the stars gone, everyone's fight called off, the fade back in. Weapons are kept; no jail.
// - The heat, its hold and how long CJ has been unseen are in the co-save, a record of their own
//   (Wanted::kRecordType).
// Main thread only.
namespace AhShitRim::Police
{
    void Install();  // the GoToPrison hook

    // Every frame while San Andreas has the player, after Vitals (a_pose null while it isn't valid). a_cjDrawn: CJ
    // is drawn at the player's place. a_scene: Skyrim moves the player. Returns whether Busted is running.
    bool Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_cjDrawn, bool a_scene,
        float a_delta);

    // Every frame, owned or not: one step of taking out the spawned pursuers that are leaving.
    void Sweep();

    // Busted is running and the player can't act.
    bool IsBusted();

    // CJ is being arrested: San Andreas gets kInputBusted.
    bool IsArrested();

    // Skyrim has the player back: Busted over, the spawned pursuers gone, the stars kept for CJ's return.
    void OnRelease();

    // The game moved the player and CJ was re-anchored there.
    void OnReanchor();

    // A game loads: the bounties are seeded again, nothing spawned is ours any more.
    void OnGameLoad();

    // The co-save record.
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
