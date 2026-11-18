#pragma once

// Agent-driven test sessions through skse-test-kit (skse/extern/skse-test-kit, docs/DEV_SETUP.md "Test sessions").
// Only dev builds link the kit (CMake option AHSHITRIM_TEST_KIT, which scripts/build.ps1 turns on when the submodule is
// there and scripts/package.ps1 leaves off); without it both calls are no-ops. With the kit, AhShitRim.ini's [Test]
// section (which scripts/skytest.ps1 writes for a session and takes out again) starts Skyrim hidden, never focused,
// straight into a test state; no [Test], no change at all. [Test] CJ=1 also hands the player to San Andreas once
// there, and the session counts as ready only when CJ has it.
namespace AhShitRim::TestKit
{
    void Load();  // first thing after SKSE::Init, before the game creates its window
    void OnMessage(SKSE::MessagingInterface::Message* a_message);
}
