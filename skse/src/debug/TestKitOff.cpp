// Builds without the test kit (CI, scripts/package.ps1, a checkout without skse/extern/skse-test-kit): no-ops.
#include "debug/TestKit.h"

namespace AhShitRim::TestKit
{
    void Load() {}

    void OnMessage(SKSE::MessagingInterface::Message*) {}
}
