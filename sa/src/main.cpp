// ASI entry point. The ASI loader loads AhShitRim.asi before the game's WinMain, and the static
// Plugin object below runs then. Logs to AhShitRim.log next to the .asi (the game's scripts folder).
//
// Only GTA: San Andreas 1.0 US is supported: one exe, one set of addresses. On any other exe the
// plugin logs why and does nothing else.
//
// Started with -ahshitrim-visible (scripts/launch-sa.ps1 -Visible), San Andreas shows in a small
// window instead of minimized and renders, to watch CJ while developing.

#include <plugin.h>

#include "log.h"
#include "runner.h"
#include "window.h"

#include <Windows.h>

#include <atomic>
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
}

namespace AhShitRim
{
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

    Link& GetLink()
    {
        static Link link(Protocol::Side::SanAndreas, [](std::string_view a_line) { Log(std::format("link: {}", a_line)); });
        return link;
    }
}

namespace
{
    using AhShitRim::GetLink;
    using AhShitRim::Log;

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

    // Logs where San Andreas crashed (exe offsets match gta-reversed's addresses) and the return
    // addresses into the exe found on the stack, once, then lets the crash go on as before.
    LONG WINAPI OnCrash(EXCEPTION_POINTERS* a_info)
    {
        static std::atomic<bool> logged{ false };
        if (logged.exchange(true)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        const auto* record = a_info->ExceptionRecord;
        // This ASI's own addresses are printed relative to its image base (see AhShitRim.map).
        HMODULE self = nullptr;
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OnCrash), &self);
        const auto selfBase = reinterpret_cast<std::uint32_t>(self);
        std::string stack;
        const auto* esp = reinterpret_cast<const std::uint32_t*>(a_info->ContextRecord->Esp);
        for (int i = 0, found = 0; i < 256 && found < 16; ++i) {
            const std::uint32_t value = esp[i];
            if (value > 0x401000 && value < 0x8A0000) {
                stack += std::format(" 0x{:06X}", value);
                ++found;
            } else if (value > selfBase && value < selfBase + 0x200000) {
                stack += std::format(" asi+0x{:X}", value - selfBase);
                ++found;
            }
        }
        MEMORY_BASIC_INFORMATION mbi{};
        ::VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi));
        Log(std::format("crash: exception 0x{:08X} at 0x{:08X} (access {} 0x{:08X}, page protect 0x{:X} state 0x{:X}), ecx 0x{:08X}; exe addresses on the stack:{}",
            record->ExceptionCode, reinterpret_cast<std::uintptr_t>(record->ExceptionAddress),
            record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0, record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0,
            mbi.Protect, mbi.State, a_info->ContextRecord->Ecx, stack));
        return EXCEPTION_CONTINUE_SEARCH;
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
            ::AddVectoredExceptionHandler(0, [](EXCEPTION_POINTERS* a_info) -> LONG {
                // Only access violations and the like, not the C++ and debugger exceptions games throw on purpose.
                const DWORD code = a_info->ExceptionRecord->ExceptionCode;
                return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ?
                           OnCrash(a_info) :
                           EXCEPTION_CONTINUE_SEARCH;
            });
            StartLink();
            const bool visible = std::wstring_view(::GetCommandLineW()).find(L"-ahshitrim-visible") != std::wstring_view::npos;
            AhShitRim::Window::Install(visible);
            AhShitRim::Runner::Install();
        }
    } g_plugin;
}
