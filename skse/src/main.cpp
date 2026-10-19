// SKSE entry point. SKSE::Init sets up logging to
// Documents/My Games/Skyrim Special Edition/SKSE/AhShitRim.log.

#include "link/LinkService.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    SKSE::log::info(
        "AhShitRim loaded (plugin {}, Skyrim runtime {})",
        SKSE::GetPluginVersion().string("."),
        a_skse->RuntimeVersion().string("."));

    AhShitRim::LinkService::Start();

    return true;
}
