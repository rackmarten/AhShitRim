// Built only with AHSHITRIM_TEST_KIT (dev builds); TestKitOff.cpp stands in otherwise.
#include "debug/TestKit.h"

#include "Ini.h"
#include "link/LinkService.h"
#include "player/PlayerSync.h"

#include <skse_test_kit/Kit.h>

namespace AhShitRim::TestKit
{
    namespace
    {
        bool g_cj = false;
        int  g_wait = 0;  // frames until the next try at the handover

        void Arrived(const SkseTestKit::Settings& a_settings)
        {
            const auto cj = a_settings.values.find("CJ");
            g_cj = cj != a_settings.values.end() && cj->second == "1";
        }

        // [Test] CJ=1: San Andreas takes the player as F10 hands it over, once the link is up (San Andreas may still be
        // starting), tried again every 2 s until it has it.
        bool CjReady()
        {
            if (!g_cj || PlayerSync::DebugOwned()) {
                return true;
            }
            if (g_wait > 0) {
                --g_wait;
                return false;
            }
            g_wait = 120;
            if (LinkService::Get().IsUp()) {
                SKSE::log::info("test kit: handing the player to San Andreas ([Test] CJ=1)");
                PlayerSync::DebugToggle();
            }
            return PlayerSync::DebugOwned();
        }
    }

    void Load()
    {
        SkseTestKit::Load({ .name = "AhShitRim", .ini = Ini::kPath, .hooks = { .arrived = &Arrived, .ready = &CjReady } });
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_message)
    {
        SkseTestKit::OnMessage(a_message);
    }
}
