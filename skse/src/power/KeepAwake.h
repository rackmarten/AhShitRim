#pragma once

// Keeps the display on while San Andreas has the player. Nobody touches the keyboard or mouse while
// CJ is idle on screen, so Windows turns the display off after its timeout, and with it any audio
// endpoint that lives in the monitor (HDMI or DisplayPort speakers). Neither Skyrim's XAudio2 nor
// San Andreas's DirectSound reconnects to a device that went away, so both games went silent for
// good. A power request (it shows in `powercfg /requests`) holds the display on; Windows drops it
// when Skyrim exits.
namespace AhShitRim::KeepAwake
{
    // Holds the display on, or lets it go. Repeating the current state does nothing. Returns false if
    // Windows refused the request.
    bool Display(bool a_hold);
}
