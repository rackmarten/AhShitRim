#pragma once

// The point lights the plugin adds to Skyrim's renderer itself (the muzzle flash's, the vehicles' lamps'): a light
// lights nothing until it's among the shadow scene node's lights, and Skyrim drops every light from those when the
// player goes through a load door into an interior (and again on the way out), so a light added once, when it was made,
// stays dark after that. Keep adds it back whenever it's missing.
namespace AhShitRim::SceneLights
{
    // Makes sure a_light is among the renderer's lights (every kind of surface lit, no shadow), adding it when it isn't:
    // the first time, or after Skyrim dropped it. Call it each frame the light is used. True when it's there; false
    // while the renderer isn't up (tried again on the next call).
    bool Keep(RE::NiPointLight* a_light);
}
