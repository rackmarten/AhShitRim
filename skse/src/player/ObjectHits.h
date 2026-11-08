#pragma once

#include <cstdint>

#include "traps.h"

// CJ's hits on Skyrim's objects (the rules in protocol/cpp/traps.h): a bullet or a blow that lands on an
// object, and his fire and blasts reaching the objects round them, are sent as Skyrim's own hit event from
// the player (what Papyrus OnHit listens to: the hanging oil lamps fall, the oil on the ground ignites from
// fire, and from a bullet landing in it, which is sent to it as fire), and a destructible object takes damage as from a weapon or a fire spell (the spider webs tear,
// freeing whoever is stuck in them). The collision round CJ is read again where something took damage.
// Main thread only.
namespace AhShitRim::ObjectHits
{
    // Whether a_ref (an object, not an actor) is destructible and not destroyed: its collision is kept round CJ.
    bool Destructible(const RE::TESObjectREFR* a_ref);

    // A bullet or a blow of a_weapon with a_saDamage San Andreas points landed on a_ref.
    void Struck(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, Traps::Hit a_hit, std::uint8_t a_weapon, float a_saDamage);

    // A bullet of a_weapon stopped at a_at (on anything, the land too): oil it landed in catches fire, GTA style.
    void BulletLanded(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, std::uint8_t a_weapon);

    // The flamethrower's cone from a_origin along a_dir (a unit vector) reaching a_range.
    void FireCone(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_origin, const RE::NiPoint3& a_dir, float a_range);

    // Fire or a blast round a_at, reaching objects whose bounds come within a_radius; a blast's Skyrim damage.
    void Around(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_at, float a_radius, Traps::Hit a_hit, float a_skyrimDamage = 0.0f);

    void Clear();
}
