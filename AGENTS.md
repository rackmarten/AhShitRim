# AGENTS.md

Notes for agents working on AhShitRim, headless on Linux or on the user's Windows desktop.

## Read first

`docs/DESIGN.md` (how the two plugins fit together), `docs/PROTOCOL.md` (the shared memory between them) and `docs/DEV_SETUP.md` (the build and the CI).

## On the Windows desktop

Both games, MSVC and the scripts are here, so build and test locally instead of waiting on CI:

- Run `scripts/*.ps1` from the PowerShell tool, or `pwsh -NoProfile -File scripts/<name>.ps1` from Git Bash. They set up the MSVC dev shell themselves; `cmake` isn't on PATH outside them.
- `scripts/build.ps1` builds both plugins, `scripts/test.ps1` runs the game-free tests, `scripts/logs.ps1` tails both plugin logs. Long builds go in the background.
- `scripts/restart-games.ps1` is the whole in-game loop (close, build, deploy, start both, wait for "link up"). `-NoBuild` deploys whatever was built last instead. `scripts/fake-skyrim.ps1` and `scripts/fake-weapons.ps1` check a running San Andreas without Skyrim.
- You may close Skyrim and San Andreas whenever a deploy or test needs it; say so in your reply. Never run `fake_skyrim` or `fake_weapons` while Skyrim is running: both write the input slot and fight over the handover.
- Dev and test keys stay off Skyrim's default binds (F9 is quick load; `/` is the HUD demo). Check the README's keys table first.
- Another session may be using the main checkout. Work in a git worktree of your own, and don't switch branches or commit in someone else's.
- Files are LF (`.gitattributes`). Edit them with the editor tools; PowerShell's `Set-Content` and Python's default text mode on Windows write CRLF.

## Headless sessions (Linux)

A headless session runs on Linux with no MSVC and neither game installed, so `scripts/*.ps1` don't run. The game-free tests do, and CI covers the rest:

- Run `scripts/test.sh` before pushing: it builds and runs the protocol tests natively (gcc or clang; see `docs/DEV_SETUP.md`). Everything but the link's tests runs there. No compiler on the host? `docker run --rm -v "$PWD:$PWD" -w "$PWD" ubuntu:24.04 bash -c 'apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ && scripts/test.sh'` does the same (delete the root-owned `protocol/build/linux-tests` afterwards the same way).
- Every PR gets a `protocol-tests-linux` job running `scripts/test.sh` and a `build` job running `scripts/wine/build.sh` (both plugins with MSVC under Wine, then every doctest case, the link's included), on the self-hosted runner `arcade` (`.github/workflows/build.yml`). Docs-only PRs run nothing. The Pester tests and the release zip only run on GitHub's windows-latest, by hand.
- `gh pr checks` shows the result, `gh run view <run-id> --log-failed` the failing step's log. Fix and push again until it's green.
- Most builds take a minute or two. Jobs wait while someone streams or plays on arcade, and one in progress then is cancelled: re-run it later (`gh run rerun <run-id> --failed`).

So CI can check your logic, put anything that doesn't need a game in a header under `protocol/cpp/` and cover it with doctest cases in `protocol/tests/cpp/` (add new test files to `protocol/CMakeLists.txt`). Keep the game-facing code in `skse/` and `sa/` a thin layer over it.

## Rules

- Every PR description ends with a numbered in-game test checklist for the user: what to start, what to press, what they should see.
- A new shared memory slot or ring, or any other layout change, bumps `kProtocolVersion` in `protocol/ahshitrim_protocol.h` and updates `docs/PROTOCOL.md`.
- Branding: no "GTA" in names (files, types, the mod's UI). Never commit Rockstar or Bethesda assets; the plugins read what they need from the user's own installs at runtime.
- Don't touch `docs/ROADMAP.md`: it's the user's own untracked file.
- Add an entry under `## [Unreleased]` in `CHANGELOG.md` for changes players or developers notice (agent notes like this file don't need one).

## References

San Andreas's internals come from gta-reversed. Read its sources with `gh api repos/gta-reversed/gta-reversed/contents/<path> --jq .content | base64 -d`, e.g. `source/game_sa/Entity/Ped/Ped.cpp`.
