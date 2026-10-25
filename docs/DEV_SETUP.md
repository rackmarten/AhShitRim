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
scripts/test.ps1                   # game-free unit tests: protocol layout, link, rings, input mapping, pose, collision chunks, floating origin, fall rescue, CJ's model, skinning, interpolation, texture decoding (doctest), and the launcher's checks and install steps (Pester 5)
scripts/deploy-local.ps1           # copy both plugins into the games (close the games first)
scripts/logs.ps1                   # tail of the plugin logs and the SKSE loader log
scripts/launch-sa.ps1              # start San Andreas hidden (minimized), wait until CJ waits for Skyrim's ground
scripts/launch-sa.ps1 -Visible     # the same in a small visible window, to watch CJ
scripts/fake-weapons.ps1           # with San Andreas running and Skyrim closed: give the weapon sets, switch through every weapon on the mouse wheel, aim and fire each, and check the weapon is in CJ's hand with its model, the shots are counted, twin pistols come in two hands and a loadout set and cleared comes back as sent
scripts/fake-vehicles.ps1          # with San Andreas running and Skyrim closed: ride an NRG-500 and a BMX (and bunny hop it on Left Ctrl), see a boat refused, fly a Hunter (its weapons on the mouse and on Left Alt and Left Ctrl) and a Hydra (hover, forward flight), fire their guns and the Rhino's, fly up on the jetpack and come down on the parachute, then ram a parked car with BSXSGGC on and spawn four planes flying with AIR (AHSHITRIM_ONLY=cheats runs only those)
scripts/hit-test.ps1               # both games, nobody at the keys: loads the most recent save, CJ in a Sandking at Riverwood, a giant and two guards on him (-Scenario hits) or two stars (bust); prints the logs, closes both, never saves
scripts/restart-games.ps1          # the whole loop: close both games, build, deploy, start San Andreas, then Skyrim (-NoBuild skips the build)
scripts/package.ps1                # the player release zip, out/release/AhShitRim-<version>.zip (after a Release build)
scripts/radar-map.ps1              # paint the radar's map of Skyrim from your plugins into Documents\My Games\AhShitRim (-Force to paint it again, -NoMods for the base game only)
scripts/fake-skyrim.ps1            # with San Andreas running and Skyrim closed: hand over, send flat ground, hold W, check CJ walks on it, and that his model arrives and skins to a man's height with moving bones; then spawn a Sandking, drive it, ride a Sanchez, spawn a ZR-350, check a new handover takes CJ out and removes them, and drive 1.3 km through a floating origin move
```

From WSL, call the same scripts through Windows PowerShell, e.g. `powershell.exe -File scripts/build.ps1`.

The first SKSE build takes a while (often 15 to 25 minutes): vcpkg builds the dependencies and CommonLibSSE-NG compiles from source. The first San Andreas build compiles plugin-sdk's few hundred files once. Later builds are incremental. Outputs:

- `skse/build/release/AhShitRim.dll` (+ `.pdb`), deployed to `<SkyrimPath>\Data\SKSE\Plugins\`, next to an `AhShitRim.ini` the deploy writes with `[SanAndreas] Path=<SaPath>`: the plugin reads San Andreas's fonts and HUD sprites from there at startup
- `sa/build/Release/AhShitRim.asi` (+ `.pdb`), deployed to `<SaPath>\scripts\`

### The radar map

`scripts/radar-map.ps1` builds `protocol/tools/radar_map.exe` (the `tools` preset: Release, in `protocol/build/tools`) and runs it on `SkyrimPath` from `local.props`. It reads your load order (`%LOCALAPPDATA%\Skyrim Special Edition\plugins.txt`), paints Tamriel and writes `radar-map.png` and `radar-map.json` to `Documents\My Games\AhShitRim` (`-Out <folder>` elsewhere). It needs neither game running and takes about 5 to 8 seconds; it skips the painting while the plugins haven't changed since the last map (`-Force` paints it anyway). The exe takes the same as arguments (`--skyrim <folder> --out <folder> --force --no-mods`) and finds Skyrim through the registry without `--skyrim`. How it reads and paints is in [DESIGN.md](DESIGN.md#the-radars-map). Never commit the image: it's made from Bethesda's data.

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

### Picking CJ's lines

CJ's lines for his keys, conversations and shouts are picked by ear on a local page: `node scripts/voice-picker/extract.mjs` builds it from your San Andreas install, `uv run scripts/voice-picker/transcribe.py` (optional; [uv](https://docs.astral.sh/uv/) and an NVIDIA GPU) transcribes every line with Whisper so the page can search what he says, and `node scripts/voice-picker/apply.mjs` writes the downloaded picks into `protocol/cpp/voice_lines.h`. Everything extracted or transcribed stays in the gitignored `extracted/`. See [`scripts/voice-picker/README.md`](../scripts/voice-picker/README.md).

Milestone 1's ground check: from the stables outside Whiterun, walk up the road to the gate, over the rough tundra beside it, and (F10 again inside the city) up the steps to Dragonsreach. CJ should never fall through or hover, and San Andreas's log should show no `below the ground` line on normal ground.

### Load test

`scripts/load-test.ps1` runs hundreds of loads unattended and counts the crashes: copy any two saves to `StressA` and
`StressB` (both files, `.ess` and `.skse`) in Skyrim's `Saves` folder, then run it (it closes and starts both games
itself, so nobody else may be using them). The SKSE plugin's load test (`[Debug] LoadTest` in `AhShitRim.ini`, which the
script sets) alternates loads between the two saves and runs one scenario per cycle (idle, people scared, a 6-star chase
loaded through, ASNAEB, a coc, AEZAKMI, an F10 hand-back), logging every step and who is in a fight before each load.
A crash, a hang or Skyrim gone is recorded with its cycle, the games are started again and the test goes on. Results:
`Documents/My Games/AhShitRim/loadtest-results.csv` (one line per run event) and `loadtest.csv` (every step).
`-Scenarios chase` runs only some scenarios; `-Dll` tests another build.

## CI

Every pull request and every push to `main` or `production` that touches more than `docs/` and Markdown files, and every manual run (Actions > build > Run workflow), builds on **arcade**, a self-hosted runner (a Debian VM in the homelab, runner name `arcade`, label `msvc-wine`). It costs no Actions minutes, which matters on a private repo: GitHub's Windows runners bill double. Two jobs, one after the other on the one runner:

- `protocol-tests-linux`: `scripts/test.sh` (below) with gcc, as a headless session runs it.
- `build`: `scripts/wine/build.sh` (below): both plugins with the real MSVC under Wine, then the whole doctest suite under Wine, the link's tests included. The checkout and build folders stay on the runner (`clean: false`), so a typical PR builds in a minute or two; the first build after a fresh runner or a CommonLib bump takes about 25 minutes. If it fails or hangs (`cl.exe` under Wine now and then spins forever on one file), the job stops Wine and runs it once more. It uploads `AhShitRim-<commit>` with `AhShitRim.dll`, `AhShitRim.asi` and `AhShitRim.map`, kept for 7 days.

A newer push to the same branch cancels the run in progress. While someone streams or plays on arcade, `cue-gpu` stops the runner (a job in progress is cancelled: re-run it from the PR once they're done) and jobs wait in the queue. The runner is set up with `scripts/wine/setup-msvc-wine.sh` and then `scripts/wine/setup-runner.sh` (its header has the one command to run from the desktop); the arcade repo's README, "AhShitRim CI runner", covers it from the machine's side.

A healthcheck keeps the runner up (`scripts/wine/runner-healthcheck.sh`, installed by `setup-runner.sh` as `~/.local/bin/actions-runner-healthcheck` and the user timer `actions-runner-healthcheck.timer`, every 3 minutes). While nobody holds the machine, it starts a stopped runner, restarts one GitHub still shows offline a few minutes after it started, and restarts a frozen one: idle on GitHub while a `msvc-wine` job has waited over 3 minutes (the same signal as sandbox's runner healthcheck; the runner's own log stays quiet while it's healthy, so it tells you nothing). A runner that hangs inside a job is left to the workflow's timeouts. Each check's outcome is in `journalctl --user -u actions-runner-healthcheck`, and `DRY_RUN=1 actions-runner-healthcheck` checks without touching anything.

It reads GitHub with `gh` on arcade, logged in once with a read-only token: a fine-grained personal access token (github.com > Settings > Developer settings > Fine-grained tokens) for `rackmarten/AhShitRim` only, with **Administration: Read-only** (to list the repo's runners) and **Actions: Read-only**. Copy it, then from PowerShell on the desktop:

```powershell
Get-Clipboard | wsl -d Debian -u marek -e ssh arcade gh auth login --with-token --insecure-storage
wsl -d Debian -u marek -e ssh arcade gh auth status
```

`--insecure-storage` keeps it in `~/.config/gh/hosts.yml` (readable by `marek` only) rather than the desktop's keyring, which is locked until someone logs in at the monitor, so the timer can use it after a reboot. When the token expires, the journal shows `WARN: couldn't read the runner's status`; make a new one and log in again.

What arcade doesn't do: the launcher's Pester tests (`scripts/test.ps1`) and the player release zip (`scripts/package.ps1`) need PowerShell on Windows. For those, run the workflow by hand with **Also build on GitHub's windows-latest** ticked. That `build-windows` job builds both plugins on GitHub's `windows-latest` (Visual Studio 2026; `scripts/build.ps1` switches the San Andreas build to its generator) with the desktop's scripts: `scripts/build.ps1 -Target skse`, `scripts/build.ps1 -Target sa -PluginSaCache .plugin-sa`, `scripts/test.ps1`, then `scripts/package.ps1`, and uploads `AhShitRim-windows-<commit>` (`AhShitRim.dll` + `.pdb`, `AhShitRim.asi` + `.map`) and `AhShitRim-release-<commit>` (the release zip). It uses Actions minutes. There's no `local.props` there, so the HUD test that reads a real San Andreas install skips itself. vcpkg's binary archives are cached between its runs, and so is plugin-sdk's `plugin_sa.lib`, keyed on the plugin-sdk commit, `sa/cmake/plugin_sa.cmake` and the MSVC toolset (`-PluginSaCache` links the cached one when it's there and keeps a fresh build there otherwise; `PLUGIN_SA_LIB` in CMake, Release only). Caches GitHub hasn't seen used for a week are dropped, so a manual run after a quiet spell builds from scratch (about 30 minutes).

To try a PR in game, either:

- build it: `git fetch && git switch <branch>`, then `scripts/build.ps1` and `scripts/deploy-local.ps1` (or `scripts/restart-games.ps1`), or
- download its artifact (`gh run download <run-id>`), put the files where a local build would (`AhShitRim.dll` in `skse/build/release/`, `AhShitRim.asi`/`.map` in `sa/build/Release/`) and run `scripts/deploy-local.ps1`, which also writes `AhShitRim.ini`, or
- run the workflow by hand with windows-latest ticked and install its release zip (the `AhShitRim-release-<commit>` artifact) as a player would, following [INSTALL.md](INSTALL.md).

### Protocol tests on Linux

The game-free doctest cases also build natively on Linux with gcc or clang, through the `linux-tests` preset in `protocol/CMakePresets.json` (Ninja, Debug). It needs no vcpkg: CMake downloads a pinned doctest release itself (`AHSR_FETCH_DOCTEST`). The link (`link.cpp`, `test_link.cpp`, `fake_skyrim`) is Windows' shared memory and is left out, and so are the launcher's Pester tests. You need CMake 3.21 or later, Ninja and a C++23 compiler (gcc 13 or clang 18 do; on Ubuntu 24.04, `sudo apt install cmake ninja-build g++`). Then:

```bash
scripts/test.sh                                  # configure, build and ctest the linux-tests preset
AHSR_SA_PATH=/path/to/san-andreas scripts/test.sh   # also read a real install's fonts and HUD textures
CXX=clang++ scripts/test.sh                      # clang instead (on a fresh build directory)
```

### Building on Linux with msvc-wine

Both plugins and the whole doctest suite (the link's tests too) build on Linux with the real MSVC running under Wine ([mstorsjo/msvc-wine](https://github.com/mstorsjo/msvc-wine)), so a Linux box can check what otherwise needs the Windows CI. Tried on a Debian 13 VM (`arcade`: 10 cores, 16 GB, Wine 10).

```bash
scripts/wine/setup-msvc-wine.sh     # once: apt packages (sudo), MSVC + Windows SDK into ~/msvc, vcpkg into ~/vcpkg
scripts/wine/build.sh               # SKSE plugin, San Andreas plugin, then the tests under Wine
scripts/wine/build.sh tests         # any of: skse sa tests
```

Downloading MSVC accepts Microsoft's Visual Studio licence, and the toolchain stays on that machine. The setup script installs the newest Visual Studio 2026 toolset (14.51, what `windows-latest` builds with); `MSVC_VERSION=17.14` gets 14.44 instead. `build.sh` uses Ninja for both plugins (the Visual Studio generator needs MSBuild), vcpkg with an overlay `x64-windows-static-md` triplet that chainloads `scripts/wine/msvc-wine.cmake`, and runs DirectXTK's shader batch file under Wine's `cmd` (`scripts/wine/bin/`). `lib` and `link` go through `scripts/wine/long-args.sh`, which moves a command line too long for Windows into a response file with its paths rewritten for Wine, and the tests find the debug CRT DLLs through `WINEPATH`. It refuses to start while someone holds the machine's GPU for a game stream (a file in `$XDG_RUNTIME_DIR/cue-gpu/holders`, `BUSY_DIR` to change it, `FORCE=1` to skip), and prints each target's time and the outputs' machine type and exports at the end.

What happened (2026-10-23, MSVC 14.51, `-j 5`):

| Run | SKSE | San Andreas | Tests | Total |
| --- | --- | --- | --- | --- |
| Cold: fresh clone, empty vcpkg binary cache | ~22 min (vcpkg ports ~5, CommonLibSSE most of the rest) | ~3 min | ~1 min | ~26 min |
| Nothing changed | 13 s | 2 s | 7 s | 22 s |
| `protocol/ahshitrim_protocol.h` touched | 34 s | 29 s | 28 s | ~1.5 min |

- The SKSE DLL is the same size as CI's build of the same code (1,184,256 bytes), x64, with `SKSEPlugin_Load`, `SKSEPlugin_Query` and `SKSEPlugin_Version` exported. The ASI is x86 and 512 bytes smaller than CI's (1,254,400 against 1,254,912; Ninja's flags against the Visual Studio generator's). All doctest cases pass under Wine, the link's included. Neither binary has been tried in game yet.
- So it's a go for a self-hosted runner that keeps its build folders: a typical PR is a minute or two instead of the Windows runner's 7 to 10 (billed double).

Known gaps:

- `cl.exe` under Wine sometimes spins forever on one file (100% of a core, no output): once in four full builds, on a plugin-sdk file that compiled fine in the other runs. Killing that `cl.exe` and running `build.sh` again finishes the build. A runner needs a job timeout and `wineserver -k` (with this `WINEPREFIX`) before a retry.
- Compiling with every core (`-j 10`) on 16 GB once crashed `c2.dll` (an `int3`) partway through CommonLibSSE, most likely out of memory, so `build.sh` defaults to half the cores (`JOBS` overrides). If a compile crashes, run it again; ninja carries on where it stopped.
- Fixed along the way: CommonLibSSE's `lib.exe` call never started, because CMake on a Linux host puts all ~520 objects on one command line, over Windows' 32767 characters, and msvc-wine's wrapper then waits on its pipes forever (`long-args.sh` now). Forcing Ninja response files instead broke every compile (msvc-wine never rewrites the `/home/...` paths inside them). Then `lib.exe` couldn't open its 300th object (`LNK1104`): Wine holds a few file descriptors per mapped object, past the soft limit of 1024, so `build.sh` raises it.
- The launcher's Pester tests (`scripts/test.ps1`) and `scripts/package.ps1` don't run here; they need PowerShell on Windows.

## Branches and releases

From 0.1 on there are two long-lived branches:

- **`main`** is the development version. Features and fixes for the next release go there through pull requests, as before.
- **`production`** is what players get. A release is `main` merged into `production`, and release tags (`v0.1.0`, `v0.1.1`, ...) exist only on `production`, each on the commit its release zip is built from.
- **Hotfixes** for players branch from `production` (`fix-<slug>`), go through a pull request into `production`, are tagged there, and `production` is then merged back into `main`.

CI builds every push to either branch and every pull request into either. Merge pull requests into `production` with a merge commit, never squash, so `main` and `production` keep a shared history and the merge back stays small.

### Cutting a release

1. On `main`, through an ordinary pull request: set the version (`VERSION` in `skse/CMakeLists.txt` and `sa/CMakeLists.txt`, `version-semver` in `skse/vcpkg.json` and `protocol/vcpkg.json`), and in `CHANGELOG.md` rename `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD` with a new empty `## [Unreleased]` above it. Merge it.
2. Open the release pull request from `main` into `production`, wait for CI, and merge it with a merge commit:

   ```bash
   gh pr create --base production --head main --title "Release X.Y.Z" --body "Release notes: CHANGELOG.md, X.Y.Z"
   gh pr merge --merge <number>
   ```

