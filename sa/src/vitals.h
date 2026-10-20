#pragma once

class CPlayerPed;

// CJ's health while Skyrim has the player (protocol v11, the rules in protocol/cpp/health.h).
// Skyrim's health is the only health: San Andreas keeps CJ at a fixed full health plus a buffer, so
// nothing it does can kill him, works out the damage it would do (a fall, a crash, an explosion) as
// what each frame took off, and reports the running total in the pose for Skyrim to take off its
// player. His armour is Skyrim's side too (it absorbs the damage first), so San Andreas's own stays 0.
// While the player is dying (Wasted) the game runs in slow motion and damage doesn't count.
// Game thread only.
namespace AhShitRim::Vitals
{
    // Right before a frame of San Andreas runs: CJ's health at its fixed value. a_counting says
    // whether this frame's damage is Skyrim's business.
    void Begin(CPlayerPed* a_ped, bool a_counting);

    // Right after the frame: what it took off CJ goes into the total.
    void End(CPlayerPed* a_ped);

    // Wasted: slow motion on or off (kInputWasted).
    void SetWasted(bool a_wasted);

    // Damage so far, San Andreas health points.
    float Damage();
}
