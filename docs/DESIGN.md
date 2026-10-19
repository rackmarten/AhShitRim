# AhShitRim: design

> Status: milestone 1 in progress. The two plugins build, load in their games and find each other over shared memory (protocol v1). Nothing is simulated or drawn yet.

AhShitRim runs **GTA: San Andreas** and **Skyrim Anniversary Edition** at the same time. Skyrim owns the world and draws everything; San Andreas runs hidden and simulates CJ, his cars, his guns and his cheat codes.

It's a sibling of [PerkeleRim](https://github.com/rackmarten/PerkeleRim) (My Summer Car in Skyrim) and is built the same way. Where this document is silent, PerkeleRim's `docs/DESIGN.md` describes the shared approach.

## 1. Goals

- CJ on foot and in San Andreas's vehicles, with San Andreas's own movement, animations and vehicle physics, on Skyrim's terrain and statics.
- Skyrim keeps working as Skyrim: its own player actor stays the real player, so dialogue, quests, bounties and kill credit keep working. CJ is the body the player drives.
- No assets from either game in the repository or a release. Models, textures and sounds are read from the player's own installs at play time.

## 2. Non-goals (for now)

- Guns, NPCs, quests, cheat codes and the wanted level (after milestone 1, see the README).
- San Andreas's own world. Its map is emptied; only Skyrim's world exists.
- Any San Andreas exe other than 1.0 US, and the Definitive Edition (a different engine).

## 3. Architecture

```
 Skyrim AE (64-bit)                          GTA: San Andreas 1.0 US (32-bit, hidden)
 ┌──────────────────────────────┐            ┌────────────────────────────────────┐
 │ SKSE plugin  (skse/)         │            │ ASI plugin  (sa/)                  │
 │ CommonLibSSE-NG, C++         │  shared    │ plugin-sdk, C++                    │
 │ - samples Skyrim collision   │  memory    │ - empties SA's map                 │
 │ - forwards input             │ ◄────────► │ - builds Skyrim's ground around CJ │
 │ - draws CJ and the car       │ protocol/  │ - runs CJ and vehicle physics      │
 │ - owns quests, NPCs, saves   │            │ - streams poses and events back    │
 └──────────────────────────────┘            └────────────────────────────────────┘
```

- **Skyrim side** (`skse/`): an SKSE plugin on CommonLibSSE-NG, the same build setup as PerkeleRim's `skse/`. Code from PerkeleRim is copied in as each increment needs it (collision sampler, model building, input forwarding, launcher pieces); there is no shared library yet.
- **San Andreas side** (`sa/`): a 32-bit ASI plugin on [plugin-sdk](https://github.com/DK22Pac/plugin-sdk), loaded by an ASI loader. [gta-reversed](https://github.com/gta-reversed/gta-reversed) is the reference for San Andreas's internals.
- **Link** (`protocol/`): one versioned shared memory section; see [PROTOCOL.md](PROTOCOL.md). The same C++ link code compiles into both plugins (64-bit and 32-bit), so the header uses fixed-width types only.

### One exe version

Only GTA: San Andreas **1.0 US** is supported: one exe means one set of memory addresses, and plugin-sdk and gta-reversed both target it. The current Steam build is downgraded to it once (docs/DEV_SETUP.md). On any other exe the ASI logs why and does nothing.

### Ownership rules

- Skyrim owns the world, the player actor, NPCs, quests, crime and saves.
- San Andreas owns CJ's and the vehicles' physics and animation state while the player is handed to it (F10, as in PerkeleRim).
- Each shared memory slot has exactly one writer.

### Ground: Skyrim's collision in San Andreas

San Andreas's world is about 6 km across and its collision is loaded as static per-sector files, so Skyrim's ground has to be built there at runtime, around CJ, with a **floating origin** (Skyrim positions are re-based near San Andreas's origin and the base moves with CJ).

- First try: spawn collision objects around CJ from Skyrim's sampled collision.
- Fallback, if San Andreas's pool limits get in the way: hook its collision queries to read Skyrim's mesh directly.

Whichever wins is recorded here.

### Running San Andreas hidden

San Andreas runs windowed and hidden, started and driven from Skyrim. Its physics stays capped at 30 fps, since higher frame rates break it.

## 4. Milestone 1: on the ground

| Step | What |
|---|---|
| Scaffold (done) | Both plugins build and load; shared memory handshake, protocol v1 |
| Hidden runner | San Andreas started hidden from Skyrim, input forwarded |
| Ground | Skyrim's collision built in San Andreas around CJ (the make-or-break part) |
| CJ | CJ drawn in Skyrim from San Andreas's own model |
| Car | The Greenwood driven on Skyrim's roads |

## 5. Repo layout

| Path | What |
|---|---|
| `skse/` | SKSE plugin; `extern/CommonLibSSE-NG` submodule |
| `sa/` | ASI plugin; `extern/plugin-sdk` submodule |
| `protocol/` | Shared protocol header, C++ link code and its game-free tests |
| `scripts/` | `build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1` |
| `docs/` | This document, [PROTOCOL.md](PROTOCOL.md), [DEV_SETUP.md](DEV_SETUP.md) |

## 6. Risks

- **Collision at runtime** is the open question (see Ground). If neither approach holds up, the project stops at milestone 1.
- **San Andreas updates** through Steam undo the downgrade. Keep the downgraded copy outside Steam, or run the downgrader again.
- **Two engines in one session**: both games must run at once; San Andreas is light, but its frame pacing affects physics.
