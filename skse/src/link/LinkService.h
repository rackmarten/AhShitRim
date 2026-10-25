#pragma once

#include "link.h"

// Owns the Skyrim end of the link and ticks it on its own thread (20 Hz), so the handshake and
// heartbeat also run in menus and while loading.
namespace AhShitRim::LinkService
{
    void  Start();  // call once at SKSE load
    Link& Get();
    // Tells the player in a message box when San Andreas speaks another version: once the mismatch starts, then
    // every few minutes while it lasts, and right away when a_now (the hand-over key pressed). Main thread only.
    void ShowMismatch(bool a_now = false);
}
