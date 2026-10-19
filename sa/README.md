# sa

32-bit ASI plugin (C++, [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)) that runs inside GTA: San Andreas 1.0 US. Later increments empty San Andreas's map, build Skyrim's collision there and run CJ and his car on it; see [docs/DESIGN.md](../docs/DESIGN.md).

For now it logs `AhShitRim loaded` and the detected game version to `AhShitRim.log` next to the `.asi` (the game's `scripts` folder), and starts the link to the Skyrim plugin (built on `protocol/cpp`). Link state is logged there too, prefixed `link:`.

Only 1.0 US is supported (one exe, one set of addresses). On any other exe the plugin logs why and does nothing else.

- `src/` plugin source
- `extern/plugin-sdk` git submodule ([DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)). `CMakeLists.txt` builds its `plugin_sa` library directly from the sources, mirroring the SDK's own premake project, so no `PLUGIN_SDK_DIR` or premake step is needed.
- `CMakePresets.json` `release` and `debug` presets (Visual Studio 2022 generator, Win32)

Build with `scripts/build.ps1 -Target sa`. Setup is in [docs/DEV_SETUP.md](../docs/DEV_SETUP.md).
