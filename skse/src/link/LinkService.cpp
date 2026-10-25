#include "link/LinkService.h"

namespace AhShitRim::LinkService
{
    Link& Get()
    {
        static Link link(Protocol::Side::Skyrim, [](std::string_view a_line) { SKSE::log::info("link: {}", a_line); });
        return link;
    }

    void Start()
    {
        // Never joined. On exit Windows ends this thread before static destructors run, so the
        // Link destructor then detaches cleanly and the San Andreas side logs it.
        std::thread([] {
            const auto start = std::chrono::steady_clock::now();
            for (;;) {
                const auto now = std::chrono::steady_clock::now() - start;
                Get().Tick(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
                std::this_thread::sleep_for(50ms);
            }
        }).detach();
        SKSE::log::info("link: started (protocol v{})", Protocol::kProtocolVersion);
    }

    void ShowMismatch(bool a_now)
    {
        static Handshake::Notice notice{ Handshake::kMessageRepeatMs };
        const std::uint32_t      other = Get().MismatchVersion();
        const auto               nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool               due = notice.Due(other, static_cast<std::uint64_t>(nowMs));
        if (other && (due || a_now)) {
            RE::DebugMessageBox(Handshake::MismatchMessage(Protocol::Side::Skyrim, other).c_str());
        }
    }
}
