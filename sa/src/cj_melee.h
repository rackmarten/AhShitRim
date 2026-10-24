#pragma once

class CPlayerPed;
class CVector;

// CJ on foot against Skyrim's actors (the rules in protocol/cpp/melee.h). San Andreas's world has no one in it,
// so on foot Skyrim sends the actors around CJ as capsules too (the proxy slot, as for his vehicle), and every
// frame after San Andreas's physics step:
// - CJ is pushed out of each he walked into, and what of his run went into one is taken away, so he stops
//   against a guard, a wolf or a giant instead of walking through,
// - while he holds the right mouse button with fists or a melee weapon, or swings one at someone hostile in
//   front, Skyrim flags the one he locks on to (kProxyTarget), and from the next frame his on-foot task
//   (CTaskSimplePlayerOnFoot::ProcessPed) turns him to face it as he stands and fights, before his blows are
//   struck. Running, he goes where he is steered.
// Game thread only.
namespace AhShitRim::CjMelee
{
    // Once the other plugins (SilentPatch) have patched the game.
    void Install();

    // Once a frame after the simulation, before the pose is published; a_active while CJ has the player and nothing
    // else holds him (Wasted, a scene). a_spawn and a_lift as for Vehicles::Update.
    void Update(CPlayerPed* a_ped, const CVector& a_spawn, float a_lift, unsigned a_handover, bool a_active);
}
