# AGENTS.md

Notes for agents working on AhShitRim without a Windows desktop.

## Read first

`docs/DESIGN.md` (how the two plugins fit together), `docs/PROTOCOL.md` (the shared memory between them) and `docs/DEV_SETUP.md` (the build and the CI).

## You can't build or run anything here

A headless session runs on Linux with no MSVC and neither game installed, so `scripts/*.ps1` don't run. CI is your compiler and test runner:

- Every PR gets a Windows build of both plugins plus `scripts/test.ps1` (`.github/workflows/build.yml`).
- `gh pr checks` shows the result, `gh run view <run-id> --log-failed` the failing step's log. Fix and push again until it's green.
- A cold build (no vcpkg cache) can take around 30 minutes; later ones are much quicker.

So CI can check your logic, put anything that doesn't need a game in a header under `protocol/cpp/` and cover it with doctest cases in `protocol/tests/cpp/` (add new test files to `protocol/CMakeLists.txt`). Keep the game-facing code in `skse/` and `sa/` a thin layer over it.

## Rules

- Every PR description ends with a numbered in-game test checklist for the user: what to start, what to press, what they should see.
- A new shared memory slot or ring, or any other layout change, bumps `kProtocolVersion` in `protocol/ahshitrim_protocol.h` and updates `docs/PROTOCOL.md`.
- Branding: no "GTA" in names (files, types, the mod's UI). Never commit Rockstar or Bethesda assets; the plugins read what they need from the user's own installs at runtime.
- Don't touch `docs/ROADMAP.md`: it's the user's own untracked file.
- Add an entry under `## [Unreleased]` in `CHANGELOG.md`.

## References

San Andreas's internals come from gta-reversed. Read its sources with `gh api repos/gta-reversed/gta-reversed/contents/<path> --jq .content | base64 -d`, e.g. `source/game_sa/Entity/Ped/Ped.cpp`.