3. Tag the merge on `production` and push the tag:

   ```bash
   git fetch origin
   git switch production
   git merge --ff-only origin/production
   git tag -a vX.Y.Z -m "AhShitRim X.Y.Z"
   git push origin vX.Y.Z
   ```

4. Build the zip from the tag on the desktop (`scripts/build.ps1`, then `scripts/package.ps1`; run `scripts/encode-music.ps1` first if Sweetroll FM's tracks ship) and publish it as the tag's release:

   ```bash
   gh release create vX.Y.Z out/release/AhShitRim-X.Y.Z.zip --title "AhShitRim X.Y.Z" --notes-file <release notes>
   ```

### A hotfix

1. Branch from `production`, fix it, and set the patch version and its `CHANGELOG.md` section (`## [X.Y.Z+1] - YYYY-MM-DD`) on the same branch:

   ```bash
   git fetch origin
   git switch -c fix-<slug> origin/production
   ```

2. Push it and open the pull request into `production`; once CI is green, merge it with a merge commit:

   ```bash
   git push -u origin fix-<slug>
   gh pr create --base production --title "Fix: <what>"
   gh pr merge --merge <number>
   ```

3. Tag it on `production` and publish it, as steps 3 and 4 of a release.
4. Merge `production` back into `main`, through a pull request so CI checks the result:

   ```bash
   git switch -c merge-production-vX.Y.Z origin/main
   git merge --no-ff origin/production -m "Merge production (vX.Y.Z) into main"
   git push -u origin merge-production-vX.Y.Z
   gh pr create --base main --title "Merge production (vX.Y.Z) into main"
   ```

   Keep `main`'s own version and `## [Unreleased]` section in the conflict, with the hotfix's changelog section below it. A hotfix should avoid changing the shared memory's layout; if it has to, it takes a `kProtocolVersion` above `main`'s, and `main` renumbers on the merge back.

Agents open feature pull requests against `main` and hotfix pull requests against `production`, and never create tags or releases: those are the maintainer's.

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
