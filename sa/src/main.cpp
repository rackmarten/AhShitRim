// ASI entry point. The ASI loader loads AhShitRim.asi before the game's WinMain, and the static
// Plugin object below runs then. Logs to AhShitRim.log next to the .asi (the game's scripts folder).
//
// Only GTA: San Andreas 1.0 US is supported: one exe, one set of addresses. On any other exe the
// plugin logs why and does nothing else.

#include <plugin.h>

#include "link.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string_view>
#include <thread>

namespace
{
    std::mutex    g_logLock;
    std::ofstream g_log;

    void Log(std::string_view a_line)
    {
        SYSTEMTIME t;
        ::GetLocalTime(&t);
        std::scoped_lock lock(g_logLock);
        if (!g_log.is_open()) {
            g_log.open(std::filesystem::path(plugin::paths::GetPluginDirPathW()) / L"AhShitRim.log", std::ios::trunc);
        }
        g_log << std::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] {}\n",
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, a_line)
              << std::flush;
    }

    AhShitRim::Link& GetLink()
    {
        static AhShitRim::Link link(AhShitRim::Protocol::Side::SanAndreas, [](std::string_view a_line) { Log(std::format("link: {}", a_line)); });
        return link;
    }

    // Ticks the link on its own thread (20 Hz), so the handshake and heartbeat also run in menus
    // and while loading. Never joined: on exit Windows ends this thread before static destructors
    // run, so the Link destructor then detaches cleanly and the Skyrim side logs it.
    void StartLink()
    {
        std::thread([] {
            const auto start = std::chrono::steady_clock::now();
            for (;;) {
                const auto now = std::chrono::steady_clock::now() - start;
                GetLink().Tick(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }).detach();
        Log(std::format("link: started (protocol v{})", AhShitRim::Protocol::kProtocolVersion));
    }

    class Plugin
    {
    public:
        Plugin()
        {
            Log(std::format("AhShitRim loaded (plugin {}, game {})", AHSHITRIM_VERSION, plugin::GetGameVersionName()));
            if (!plugin::IsSupportedGameVersion()) {
                Log(std::format(
                    "this game is {}, but AhShitRim only supports GTA: San Andreas 1.0 US. "
                    "Downgrade it (docs/DEV_SETUP.md) and start gta_sa.exe. Not starting.",
                    plugin::GetGameVersionName()));
                return;
            }
            StartLink();
        }
    } g_plugin;
}
