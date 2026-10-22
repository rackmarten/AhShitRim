# Windows development setup

Everything needed to build both halves of AhShitRim, deploy them into your game installs and read their logs. Follow it top to bottom on a fresh Windows 10/11 machine.

The toolchain is one install: **Build Tools for Visual Studio 2022** (compiler only, no IDE) builds both the 64-bit SKSE plugin and the 32-bit ASI plugin. Visual Studio itself is optional.

## 1. Games

You need your own copies of both games. Nothing from them is ever committed to this repo.

### Skyrim Anniversary Edition

1. Install **The Elder Scrolls V: Skyrim Special Edition** from Steam, with the Anniversary upgrade if you own it. Keep it on the latest Steam version.
2. Stop Steam from updating it behind your back: Steam > Skyrim > Properties > Updates > "Only update this game when I launch it", and launch through SKSE, not Steam.
3. Install **SKSE64** for your runtime from <https://skse.silverlock.org/> (the "Current Anniversary Edition build"). Copy `skse64_loader.exe`, `skse64_*.dll` and the `Data` folder contents next to `SkyrimSE.exe`.
4. Install **Address Library for SKSE Plugins** from <https://www.nexusmods.com/skyrimspecialedition/mods/32444>, the "All in one (Anniversary Edition)" file. Its `versionlib-*.bin` files go into `Data\SKSE\Plugins`.
5. Launch `skse64_loader.exe` once, quit, and confirm `Documents\My Games\Skyrim Special Edition\SKSE\skse64.log` exists.

Already set up for PerkeleRim? Then Skyrim is ready as it is.

### GTA: San Andreas 1.0 US

AhShitRim supports exactly one San Andreas exe, **1.0 US**: one exe means one set of memory addresses. The Steam copy is a later build, so it is downgraded once. The Definitive Edition is a different engine and won't work.

1. Install the legacy Steam **Grand Theft Auto: San Andreas** (app 12120, not the Definitive Edition). Steam > Properties > Updates: only update when launched.
2. Copy the whole game folder (`steamapps\common\Grand Theft Auto San Andreas`) somewhere outside Steam, e.g. `C:\Games\Grand Theft Auto San Andreas`, so Steam never touches the downgraded copy. Keep a backup.
3. Download `gtasa-open-downgrader-windows.exe` from the latest release of <https://github.com/xxanqw/gtasa-open-downgrader> (open source). Run it, point it at the copied folder, check it identifies the current Steam build (NewSteam R2), and downgrade to **1.0 US**.
4. In the same app, install the **ASI Loader** and **SilentPatch**, and apply the 4GB flag. Skip GInput, SkyGFX, 2DFX, widescreen and frame rate mods: fewer variables, and AhShitRim caps San Andreas at 30 fps itself.
5. Start `gta_sa.exe` directly (not through Steam). The mouse should work in the menu and a new game should load.

The downgraded folder has `gta_sa.exe` (1.0 US, the one to start). A `gta-sa.exe` may sit next to it: that's the old Steam exe, don't use it. If Steam ever updates or verifies a copy you play from, run the downgrader again.

The ASI loader loads `.asi` files from the game folder and its `scripts` folder; `scripts/deploy-local.ps1` puts AhShitRim's there.

## 2. Build tools

Run these from an elevated PowerShell. Every tool has a winget package.

```powershell
# Git (CommonLibSSE-NG and plugin-sdk are submodules, and vcpkg fetches its registry with git)
winget install --id Git.Git -e

# Build Tools for VS 2022: MSVC (x64 and x86), Windows SDK, CMake, Ninja and vcpkg. Compiler only, no IDE.
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.Vcpkg"
```

Already have Build Tools? Open **Visual Studio Installer** > Build Tools 2022 > Modify, tick **Desktop development with C++**, and make sure "C++ CMake tools for Windows" and "vcpkg package manager" are ticked on the right. The workload's "MSVC v143 x64/x86 build tools" covers both the 64-bit and the 32-bit build.

You don't need to put CMake, Ninja or vcpkg on your PATH. The scripts find the Build Tools install with `vswhere`, enter its developer shell, and use the copies bundled there (that shell also sets `VCPKG_ROOT`; an existing `VCPKG_ROOT` pointing at a standalone vcpkg is respected).

