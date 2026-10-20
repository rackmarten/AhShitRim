# sa

32-bit ASI plugin (C++, [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)) that runs inside GTA: San Andreas 1.0 US. It empties San Andreas's map, builds Skyrim's collision there and runs CJ (later his car) on it; see [docs/DESIGN.md](../docs/DESIGN.md).

It runs San Andreas hidden as a simulator of CJ (see "Running San Andreas hidden" in the design): windowed and minimized, straight into an empty world with CJ on Skyrim's ground, built around him from the collision chunks Skyrim streams, at 30 frames a second, driven by the input Skyrim sends and streaming CJ's pose, skeleton and model back. It logs to `AhShitRim.log` next to the `.asi` (the game's `scripts` folder): `AhShitRim loaded` with the detected game version, then lines prefixed `link:`, `window:`, `runner:`, `ground:`, `body:` and, if San Andreas crashes, `crash:` with the address and the exe addresses on the stack.

- `src/main.cpp` entry point, log, link thread, crash logging
- `src/window.cpp` windowed and minimized, never paused for losing the focus
- `src/runner.cpp` straight into a game, empty world, CJ, frame cap, input and pose
- `src/chunk_world.cpp` Skyrim's collision chunks as collision objects around CJ, the floating origin, holding CJ until the ground is in, fall rescue
- `src/cj_body.cpp` CJ's model (geometry, skin, textures) sent to Skyrim, his animations run in full while hidden and his bones posed every frame, with San Andreas's camera

Only 1.0 US is supported (one exe, one set of addresses). On any other exe the plugin logs why and does nothing else.

- `src/` plugin source
- `extern/plugin-sdk` git submodule ([DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)). `CMakeLists.txt` builds its `plugin_sa` library directly from the sources, mirroring the SDK's own premake project, so no `PLUGIN_SDK_DIR` or premake step is needed.
- `CMakePresets.json` `release` and `debug` presets (Visual Studio 2022 generator, Win32)

Build with `scripts/build.ps1 -Target sa`. Setup is in [docs/DEV_SETUP.md](../docs/DEV_SETUP.md).
