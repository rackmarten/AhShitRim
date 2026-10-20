# AhShitRim: design

> Status: milestone 1 done. San Andreas runs hidden as a simulator of CJ, driven from Skyrim with F10, on Skyrim's own terrain and statics, built around him as collision objects (protocol v4). Skyrim draws CJ from San Andreas's own model, animated by San Andreas, through San Andreas's camera (protocol v5). CJ spawns, drives and rides San Andreas's vehicles there, which Skyrim draws from their own models too (protocol v6), checked in game from Whiterun to Riverwood and past two floating origin moves.

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
- The pools weren't the limit: San Andreas keeps at most 225 chunks (15 x 15), and its building pool has room for them next to the 9,200 map buildings the empty world takes out of the world. The limit that did bite is the store of atomic model infos, which is full (`CModelInfo::AddAtomicModel` crashed), so each chunk slot borrows the model info of one of the map's own atomic models instead, unused once the map is emptied.
- Hooking the queries would have meant reimplementing San Andreas's sphere, box and line tests against a foreign mesh across a dozen functions, for no gain over handing it collision it already understands.

How it works:

- **Skyrim side** (`skse/src/collision/`, ported from PerkeleRim): while San Andreas has the player, the collision sampler reads the land as a heightfield (33 x 33 Havok raycasts per 2048-unit chunk) plus the statics' real collision triangles, nearest chunks first, and streams each as a `CollisionChunk` through the ring ([PROTOCOL.md](PROTOCOL.md#collision-chunks)). Chunks are tagged with the handover they belong to.
- **Floating origin** (`protocol/cpp/ground.h`, game-free and tested): a handover puts the base at the Skyrim player's feet, which sits at San Andreas (0, 0, 500 m), high above San Andreas's empty map and its sea. Skyrim position `p` is at `(p - base) / 70 + (0, 0, 500)` metres. Once CJ is 1 km out, or 300 m above or below 500 m, the base moves under him by whole metres, and CJ, the chunks, the camera and the remembered spots all move back by the same shift. Skyrim never notices: poses stay offsets from the handover spot.
- **San Andreas side** (`sa/src/chunk_world.cpp`): each chunk becomes one invisible building positioned at the chunk's middle (its median height), its vertices within 256 m of it (San Andreas packs them as int16 at 1/128 m; triangles out of reach are dropped and counted). San Andreas faces a triangle the other way round from Skyrim, so two corners swap. A resend replaces the chunk in its slot; chunks more than 7 from CJ go, which is always past the 3 at which Skyrim forgets them around the player and a point up to 3 chunks ahead (and resends them when the player comes back). Every triangle is tarmac: chunks carry no material yet. Chunks for a handover San Andreas hasn't read yet wait until it has.
- **Waiting for the ground**: after a handover CJ is held at the handover spot until the chunk under it is in, then let go.
- **Fall rescue** (`protocol/cpp/fall_rescue.h`, PerkeleRim's rule): with no surface under his feet but one above for a quarter second (he slipped through the land or into a rock), CJ goes back where he last stood if that was within 25 m, else onto a surface at most 1 m over his feet, never a ceiling. With nothing above or below at all (a hole), he goes back where he last stood. In a vehicle the vehicle is watched instead, by its middle (the bottom of its collision box hangs below its tyres), standing while a wheel touches.
- **Fast vehicles**: Skyrim samples around a second point up to 3 chunks ahead of the player (2 s of travel), with twice the time per frame above 10 m/s, so the road ahead of a car at 50 m/s is in before it gets there. Skyrim itself may still be loading the land out there, so a chunk that came in incomplete is sampled again after 0.4 s at speed instead of 2 s.

### Running San Andreas hidden

San Andreas runs windowed and minimized, started by `scripts/launch-sa.ps1` and driven from Skyrim. AhShitRim does this in its own ASI rather than with a windowed-mode mod, to keep San Andreas as close to headless as it gets:

- **Window** (`sa/src/window.cpp`): RenderWare opens its windowed video mode, and every Direct3D device is created windowed and without vsync. The window is minimized and never activated (`launch-sa.ps1 -Visible` shows it in a small 800x600 window instead, still without taking the focus, to watch CJ). Losing the focus never reaches San Andreas, which would otherwise pause its loop and sounds and open its menu, and it can't move the cursor.
- **Straight into a game** (`sa/src/runner.cpp`): the splashes, movies and main menu are skipped and a new game loads (about a second, with no map). The mission script never runs; CJ is created where it would have created him, and dressed as its intro dresses him (vest, jeans, sneakers).
- **Empty world**: map collision and IPLs are never streamed in, and every building, dummy and object loaded at start is taken out of the world. No peds, traffic, parked cars, trains, planes or wanted level. CJ stands on Skyrim's ground, built around him (see Ground).
- **Frame rate**: physics stays capped at 30 fps, since higher frame rates break it. While hidden nothing is rendered (San Andreas's `Idle` runs the simulation and returns before drawing), so the only cost is the simulation. Peds' bones are only posed while rendering; CJ's model in Skyrim, which needs his skeleton, will have to pose them itself. Sounds are muted.
- **Hooks**: functions that only need to be disabled are hooked at their start; the per-frame ones (`Idle`, `FrontendIdle`, `CPad::UpdatePads`) are hooked at their call sites once the window shows, after every other plugin has patched the game, because SilentPatch patches a call inside `Idle`'s first bytes.

F10 in Skyrim hands the player to San Andreas, as in PerkeleRim: Skyrim forwards WASD, mouse look, sprint, jump, crouch, walk, the enter key and the horn, and the vehicle spawn keys (see [PROTOCOL.md](PROTOCOL.md#latest-value-slots) and the README's keys), forces its first-person camera state and moves its player by CJ's offset from the handover spot every frame, looking where San Andreas's camera looks. Once CJ's model is in, Skyrim draws him instead of its player and views him through San Andreas's camera (see CJ in Skyrim). Escape, Tab, M and the console stay Skyrim's; a pausing menu pauses San Andreas too. Skyrim's sneak eye is hidden every frame CJ doesn't crouch, since Skyrim kept it up after a crouch (as in PerkeleRim). The handover ends on F10, when the link goes down, the player dies or the game moves the player (load door, fast travel), or when a game loads.

### CJ in Skyrim

CJ's model and animation come from the running San Andreas, like PerkeleRim's models from MSC: nothing is converted, shipped or written to disk.

- **The model** (`sa/src/cj_body.cpp`): CJ's clump, as `CClothes::RebuildPlayer` built it, is one skinned atomic. Its geometry (vertices, normals, UVs, triangles per material), its skin (four bone indices and weights per vertex) and its textures go to Skyrim as one blob in `ModelChunk` messages over the ring back ([PROTOCOL.md](PROTOCOL.md#cjs-model)), whenever the link comes up and whenever the geometry changes. Textures are read from their Direct3D 9 textures (the raster's Direct3D part) and decoded to RGBA (`protocol/cpp/texture_decode.h`: DXT1/3/5 and the plain formats). Meshes are split per material and put in Skyrim units (`protocol/cpp/skin_build.h`); a triangle facing against its vertices' normals is turned round.
- **The skeleton**: San Andreas only updates a ped's bones in full when it is on screen, and while hidden nothing is. A `CEntity::UpdateAnim` hook runs CJ's animations as if he were on screen, and after each frame the ASI poses his hierarchy (`UpdateRpHAnim`, then `ShoulderBoneRotation`, which sets the breast bones between clavicle and upper arm and otherwise only runs while drawing; without it the shoulders stretched back) and turns each bone into the transform RenderWare's skin pipeline would use (skin-to-bone, then the bone, then the atomic's frame for a local-space hierarchy; `protocol/cpp/body.h`), relative to his feet, in Skyrim units. With San Andreas's active camera (position, direction, field of view) it goes into the body slot every frame.
- **Drawing** (`skse/src/body/ModelView.cpp`, which draws the vehicles too): the model is built as PerkeleRim's ModelView builds MSC's (BSTriShapes cloned from a vanilla shape, hand-made textures and shaders), but each shape's vertex buffer is dynamic: every Skyrim frame each vertex is skinned on the CPU by its bones (about 1,700 vertices) and written into it. Skyrim's own skinning (`NiSkinInstance`, `NiSkinPartition`) isn't used; it would have to be put together by hand and is far more likely to crash than a rigid shape whose buffer changes. CJ is drawn two-sided (a closed mesh, so it costs nothing visible and the winding can't turn him inside out), lit and taking shadows but casting none yet. He hangs in the node the player's own 3D hangs in, at the handover spot plus his feet; the player's own 3D is hidden meanwhile.
- **Camera**: Skyrim's camera stays in its first-person state (which only draws the player's hidden arms) and its camera node is put where San Andreas's camera is, looking where it looks, with San Andreas's field of view (both are horizontal for a 4:3 screen), as PerkeleRim does for MSC's chase camera. First person isn't supported yet.
- **Smoothing** (`Body::Interpolator`): San Andreas runs at 30 frames a second and Skyrim faster, so Skyrim plays CJ's frames back 50 ms behind, blending bones (normalized quaternion lerp), feet and camera between the two frames around that time. Frames carry San Andreas's high-resolution clock; the gap to Skyrim's clock is the smallest seen, allowed to grow slowly. A new handover starts over.

