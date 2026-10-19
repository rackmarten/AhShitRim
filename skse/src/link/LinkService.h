#pragma once

#include "link.h"

// Owns the Skyrim end of the link and ticks it on its own thread (20 Hz), so the handshake and
// heartbeat also run in menus and while loading.
namespace AhShitRim::LinkService
{
    void  Start();  // call once at SKSE load
    Link& Get();
}
