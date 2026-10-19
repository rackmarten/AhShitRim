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
}
