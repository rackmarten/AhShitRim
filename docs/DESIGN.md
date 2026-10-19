# AhShitRim: design

> Status: milestone 1 in progress. San Andreas runs hidden as a simulator of CJ, driven from Skyrim with F10, on Skyrim's own terrain and statics, built around him as collision objects (protocol v4). CJ's movement moves the Skyrim player. Nothing from San Andreas is drawn in Skyrim yet.

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

San Andreas's world is about 6 km across and its collision is loaded as static per-sector files, so Skyrim's ground has to be built there at runtime, around CJ, with a **floating origin**.

Two approaches were on the table: spawn collision objects around CJ, or hook San Andreas's collision queries to test against Skyrim's mesh directly. **Spawned collision objects won**, and the query hooks aren't needed:

- San Andreas's collision format takes triangle meshes as they are (`CColModel` with compressed vertices and triangles), so a chunk becomes one invisible `CBuilding` whose model has the chunk as its collision. Every collision query San Andreas makes (peds, cars, camera, line of sight) then sees Skyrim's ground with no hook at all.
- The pools weren't the limit: San Andreas keeps at most 169 chunks (13 x 13), and its building pool has room for them next to the 9,200 map buildings the empty world takes out of the world. The limit that did bite is the store of atomic model infos, which is full (`CModelInfo::AddAtomicModel` crashed), so each chunk slot borrows the model info of one of the map's own atomic models instead, unused once the map is emptied.
- Hooking the queries would have meant reimplementing San Andreas's sphere, box and line tests against a foreign mesh across a dozen functions, for no gain over handing it collision it already understands.

How it works:

- **Skyrim side** (`skse/src/collision/`, ported from PerkeleRim): while San Andreas has the player, the collision sampler reads the land as a heightfield (33 x 33 Havok raycasts per 2048-unit chunk) plus the statics' real collision triangles, nearest chunks first, and streams each as a `CollisionChunk` through the ring ([PROTOCOL.md](PROTOCOL.md#collision-chunks)). Chunks are tagged with the handover they belong to.
- **Floating origin** (`protocol/cpp/ground.h`, game-free and tested): a handover puts the base at the Skyrim player's feet, which sits at San Andreas (0, 0, 500 m), high above San Andreas's empty map and its sea. Skyrim position `p` is at `(p - base) / 70 + (0, 0, 500)` metres. Once CJ is 1 km out, or 300 m above or below 500 m, the base moves under him by whole metres, and CJ, the chunks, the camera and the remembered spots all move back by the same shift. Skyrim never notices: poses stay offsets from the handover spot.
- **San Andreas side** (`sa/src/chunk_world.cpp`): each chunk becomes one invisible building positioned at the chunk's middle (its median height), its vertices within 256 m of it (San Andreas packs them as int16 at 1/128 m; triangles out of reach are dropped and counted). San Andreas faces a triangle the other way round from Skyrim, so two corners swap. A resend replaces the chunk in its slot; chunks more than 6 from CJ go, which is always past the 3 at which Skyrim forgets them (and resends them when the player comes back). Chunks for a handover San Andreas hasn't read yet wait until it has.
- **Waiting for the ground**: after a handover CJ is held at the handover spot until the chunk under it is in, then let go.
- **Fall rescue** (`protocol/cpp/fall_rescue.h`, PerkeleRim's rule): with no surface under his feet but one above for a quarter second (he slipped through the land or into a rock), CJ goes back where he last stood if that was within 25 m, else onto a surface at most 1 m over his feet, never a ceiling. With nothing above or below at all (a hole), he goes back where he last stood.

### Running San Andreas hidden

San Andreas runs windowed and minimized, started by `scripts/launch-sa.ps1` and driven from Skyrim. AhShitRim does this in its own ASI rather than with a windowed-mode mod, to keep San Andreas as close to headless as it gets:

- **Window** (`sa/src/window.cpp`): RenderWare opens its windowed video mode, and every Direct3D device is created windowed and without vsync. The window is minimized and never activated (`launch-sa.ps1 -Visible` shows it in a small 800x600 window instead, still without taking the focus, to watch CJ). Losing the focus never reaches San Andreas, which would otherwise pause its loop and sounds and open its menu, and it can't move the cursor.
- **Straight into a game** (`sa/src/runner.cpp`): the splashes, movies and main menu are skipped and a new game loads (about a second, with no map). The mission script never runs; CJ is created where it would have created him.
- **Empty world**: map collision and IPLs are never streamed in, and every building, dummy and object loaded at start is taken out of the world. No peds, traffic, parked cars, trains, planes or wanted level. CJ stands on Skyrim's ground, built around him (see Ground).
- **Frame rate**: physics stays capped at 30 fps, since higher frame rates break it. While hidden nothing is rendered (San Andreas's `Idle` runs the simulation and returns before drawing), so the only cost is the simulation. Peds' bones are only posed while rendering; CJ's model in Skyrim, which needs his skeleton, will have to pose them itself. Sounds are muted.
- **Hooks**: functions that only need to be disabled are hooked at their start; the per-frame ones (`Idle`, `FrontendIdle`, `CPad::UpdatePads`) are hooked at their call sites once the window shows, after every other plugin has patched the game, because SilentPatch patches a call inside `Idle`'s first bytes.

F10 in Skyrim hands the player to San Andreas, as in PerkeleRim: Skyrim forwards WASD, mouse look, sprint, jump, crouch and walk (see [PROTOCOL.md](PROTOCOL.md#latest-value-slots)), forces its first-person camera and moves its player by CJ's offset from the handover spot every frame, looking where San Andreas's camera looks. Escape, Tab, M and the console stay Skyrim's; a pausing menu pauses San Andreas too. The handover ends on F10, when the link goes down, the player dies or the game moves the player (load door, fast travel), or when a game loads.

## 4. Milestone 1: on the ground

| Step | What |
|---|---|
| Scaffold (done) | Both plugins build and load; shared memory handshake, protocol v1 |
| Hidden runner (done) | San Andreas started hidden, driven from Skyrim with F10, CJ on a test plane (protocol v3) |
| Ground (built, in-game check pending) | Skyrim's collision built in San Andreas around CJ as spawned collision objects, floating origin, fall rescue (protocol v4) |
| CJ | CJ drawn in Skyrim from San Andreas's own model |
| Car | The Greenwood driven on Skyrim's roads |

## 5. Repo layout

| Path | What |
|---|---|
| `skse/` | SKSE plugin; `extern/CommonLibSSE-NG` submodule |
| `sa/` | ASI plugin; `extern/plugin-sdk` submodule |
| `protocol/` | Shared protocol header, C++ link code and its game-free tests |
| `scripts/` | `build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1`, `launch-sa.ps1`, `restart-games.ps1`, `fake-skyrim.ps1` |
| `docs/` | This document, [PROTOCOL.md](PROTOCOL.md), [DEV_SETUP.md](DEV_SETUP.md), [ROADMAP.md](ROADMAP.md) |

## 6. Risks

- **Collision at runtime**: spawned collision objects carry CJ on foot on flat test ground (`scripts/fake-skyrim.ps1`); Skyrim's real terrain and statics, and cars on them, still have to prove out in game.
- **San Andreas updates** through Steam undo the downgrade. Keep the downgraded copy outside Steam, or run the downgrader again.
- **Two engines in one session**: both games must run at once; San Andreas is light, but its frame pacing affects physics.
