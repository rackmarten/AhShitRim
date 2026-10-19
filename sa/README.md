# sa

32-bit ASI plugin (C++, [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)) that runs inside GTA: San Andreas 1.0 US. Later increments empty San Andreas's map, build Skyrim's collision there and run CJ and his car on it; see [docs/DESIGN.md](../docs/DESIGN.md).

It runs San Andreas hidden as a simulator of CJ (see "Running San Andreas hidden" in the design): windowed and minimized, straight into an empty world with CJ on a flat test plane, at 30 frames a second, driven by the input Skyrim sends and streaming CJ's pose back. It logs to `AhShitRim.log` next to the `.asi` (the game's `scripts` folder): `AhShitRim loaded` with the detected game version, then lines prefixed `link:`, `window:`, `runner:` and, if San Andreas crashes, `crash:` with the address and the exe addresses on the stack.

- `src/main.cpp` entry point, log, link thread, crash logging
- `src/window.cpp` windowed and minimized, never paused for losing the focus
- `src/runner.cpp` straight into a game, empty world, test plane, CJ, frame cap, input and pose

Only 1.0 US is supported (one exe, one set of addresses). On any other exe the plugin logs why and does nothing else.

- `src/` plugin source
- `extern/plugin-sdk` git submodule ([DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)). `CMakeLists.txt` builds its `plugin_sa` library directly from the sources, mirroring the SDK's own premake project, so no `PLUGIN_SDK_DIR` or premake step is needed.
- `CMakePresets.json` `release` and `debug` presets (Visual Studio 2022 generator, Win32)

Build with `scripts/build.ps1 -Target sa`. Setup is in [docs/DEV_SETUP.md](../docs/DEV_SETUP.md).
