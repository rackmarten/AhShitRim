#pragma once

// CJ's rockets, grenades, tear gas and Molotovs fly in Skyrim (protocol v15). San Andreas's world has no one
// in it to hit, so when CProjectileInfo::AddProjectile makes one of his, this module reads it as San Andreas
// made it (where, how fast, which way it faces, its fuse), sends it to Skyrim as a Projectile message and
// takes it out of San Andreas's world again without a blast. One San Andreas couldn't throw (no room,
// CProjectileInfo::RemoveNotAdd, which would blow it up at his feet) goes as a burst where it is. Each
// kind's model goes to Skyrim once, as the weapon in his hand does. The satchel charge stays San Andreas's
// (#863). A rocket keeps a stand-in in San Andreas, without collision, flown along the same line, so its
// sound follows it until Skyrim says it went off. Skyrim's Effect messages bring the rest back: San Andreas's
// explosion sound where one went off, its own fire where a Molotov's burns (its crackle, and CJ catches fire
// walking into it) and CJ set alight when he is caught in one. Game thread only.

#include <cstdint>

class CPlayerPed;

namespace AhShitRim::CjProjectiles
{
    // Once the other plugins (SilentPatch) have patched the game.
    void Install();

    // The ring brought an Effect message: kept until the next Update.
    void Queue(const std::uint8_t* a_data, std::uint32_t a_size);

    // Once a frame after CjWeapons::Update: the effects Skyrim sent, and the rockets' stand-ins moved on.
    void Update(CPlayerPed* a_ped);
}
