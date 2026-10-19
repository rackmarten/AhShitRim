#pragma once

// San Andreas as a hidden simulator of CJ, driven over the link.
//
// - Straight into a game: the splashes, movies and main menu are skipped, and a new game starts
//   without its mission script (main.scm never runs, so no intro, missions or pickups).
// - Empty world: no map collision or IPLs are streamed in, every building, dummy and object the
//   game loaded is taken out of the world, and there are no peds, traffic, parked cars, trains,
//   planes or wanted level. CJ is created by the ASI and stands on a flat, invisible test plane.
// - 30 frames a second (physics breaks above that), and nothing is rendered unless the window is
//   visible (-ahshitrim-visible). Sounds are muted.
// - While Skyrim owns nothing, CJ stands still; while it hands the player over (InputState), its
//   input drives CJ's pad, and every frame CJ's pose goes back as a PlayerState.
namespace AhShitRim::Runner
{
    void Install();  // at ASI load, before WinMain
}
