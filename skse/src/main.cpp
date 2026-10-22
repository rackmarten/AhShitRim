// SKSE entry point. SKSE::Init sets up logging to
// Documents/My Games/Skyrim Special Edition/SKSE/AhShitRim.log.

#include "link/LinkService.h"
#include "audio/SaAudio.h"
#include "collision/CollisionSampler.h"
#include "debug/StreamTest.h"
#include "hud/SaHud.h"
#include "player/PlayerSync.h"
#include "player/SaveState.h"

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);

    SKSE::log::info(
        "AhShitRim loaded (plugin {}, Skyrim runtime {})",
        SKSE::GetPluginVersion().string("."),
        a_skse->RuntimeVersion().string("."));

    AhShitRim::LinkService::Start();
    AhShitRim::PlayerSync::Install();
    AhShitRim::SaveState::Install();

    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            AhShitRim::PlayerSync::OnDataLoaded();
            AhShitRim::CollisionSampler::OnDataLoaded();
            AhShitRim::SaHud::Load();
            AhShitRim::Audio::OnDataLoaded();
            AhShitRim::StreamTest::OnDataLoaded();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            AhShitRim::StreamTest::OnPostLoadGame();
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
