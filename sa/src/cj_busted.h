#pragma once

class CPlayerPed;

// Busted (protocol v22): while Skyrim's kInputBusted is on, CJ is arrested as San Andreas's cops arrest the player.
// In a car (or a monster truck) the driver's door opens and he is dragged out with San Andreas's jacked animation
// (CTaskComplexCarSlowBeDraggedOut through its driver's door, no jacker: Skyrim's guard stands there), then puts his
// hands up. On a bike, bicycle or quad he is knocked off it to the left and lies there. On foot he puts his hands up
// (ped.ifp's handsup, held). In a boat or an aircraft he stays put. Off again, he stands up, the arrest's animations
// gone. Game thread only.
namespace AhShitRim::CjBusted
{
    // Every input: a_busted (kInputBusted, with San Andreas owning the player).
    void Set(CPlayerPed* a_ped, bool a_busted);
}
