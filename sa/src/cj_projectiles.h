#pragma once

// CJ's rockets, grenades, tear gas and Molotovs fly in Skyrim (protocol v15). San Andreas's world has no one
// in it to hit, so when CProjectileInfo::AddProjectile makes one of his, this module reads it as San Andreas
// made it (where, how fast, which way it faces, its fuse), sends it to Skyrim as a Projectile message and
// takes it out of San Andreas's world again without a blast. One San Andreas couldn't throw (no room,
// CProjectileInfo::RemoveNotAdd, which would blow it up at his feet) goes as a burst where it is. Each
// kind's model goes to Skyrim once, as the weapon in his hand does. The satchel charge stays San Andreas's
// (#863). Game thread only.
namespace AhShitRim::CjProjectiles
{
    // Once the other plugins (SilentPatch) have patched the game.
    void Install();
}