plugin-sdk needs no separate setup: no `PLUGIN_SDK_DIR` variable and no premake step. It's a submodule, and `sa/cmake/plugin_sa.cmake` compiles its `plugin_sa` library directly.

## 3. Clone and configure

```powershell
git clone --recurse-submodules git@github.com:rackmarten/AhShitRim.git
cd AhShitRim
Copy-Item local.props.example local.props
```

Edit `local.props` (gitignored) and set:

| Property | Example |
|---|---|
| `SkyrimPath` | `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition` |
| `SaPath` | `C:\Games\Grand Theft Auto San Andreas` (the downgraded folder with `gta_sa.exe`) |

If you cloned without `--recurse-submodules`, `scripts/build.ps1` initialises the submodules for you.

If PowerShell refuses to run the scripts, allow local scripts once for your user:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

## 4. Build, deploy, check logs

```powershell
scripts/build.ps1                  # both halves, Release
scripts/build.ps1 -Target skse     # SKSE plugin only (-Configuration Debug for a debug build)
scripts/build.ps1 -Target sa       # San Andreas ASI only
scripts/test.ps1                   # game-free unit tests: protocol layout, link, rings, input mapping, pose, collision chunks, floating origin, fall rescue, CJ's model, skinning, interpolation, texture decoding (doctest), and the launcher's checks (Pester 5)
scripts/deploy-local.ps1           # copy both plugins into the games (close the games first)
scripts/logs.ps1                   # tail of the plugin logs and the SKSE loader log
scripts/launch-sa.ps1              # start San Andreas hidden (minimized), wait until CJ waits for Skyrim's ground
scripts/launch-sa.ps1 -Visible     # the same in a small visible window, to watch CJ
scripts/fake-weapons.ps1           # with San Andreas running and Skyrim closed: give the weapon sets, switch through every weapon on the mouse wheel, aim and fire each, and check the weapon is in CJ's hand with its model, the shots are counted, twin pistols come in two hands and a loadout set and cleared comes back as sent
scripts/restart-games.ps1          # the whole loop: close both games, build, deploy, start San Andreas, then Skyrim (-NoBuild skips the build)
scripts/package.ps1                # the player release zip, out/release/AhShitRim-<version>.zip (after a Release build)
scripts/fake-skyrim.ps1            # with San Andreas running and Skyrim closed: hand over, send flat ground, hold W, check CJ walks on it, and that his model arrives and skins to a man's height with moving bones; then spawn a Sandking, drive it, ride a Sanchez, spawn a ZR-350, check a new handover takes CJ out and removes them, and drive 1.3 km through a floating origin move
```

From WSL, call the same scripts through Windows PowerShell, e.g. `powershell.exe -File scripts/build.ps1`.

The first SKSE build takes a while (often 15 to 25 minutes): vcpkg builds the dependencies and CommonLibSSE-NG compiles from source. The first San Andreas build compiles plugin-sdk's few hundred files once. Later builds are incremental. Outputs:

