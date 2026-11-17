// Getting into a vehicle as San Andreas does it. While its get-in animation plays, CJ's position stays at the door and
// the animation carries his body onto the seat. On the frame it ends, the task that seats him
// (CTaskSimpleCarSetPedInAsDriver or ...AsPassenger) moves his position onto the seat and adds the sitting (or riding)
// animation to blend in at once (CCarEnterExit::AddInCarAnim: a blend delta of 1000, from 0). That frame's animation
// update has already run, so his bones still hold the get-in's last keyframe, now offset from the seat instead of the
// door: San Andreas's own state that frame has his body past the seat by as far as the door was from it (a Sanchez:
// 0.84 m, a Sandking: about 1 m, measured 17 November 2026), and Skyrim drew the hop for a frame or two. The next
// frame's update has him sitting. So on the frame he's put in, the ASI finishes that instant blend before posing him.
// Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <span>

namespace AhShitRim::GetIn
{
    // San Andreas's instant blend (CCarEnterExit::AddInCarAnim, CPed::StopNonPartialAnims): a blend delta this big takes
    // an animation from nothing to all of it, or all of it to nothing, in one update.
    constexpr float kInstantBlend = 1000.0f;

    // One animation on CJ's clump: how much of it shows and how fast that changes, a second.
    struct Blend
    {
        float amount = 0.0f;
        float delta = 0.0f;
    };

    // Whether his animations get one more update before he's posed this frame: he was just put in a vehicle and an
    // instant blend in is still waiting at nothing.
    inline bool Pending(bool a_wasInVehicle, bool a_inVehicle, std::span<const Blend> a_blends)
    {
        if (a_wasInVehicle || !a_inVehicle) {
            return false;
        }
        for (const auto& blend : a_blends) {
            if (blend.delta >= kInstantBlend && blend.amount <= 0.0f) {
                return true;
            }
        }
        return false;
    }

    // That update's step, seconds: just long enough for every instant blend to finish (San Andreas changes a blend by
    // delta x step and holds it between nothing and all), so the animations themselves move on by a millisecond.
    constexpr float kStep = 1.0f / kInstantBlend;
}
