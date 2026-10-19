// SKSE entry point. SKSE::Init sets up logging to
// Documents/My Games/Skyrim Special Edition/SKSE/AhShitRim.log.

#include "link/LinkService.h"
#include "player/PlayerSync.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    SKSE::log::info(
        "AhShitRim loaded (plugin {}, Skyrim runtime {})",
        SKSE::GetPluginVersion().string("."),
        a_skse->RuntimeVersion().string("."));

    AhShitRim::LinkService::Start();
    AhShitRim::PlayerSync::Install();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            AhShitRim::PlayerSync::OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            AhShitRim::PlayerSync::OnGameLoad();
            break;
        default:
            break;
        }
    });

    return true;
}