- `skse/build/release/AhShitRim.dll` (+ `.pdb`), deployed to `<SkyrimPath>\Data\SKSE\Plugins\`, next to an `AhShitRim.ini` the deploy writes with `[SanAndreas] Path=<SaPath>`: the plugin reads San Andreas's fonts and HUD sprites from there at startup
- `sa/build/Release/AhShitRim.asi` (+ `.pdb`), deployed to `<SaPath>\scripts\`

### Handshake check

1. Start Skyrim with `skse64_loader.exe` and wait for the main menu.
2. Start San Andreas with `scripts/launch-sa.ps1` (or `<SaPath>\gta_sa.exe`, which starts minimized as well). The order doesn't matter.
3. Run `scripts/logs.ps1`. Expect, in both `AhShitRim.log` files:
   - `AhShitRim loaded (...)`: the Skyrim runtime version on one side, `1.0 US` (compact or HOODLUM) on the other
   - `link: started (protocol v4)`, then `created shared memory` on whichever started first and `joined shared memory` on the other
   - `link: link up: San Andreas plugin pid N, protocol v4` (Skyrim's log) and `link: link up: Skyrim plugin pid N, protocol v4` (San Andreas's log)
4. Quit San Andreas: Skyrim's log shows `link down: the San Andreas plugin detached` (or `no heartbeat ... for 2000 ms` if it didn't exit cleanly). Start it again and the link comes back up.

On any San Andreas exe other than 1.0 US, its log says which version it found and that it isn't starting.

### Driving CJ from Skyrim

1. `scripts/restart-games.ps1` (add `-SaVisible` to watch San Andreas in a small window).
2. Load a save in Skyrim, outdoors or in.
3. Press F10: Skyrim switches to first person and streams its collision around you to San Andreas; CJ waits at your spot until the ground under him is in (a fraction of a second), then WASD, the mouse, Shift (sprint), Space (jump), C (crouch) and Alt (walk) drive CJ on Skyrim's terrain and statics, and his movement moves you. F10 again gives the player back.
4. `scripts/logs.ps1` shows `player sync:` once a second and `collision:` per chunk on the Skyrim side, and `ground:` and `runner:` every 5 s on the San Andreas side (chunks built, frames, inputs, CJ's position).

Milestone 1's ground check: from the stables outside Whiterun, walk up the road to the gate, over the rough tundra beside it, and (F10 again inside the city) up the steps to Dragonsreach. CJ should never fall through or hover, and San Andreas's log should show no `below the ground` line on normal ground.

## CI

Windows minutes bill double on a private repo, so the workflow runs sparingly. A pull request runs it unless it only touches `docs/` and Markdown files, and a draft gets only the Linux tests below until it's marked ready for review. A push to `main` runs it only when a cache key's inputs change (the `vcpkg.json` and `vcpkg-configuration.json` files, `sa/cmake/plugin_sa.cmake`, either submodule, the workflow), because a PR can restore only the caches saved on its own branch or on `main`. A new MSVC toolset on the runner changes the plugin_sa key too: then run it by hand on `main` (Actions > build > Run workflow).

Each Windows run builds both plugins on a GitHub `windows-latest` runner (Visual Studio 2026; `scripts/build.ps1` switches the San Andreas build to its generator) with the same scripts: `scripts/build.ps1 -Target skse`, `scripts/build.ps1 -Target sa -PluginSaCache .plugin-sa`, `scripts/test.ps1`, then `scripts/package.ps1`. There's no `local.props` on the runner, so the HUD test that reads a real San Andreas install skips itself. vcpkg's binary archives are cached between runs, so only the first build (or one after a `vcpkg.json` change) is slow. So is plugin-sdk's `plugin_sa.lib`, keyed on the plugin-sdk commit, `sa/cmake/plugin_sa.cmake` and the MSVC toolset: `-PluginSaCache` links the cached one when it's there and keeps a fresh build there otherwise (`PLUGIN_SA_LIB` in CMake, Release only). A newer push to the same branch cancels the run in progress.

Next to it, a `protocol-tests-linux` job on `ubuntu-latest` runs `scripts/test.sh` (below) in a couple of minutes, so a broken test shows up long before the Windows build finishes.

### Protocol tests on Linux

The game-free doctest cases also build natively on Linux with gcc or clang, through the `linux-tests` preset in `protocol/CMakePresets.json` (Ninja, Debug). It needs no vcpkg: CMake downloads a pinned doctest release itself (`AHSR_FETCH_DOCTEST`). The link (`link.cpp`, `test_link.cpp`, `fake_skyrim`) is Windows' shared memory and is left out, and so are the launcher's Pester tests. You need CMake 3.21 or later, Ninja and a C++23 compiler (gcc 13 or clang 18 do; on Ubuntu 24.04, `sudo apt install cmake ninja-build g++`). Then:

```bash
scripts/test.sh                                  # configure, build and ctest the linux-tests preset
AHSR_SA_PATH=/path/to/san-andreas scripts/test.sh   # also read a real install's fonts and HUD textures
CXX=clang++ scripts/test.sh                      # clang instead (on a fresh build directory)
```

Each run uploads two artifacts (the run's page, under Artifacts): `AhShitRim-<commit>` with `AhShitRim.dll` + `.pdb` and `AhShitRim.asi` + `.map`, and `AhShitRim-release-<commit>` with the player release zip.

To try a PR in game, either:

- build it: `git fetch && git switch <branch>`, then `scripts/build.ps1` and `scripts/deploy-local.ps1` (or `scripts/restart-games.ps1`), or
- install its release zip (the `AhShitRim-release-<commit>` artifact) as a player would, following [INSTALL.md](INSTALL.md), or
- download its artifact (`gh run download <run-id>`), put the files where a local build would (`AhShitRim.dll`/`.pdb` in `skse/build/release/`, `AhShitRim.asi`/`.map` in `sa/build/Release/`) and run `scripts/deploy-local.ps1`, which also writes `AhShitRim.ini`.

## How the builds work

### SKSE plugin (`skse/`)

- CMake + Ninja with the MSVC x64 toolset, C++23, dependencies from vcpkg in manifest mode (`skse/vcpkg.json`, pinned by the baseline in `skse/vcpkg-configuration.json`), triplet `x64-windows-static-md`. Same setup as PerkeleRim.
- **CommonLib:** [`alandtse/CommonLibSSE-NG`](https://github.com/alandtse/CommonLibSSE-NG) as a submodule at `skse/extern/CommonLibSSE-NG`, pinned to release tag `v10.1.0`. It's the maintained continuation of CommonLibSSE-NG, supports the current AE runtimes, builds one DLL for SE, AE and VR, and uses Address Library. Licence: GPL-3.0-or-later with a modding exception.
- To bump CommonLib: `git -C skse/extern/CommonLibSSE-NG fetch --tags && git -C skse/extern/CommonLibSSE-NG checkout <tag>`, copy its `vcpkg-configuration.json` baseline into `skse/vcpkg-configuration.json`, add any new dependencies from its `vcpkg.json` to `skse/vcpkg.json`, then rebuild.

### San Andreas plugin (`sa/`)

- CMake with the **Visual Studio 2022 generator, `-A Win32`** (preset `x86`): the game is 32-bit, and the VS generator picks the x86 compiler itself, so no second developer shell is needed. Output is `sa/build/<Configuration>/AhShitRim.asi`.
- **plugin-sdk:** [`DK22Pac/plugin-sdk`](https://github.com/DK22Pac/plugin-sdk) as a submodule at `sa/extern/plugin-sdk` (it has no release tags; the submodule pins a commit). `sa/cmake/plugin_sa.cmake` builds its `plugin_sa` static library from the sources with the same defines as the SDK's own premake project (`GTASA`, `PLUGIN_SGV_10US`, `RW`, ...), static CRT, `/std:c++latest`.
- To bump plugin-sdk: `git -C sa/extern/plugin-sdk pull origin master`, rebuild, and check the version detection still logs `1.0 US`.
- The link code (`protocol/cpp/link.cpp`) compiles into the ASI as 32-bit code. The protocol header's `static_assert`s run there too, so a layout that differs between 32-bit and 64-bit fails the build.

## Troubleshooting

- **`... was unexpected at this time` when entering the developer shell:** a quote character in your `PATH` confuses `VsDevCmd.bat`. The scripts strip quotes for their own process, but it's worth fixing the entry in System Properties > Environment Variables.
- **No `AhShitRim.log` in San Andreas's `scripts` folder:** the ASI loader isn't installed or isn't loading from `scripts`. Reinstall it from the downgrader, and check `scripts\global.ini` has `LoadPlugins=1`.
- **San Andreas exits or crashes at start:** its log ends with a `crash:` line (the address and the exe addresses on the stack; match them against gta-reversed) or stops before `runner: CJ waits for Skyrim's ground`. Another ASI that patches the same functions is the usual suspect; only SilentPatch, ModLoader and the ASI loader are tested.
- **San Andreas's log says the game version isn't supported:** you started the Steam exe (`gta-sa.exe`) or a copy Steam updated. Start the downgraded `gta_sa.exe`, or run the downgrader again.
- **Skyrim crashes at start, or SKSE says the plugin is incompatible:** the game updated. Update SKSE and Address Library to match.
