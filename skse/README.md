# skse

SKSE plugin (C++, CommonLibSSE-NG) that runs inside Skyrim. It samples Skyrim's collision for San Andreas and draws CJ from San Andreas's own model; a later increment draws his car; see [docs/DESIGN.md](../docs/DESIGN.md).

It logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, starts the link to the San Andreas plugin (`src/link`, built on `protocol/cpp`) and, in `src/player/PlayerSync.cpp`, hands the player to San Andreas and back with F10: Skyrim's input goes to CJ, and CJ's pose moves the Skyrim player. Meanwhile `src/collision/` (ported from PerkeleRim) streams the terrain and statics around the player to San Andreas, and `src/body/ModelView.cpp` builds CJ's and the vehicles' models when San Andreas sends them and draws them posed by their bones (a vehicle's bones are its parts), while PlayerSync puts Skyrim's camera on San Andreas's. Lines are prefixed `link:`, `player sync:`, `collision:` and `model view:`.

- `src/` plugin source
- `extern/CommonLibSSE-NG` git submodule ([alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), pinned to release tag `v10.1.0`)
- `vcpkg.json`, `vcpkg-configuration.json` dependency manifest and baseline
- `CMakePresets.json` `release` and `debug` presets (Ninja, MSVC, vcpkg)

Build with `scripts/build.ps1 -Target skse`. Setup is in [docs/DEV_SETUP.md](../docs/DEV_SETUP.md).
