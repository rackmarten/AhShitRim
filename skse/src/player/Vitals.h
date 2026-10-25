#pragma once

#include "health.h"

namespace AhShitRim::Protocol
{
    struct PlayerState;
}

// CJ's health, armour and Wasted while San Andreas has the player (protocol v11, Wasted as San
// Andreas plays it since v14; the rules in protocol/cpp/health.h and wasted.h).
// - Skyrim's health is the only health. The damage San Andreas works out for CJ (a fall, a crash, an
//   explosion) comes in the pose as a running total and is taken off Skyrim's player, CJ's armour
//   (ours, the bar in San Andreas's HUD) first.
// - The player is made essential for the handover, so at 0 health it bleeds out instead of dying, and
//   that is Wasted: CJ falls dead on his front, San Andreas's death camera rises over him, "wasted",
//   a fade to black with the text, then the player is taken to the temple of the hold it fell in (`coc` to the temple's cell), wakes there at full
//   health, 100 gold lighter (all of it if short), and the screen fades back in. Weapons are kept.
// - Not while Unbound (the Helgen intro) runs: the player isn't essential then, dies, and Skyrim
//   reloads the last save as it does.
// Main thread only.
namespace AhShitRim::Vitals
{
    // San Andreas took the player (or a loaded save's handover resumed): the essential flag on.
    void OnTakeOver(RE::PlayerCharacter* a_player);

    // Skyrim has the player back: the flag off again, a Wasted in progress over.
    void OnRelease(RE::PlayerCharacter* a_player);

    // The game moved the player and CJ was re-anchored there.
    void OnReanchor();

    // Every frame while San Andreas has the player, after its pose was read (a_pose is null while it
    // isn't valid). a_cjDrawn: CJ is drawn at the player's place. Returns whether Wasted is running.
    bool Update(RE::PlayerCharacter* a_player, const Protocol::PlayerState* a_pose, bool a_cjDrawn);

    // Wasted is running: the player can't act.
    bool IsWasted();

    // CJ caught in his own blast, fire or gas (v15, Projectiles): a_damage San Andreas health points off his
    // armour first, then Skyrim's health, as San Andreas's own damage goes. Not while Wasted runs.
    void Hurt(RE::PlayerCharacter* a_player, float a_damage);

    // Hooks Skyrim's magic damage (the value modifier effects), so it goes through CJ's armour first too.
    void Install();

    // A hit that just took a_reported (either sign: HandleHealthDamage's is negative) off the player's health (Police's
    // HandleHealthDamage hook): it becomes a_factor of it
    // ([Balance] DamageTaken, the stars' share for guards), CJ's armour taking it first as San Andreas's does (every kind
    // but drowning, falls, poison and disease, Health::ArmourTakes), the more taken or the rest given back. Returns the
    // health given back (negative: taken on top). Nothing while Wasted runs or BAGUVIX is on.
    float TakeHit(RE::PlayerCharacter* a_player, float a_reported, float a_factor, Health::Kind a_kind);

    // Our own damage (San Andreas's, a hit's difference, a magic effect's) is being taken off the player: not a new hit.
    bool OwnDamage();

    // CJ falls and lies dead (San Andreas gets kInputWasted), and once he lies still, under San
    // Andreas's death camera (kInputWastedCamera).
    bool IsDown();
    bool HasDeathCamera();

    // Busted (v22, Police): from the arrest on the player takes no damage either, so the guards can't turn it into Wasted.

    // BAGUVIX (v20, CheatCodes): while on, nothing San Andreas or Skyrim does takes the player's health or CJ's armour, the
    // health bar stays full, and being hit doesn't train Block, Light Armor or Heavy Armor.
    void SetInvincible(bool a_on);
    bool IsInvincible();

    // CJ's armour in San Andreas's points (0..100), part of the save.
    float Armor();
    void  SetArmor(float a_armor);
}
