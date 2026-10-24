// SKSE entry point. SKSE::Init sets up logging to
// Documents/My Games/Skyrim Special Edition/SKSE/AhShitRim.log, which it starts afresh each run: the last two runs' are
// kept beside it first, AhShitRim.1.log and AhShitRim.2.log, so a crash's log survives the restart after it.

#include "link/LinkService.h"
#include "audio/SaAudio.h"
#include "collision/CollisionSampler.h"
#include "debug/StreamTest.h"
#include "hud/SaHud.h"
#include "player/PlayerSync.h"
#include "player/SaveState.h"

namespace
{
    void KeepLastLogs()
    {
        const auto dir = SKSE::log::log_directory();
        if (!dir) {
            return;
        }
        std::error_code error;
        const auto      log = *dir / "AhShitRim.log";
        if (!std::filesystem::exists(log, error)) {
            return;
        }
        std::filesystem::rename(*dir / "AhShitRim.1.log", *dir / "AhShitRim.2.log", error);  // the older one goes, if any
        std::filesystem::copy_file(log, *dir / "AhShitRim.1.log", std::filesystem::copy_options::overwrite_existing, error);
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    KeepLastLogs();
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
