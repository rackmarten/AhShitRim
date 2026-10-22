#pragma once

class CPlayerPed;

// CJ's health while Skyrim has the player (protocol v11, the rules in protocol/cpp/health.h).
// Skyrim's health is the only health: San Andreas keeps CJ at a fixed full health plus a buffer, so
// nothing it does can kill him, works out the damage it would do (a fall, a crash, an explosion) as
// what each frame took off, and reports the running total in the pose for Skyrim to take off its
// player. His armour is Skyrim's side too (it absorbs the damage first), so San Andreas's own stays 0.
// While the player is dying (Wasted) CJ lies dead under San Andreas's death camera and damage doesn't count.
// What San Andreas kills outright anyway (a vehicle blowing up with him in it) is hidden from its game
// logic, so its own Wasted never runs: dead in his seat he stays there for Skyrim's Wasted and is
// revived when it is over, anywhere else at once. Game thread only.
namespace AhShitRim::Vitals
{
    // Hooks CGameLogic::Update, where San Andreas notices its player died and starts its own Wasted.
    void Install();

    // Right before a frame of San Andreas runs: CJ's health at its fixed value. a_counting says
    // whether this frame's damage is Skyrim's business.
    void Begin(CPlayerPed* a_ped, bool a_counting);

    // Right after the frame: what it took off CJ goes into the total.
    void End(CPlayerPed* a_ped);

    // Every input: Wasted's state. a_down (kInputWasted): CJ falls dead on his front and lies there;
    // a_camera (kInputWastedCamera): the death camera rises over him. Off, he is up again and the
    // camera his.
    void SetWasted(CPlayerPed* a_ped, bool a_down, bool a_camera);

    // Damage so far, San Andreas health points.
    float Damage();
}
