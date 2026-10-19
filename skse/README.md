# skse

SKSE plugin (C++, CommonLibSSE-NG) that runs inside Skyrim. Later increments sample Skyrim's collision for San Andreas and draw CJ and his car; see [docs/DESIGN.md](../docs/DESIGN.md).

It logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, starts the link to the San Andreas plugin (`src/link`, built on `protocol/cpp`) and, in `src/player/PlayerSync.cpp`, hands the player to San Andreas and back with F10: Skyrim's input goes to CJ, and CJ's pose moves the Skyrim player. Lines are prefixed `link:` and `player sync:`.

- `src/` plugin source
- `extern/CommonLibSSE-NG` git submodule ([alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), pinned to release tag `v10.1.0`)
- `vcpkg.json`, `vcpkg-configuration.json` dependency manifest and baseline
- `CMakePresets.json` `release` and `debug` presets (Ninja, MSVC, vcpkg)

Build with `scripts/build.ps1 -Target skse`. Setup is in [docs/DEV_SETUP.md](../docs/DEV_SETUP.md).