### Vehicles

San Andreas's own vehicles, physics and all, on Skyrim's ground; Skyrim draws them from San Andreas's own models, the same way as CJ.

- **Spawning** (`sa/src/vehicles.cpp`, the spot rule in `protocol/cpp/vehicle.h`): ], [ and `\` in Skyrim ask for a Sandking, a ZR-350 or a Sanchez by model id, so any other vehicle costs a key (or, later, a cheat code). San Andreas parks it across CJ's path, its side to him, as its own vehicle cheat does, but on Skyrim's ground, which is rarely flat: 15 spots are tried in turn (three rings out from 2 m in front of him, straight ahead, then turned 30 and 60 degrees either way), and the first whose ground is all there under a 3 x 3 grid over the vehicle's footprint, within 0.8 m from highest to lowest, at most 1.5 m from CJ's feet, and with nothing in the way (from CJ to it, and across it) wins. It is created as a mission vehicle (San Andreas never takes those away), unlocked, a bike on its side stand, and put on its wheels (`PlaceOnRoadProperly`). Cars, monster trucks, quads, bikes and bicycles can spawn; boats, aircraft and trains come later. At most four are kept: another removes the oldest one CJ isn't in. A new handover takes CJ out of his (`FlushImmediately`, as `WARP_CHAR_FROM_CAR_TO_COORD` does) and removes them all; Skyrim hides them while it has the player.
- **Driving**: San Andreas's own enter and exit tasks, animations and vehicle physics. In a vehicle the keys take San Andreas's vehicle layout on its pad (protocol/cpp/input.h), CJ is seated every frame where his vehicle is (`SetPedPositionInCar`, which also follows a bike's lean), and San Andreas's own rules apply, a rider thrown off a bike on a hard hit among them. The chase camera is San Andreas's own, copied into Skyrim like on foot.
- **The model**: every drawn part (atomic) of the vehicle is a bone, its vertices hung on it alone, in the vehicle's own colours (San Andreas swaps the paint and light colours into its materials right before drawing a vehicle, so AhShitRim does the same when reading them). A part's frame may carry a scale or a mirror (wheels), which is baked into its vertices once. Parts that look the same are merged into one mesh, so a vehicle is a few dozen shapes.
- **The pose**: nothing draws a hidden vehicle, and drawing is what turns its wheels: `PreRender` spins, steers and drops the wheels onto their suspension, turns a bike's forks and handlebars, and leans a bike (into its parts, under the upright root: the lean put on the root as well leaned it twice as far as CJ). So each frame San Andreas puts every vehicle's matrix into its clump, runs its `PreRender`, works out a bike's lean matrix afresh for seating CJ, and sends every part's frame in the vehicle's slot. A parked vehicle's parts don't move, and Skyrim skins it again only when they do.
- **Floating origin**: the vehicles move back with CJ and the chunks when the origin moves, and with them the points where their wheels last touched the ground, which San Andreas keeps in world space for the next physics step (left behind, they stopped a car at 49 m/s dead and rolled it). `scripts/fake-skyrim.ps1` drives a Sandking 1.3 km, through an origin move, on ground streamed around it.

## 4. Milestone 1: on the ground

| Step | What |
|---|---|
| Scaffold (done) | Both plugins build and load; shared memory handshake, protocol v1 |
| Hidden runner (done) | San Andreas started hidden, driven from Skyrim with F10, CJ on a test plane (protocol v3) |
| Ground (done) | Skyrim's collision built in San Andreas around CJ as spawned collision objects, floating origin, fall rescue (protocol v4) |
| CJ (done) | CJ drawn in Skyrim from San Andreas's own model, animated, through San Andreas's camera (protocol v5) |
| Vehicles (done) | A Sandking, a ZR-350 and a Sanchez spawned, driven and ridden on Skyrim's roads, drawn from their own models (protocol v6) |

## 5. Repo layout

| Path | What |
|---|---|
| `skse/` | SKSE plugin; `extern/CommonLibSSE-NG` submodule |
| `sa/` | ASI plugin; `extern/plugin-sdk` submodule |
| `protocol/` | Shared protocol header, C++ link code and its game-free tests |
| `scripts/` | `build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1`, `launch-sa.ps1`, `restart-games.ps1`, `fake-skyrim.ps1` |
| `docs/` | This document, [PROTOCOL.md](PROTOCOL.md), [DEV_SETUP.md](DEV_SETUP.md), [ROADMAP.md](ROADMAP.md) |

## 6. Risks

- **Collision at runtime**: spawned collision objects carry CJ on Skyrim's real terrain and statics, and the vehicles at full speed from Whiterun to Riverwood and past two floating origin moves (both checked in game); `scripts/fake-skyrim.ps1` covers the same on flat test ground. Skyrim's land ahead of a very fast vehicle can still be loading when it is sampled (retried quickly at speed).
- **San Andreas updates** through Steam undo the downgrade. Keep the downgraded copy outside Steam, or run the downgrader again.
- **Two engines in one session**: both games must run at once; San Andreas is light, but its frame pacing affects physics.
