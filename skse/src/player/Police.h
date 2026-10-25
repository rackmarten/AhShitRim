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
// - Skyrim drops a bounty when the witnesses die; the stars stay (San Andreas's don't go because CJ shot the cops
//   who saw him). The fine paid to a guard (PayFine, Actor's vfunc 0xBB, hooked), Busted and Wasted end them.
// - The response: from 2 stars the hold's guards round about join the fight, and a director keeps a force of ours
//   after him (Wanted::ForceFor): copies of the hold's guards, the hold's wolves from 3 (in its guards' factions, sped up
//   to keep CJ's pace on foot or in a car, Wanted::ChaseSpeedMult), a captain and a guard wizard from 4, the hold's
//   soldiers and a dragon from 5 (bringing it down is a lot of heat), giants and a mammoth at 6. One spawns per cooldown, never in
//   view: round CJ behind the camera on the ground, or out of the city's barracks door. Killing them all buys a
//   breather, over before a star would go for nobody seeing him. They're after CJ: one fighting someone else
//   is set on him again after a while of it, gently (Wanted::Leash, no fight stopped). When the stars go they stop
//   fighting and are taken out a step a frame once the player can't see them (Sweep).
// - Guards (any hold's) and ours hit CJ by the stars: 0.4 of their damage at 1 star up to all of it from 5
//   (the player's HandleHealthDamage, Actor's vfunc 0x104, hooked: the rest is given back as each hit lands).
// - AEZAKMI (CheatCodes) clears every hold's bounty and calls off the fight as it's typed, then keeps the stars
//   at 0, puts back any bounty a crime adds and stops any guard fighting the player.
// - Busted in a vehicle: wanted, the car nearly stopped for 1.5 s, a guard of the hold at the driver's door:
//   San Andreas drags CJ out (kInputBusted, its jacked animation). A bike, bicycle or quad has no door: a guard
//   reaching him knocks him off. Moving keeps him safe.
// - On foot Skyrim's guard dialogue stays (the fine, resisting, persuading); only "go to jail" changes: the
//   player's GoToPrison (Actor's vfunc 0xB9) is hooked while CJ has the player, and Busted runs instead.
// - Busted: the arrest, "busted", the fade to black, the player taken to the hold's barracks (its crime
//   the barracks' door's own teleport outside, the jail marker as the fallback), the bounty paid from gold (all of it
//   if short) and cleared, the stars gone, the fade back in. Weapons are kept; no jail. From the arrest on nobody
//   fights the player (and Vitals keeps its health), and CJ says one of his arrested lines.
// - CJ's lines (Wanted::kLine*, through PlayerSync's input): his chased line on a star gained and now and then while
//   pursuers see him.
// - The heat, its hold and how long CJ has been unseen are in the co-save, a record of their own
//   (Wanted::kRecordType).
// Main thread only.
namespace AhShitRim::Police
{
    void Install();  // the GoToPrison hook

    // Every frame while San Andreas has the player, after Vitals (a_pose null while it isn't valid). a_cjDrawn: CJ
    // is drawn at the player's place. a_scene: Skyrim moves the player. Returns whether Busted is running. The frame
    // itself (crimes, sight, the director, spawning, fights, removals) runs in Sweep, on the main thread.
    bool Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_anchor, const Protocol::PlayerState* a_pose, bool a_cjDrawn, bool a_scene,
        float a_delta);

    // Every frame, owned or not, at the end of the player's update on the main thread: the police's frame as Update
    // recorded it, and one step of taking out the spawned pursuers that are leaving.
    void Sweep(float a_delta);

    // Whether a guard's hit was eased (Wanted::GuardDamage) and left the player standing since the last call: a bleedout
    // the whole hit began is then stopped, not Wasted. Taken once.
    bool TakeSpared();

    // The stars' share of a_attacker's hits on CJ (Wanted::GuardDamage): a guard's (any hold's) or one of ours', else 1
    // (nobody, a trap, bandits, creatures).
    float ShareFor(RE::Actor* a_attacker);

    // A hit given back left the player with health: no Wasted for its bleedout (TakeSpared).
    void NoteSpared(RE::PlayerCharacter* a_player);

    // San Andreas's wanted cheats (CheatCodes): a_how 1 two more stars (OSRBLHH), 2 six (LJSPQK), 0 none (ASNAEB). The
    // pursuit is the last hold's, else the one the player stands in, else Whiterun's; the bounty is left alone. Not
    // while AEZAKMI is on.
    void CheatStars(int a_how);

    // Where the last hit on the player came from (anyone's, through the same hook), if it landed within a_seconds.
    bool LastBlow(float a_seconds, RE::NiPoint3& a_from);

    // The player was knocked down: who is fighting it now (Skyrim's and ours) is noted.
    void OnKnockedDown(RE::PlayerCharacter* a_player);

    // The player is back where CJ is after a knock-down: for a while (Knock::Rejoin) anyone noted who dropped it is set on
    // it again. Nobody's fight is stopped.
    void Retarget(RE::PlayerCharacter* a_player);

    // A line for CJ (Wanted::kLineChased, kLineArrested) to put in the input, 0 for none; taken once.
    std::uint8_t TakeLine();

    // Whether a quick save or quickload may go ahead (Wanted::QuickAllowed): no stars, none of ours out. When it may not,
    // ours that are only leaving are taken out at once, so it can be done again in a moment.
    bool QuickAllowed(bool a_pressed);

    // Busted is running and the player can't act.
    bool IsBusted();

    // CJ is being arrested: San Andreas gets kInputBusted.
    bool IsArrested();

    // Skyrim has the player back: Busted over, the spawned pursuers gone, the stars kept for CJ's return.
    void OnRelease();

    // The game moved the player and CJ was re-anchored there.
    void OnReanchor();

    // A game loads or a new one starts (kPreLoadGame, kNewGame): nothing is done to any actor, nothing spawned is ours any
    // more, and the bounties are seeded again.
    void OnGameLoad();

    // A game has loaded (kPostLoadGame): once it has settled, copies left in the save by earlier builds
    // (Wanted::LeftoverCopy) are taken out.
    void OnPostLoadGame();

    // The co-save record.
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size);
    void Revert();
}
