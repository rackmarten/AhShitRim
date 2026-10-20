# AhShitRim: design

> Status: milestone 1 done. San Andreas runs hidden as a simulator of CJ, driven from Skyrim with F10, on Skyrim's own terrain and statics, built around him as collision objects (protocol v4). Skyrim draws CJ from San Andreas's own model, animated by San Andreas, through San Andreas's camera (protocol v5). CJ spawns, drives and rides San Andreas's vehicles there, which Skyrim draws from their own models too (protocol v6), checked in game from Whiterun to Riverwood and past two floating origin moves. While CJ has the player, Skyrim draws San Andreas's HUD and text styles in San Andreas's own fonts, and San Andreas plays its own sound and radio at Skyrim's volumes, with Radio Sovngarde on Skyrim's own music (protocol v7). E activates what CJ faces through Skyrim, and he stays the player through load doors, interiors and fast travel, re-anchored where the game puts the player (protocol v8), and Sweetroll FM on CC0 music that ships with AhShitRim (protocol v9).

AhShitRim runs **GTA: San Andreas** and **Skyrim Anniversary Edition** at the same time. Skyrim owns the world and draws everything; San Andreas runs hidden and simulates CJ, his cars, his guns and his cheat codes.

It's a sibling of [PerkeleRim](https://github.com/rackmarten/PerkeleRim) (My Summer Car in Skyrim) and is built the same way. Where this document is silent, PerkeleRim's `docs/DESIGN.md` describes the shared approach.

## 1. Goals

- CJ on foot and in San Andreas's vehicles, with San Andreas's own movement, animations and vehicle physics, on Skyrim's terrain and statics.
- Skyrim keeps working as Skyrim: its own player actor stays the real player, so dialogue, quests, bounties and kill credit keep working. CJ is the body the player drives.
- No assets from either game in the repository or a release. Models, textures and sounds are read from the player's own installs at play time. The only audio that ships is Sweetroll FM's CC0 music (`music/CREDITS.md`).

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
- **Frame rate**: physics stays capped at 30 fps, since higher frame rates break it. While hidden nothing is rendered (San Andreas's `Idle` runs the simulation and returns before drawing), so the only cost is the simulation. Peds' bones are only posed while rendering; CJ's model in Skyrim, which needs his skeleton, will have to pose them itself. Sounds are muted except while CJ has the player (see Sound and the radio).
- **Hooks**: functions that only need to be disabled are hooked at their start; the per-frame ones (`Idle`, `FrontendIdle`, `CPad::UpdatePads`) are hooked at their call sites once the window shows, after every other plugin has patched the game, because SilentPatch patches a call inside `Idle`'s first bytes.

F10 in Skyrim hands the player to San Andreas, as in PerkeleRim: Skyrim forwards WASD, mouse look, sprint, jump, crouch, walk, the enter key, the handbrake and the horn, and the vehicle spawn keys (see [PROTOCOL.md](PROTOCOL.md#latest-value-slots) and the README's keys), as actions in the modern layout (Shift sprints, Space jumps) or San Andreas's classic PC one (Space sprints, Shift jumps; `[Input] KeyLayout` in `AhShitRim.ini`, mapped in `protocol/cpp/input.h`), forces its first-person camera state and moves its player by CJ's offset from the handover spot every frame, looking where San Andreas's camera looks. Once CJ's model is in, Skyrim draws him instead of its player and views him through San Andreas's camera (see CJ in Skyrim). Escape, Tab, M and the console stay Skyrim's; a pausing menu pauses San Andreas too. Skyrim's sneak eye is hidden every frame CJ doesn't crouch, since Skyrim kept it up after a crouch (as in PerkeleRim). E activates what CJ faces through Skyrim (see Activating things). The handover ends on F10, when the link goes down or the player dies, or when a game loads; when the game moves the player it goes on, re-anchored (see Load doors, interiors and fast travel).

### Load doors, interiors and fast travel

The game moves the player while CJ has it through a load door, by fast travel or from a script. CJ goes along: the handover goes on with a new anchor where the game put the player.

- **Noticing it** (`protocol/cpp/anchor.h`, game-free and tested): Skyrim says so itself, with a `PositionPlayerEvent` (its `kFinish`) whenever it positions the player for a load door, fast travel, `coc` or a script, and the next frame with a cell re-anchors. Comparing cells alone missed Whiterun's gate out to Tamriel in game: its two sides stand 272 units apart. As a fallback, every frame Skyrim also compares the player's cell and position with where it put the player for CJ the frame before (moving the player for CJ never counts as Skyrim positioning it). A new cell with an interior on either side, or in another worldspace (a city's gate, Tamriel to Whiterun, moves the player only a few metres), is a load door; a jump of over 512 units from where Skyrim put the player for CJ the frame before is fast travel, a script or a load door within one cell (CJ's own movement doesn't count, since the player is put where he is every frame; only Skyrim easing the player out of a wall does, by well under a metre); no cell yet is a loading screen, which waits. Walking into the next exterior cell is nothing. A loading screen is a pausing menu, so San Andreas is frozen meanwhile.
- **Re-anchoring**: Skyrim counts `InputState.handover` up and sends the player's new feet and heading as the anchor, then starts the collision sampler over for that handover, so chunks of the old place are dropped as stale wherever they still are. San Andreas does what it does for every new handover: CJ comes out of his vehicle and every vehicle goes (fast travel in a car leaves the car behind), the old ground is taken out, the floating origin's base is reset exactly to the new anchor (whatever origin moves came before), and CJ is put at San Andreas (0, 0, 500 m) facing the new heading and held there until the chunk under him is in. What San Andreas keeps on CJ himself (his crouch, later his weapons and health) stays. Skyrim keeps its player hidden until CJ is drawn at the new place, a frame or two later.
- **Interiors**: the collision sampler sends an interior's chunks as its statics only (no land, no LOD), complete once every static's 3D is loaded. The chunk grid and the floating origin work the same on an interior's own coordinates, often thousands of units below anything outside.
- `scripts/fake-skyrim.ps1` re-anchors CJ out of a ZR-350 to a spot 60,000 units off and 10,000 below, checks the vehicles go and CJ is held at the new anchor until its ground arrives, then walks him on it.

### Activating things

Skyrim's own activate key refuses while CJ has the player, as all its player controls do, and E activates instead (`skse/src/player/Activation.cpp`, the game-free half in `protocol/cpp/activate.h`).

- **The pick**: a ray from CJ's chest (100 units over his feet, where Skyrim puts the player) along San Andreas's camera direction, as far as Skyrim's `fActivatePickLength` (180 units). Every reference within reach of the player that Skyrim's activate key acts on (doors, activators, containers, people, flora and everything that can be picked up; furniture stays out, since sitting down isn't CJ's) is a box: its base object's bounds on its 3D's world transform, a standing box for a living person (their bounds are often empty), or the 3D's bounding sphere for a body on the ground or empty bounds. The nearest box the ray enters wins, if a line of sight ray (Havok, the LOS layer, through the player's own body) meets nothing nearer than it, and it has a name. Skyrim's crosshair shows nothing for a nameless reference, and those (trigger boxes and helpers whose activate text is just "Activate") were often big enough that CJ stood inside them and they won whichever way he looked.
- **The prompt**: Skyrim's own activate text for it (`GetActivateText`, "Open\nBreezehome"), font tags dropped and lines joined, shows in San Andreas's help box top left while CJ faces it ("E: Open Breezehome").
- **Activating**: E calls Skyrim's `ActivateRef` with the player as the activator, so locks, crime, ownership, quest scripts, dialogue, containers and load doors all work as for Skyrim's own key. In a vehicle nothing is picked: load doors only open on foot.

### CJ in Skyrim

CJ's model and animation come from the running San Andreas, like PerkeleRim's models from MSC: nothing is converted, shipped or written to disk.

- **The model** (`sa/src/cj_body.cpp`): CJ's clump, as `CClothes::RebuildPlayer` built it, is one skinned atomic. Its geometry (vertices, normals, UVs, triangles per material), its skin (four bone indices and weights per vertex) and its textures go to Skyrim as one blob in `ModelChunk` messages over the ring back ([PROTOCOL.md](PROTOCOL.md#cjs-model)), whenever the link comes up and whenever the geometry changes. Textures are read from their Direct3D 9 textures (the raster's Direct3D part) and decoded to RGBA (`protocol/cpp/texture_decode.h`: DXT1/3/5 and the plain formats). Meshes are split per material and put in Skyrim units (`protocol/cpp/skin_build.h`); a triangle facing against its vertices' normals is turned round.
- **The skeleton**: San Andreas only updates a ped's bones in full when it is on screen, and while hidden nothing is. A `CEntity::UpdateAnim` hook runs CJ's animations as if he were on screen, and after each frame the ASI poses his hierarchy (`UpdateRpHAnim`, then `ShoulderBoneRotation`, which sets the breast bones between clavicle and upper arm and otherwise only runs while drawing; without it the shoulders stretched back) and turns each bone into the transform RenderWare's skin pipeline would use (skin-to-bone, then the bone, then the atomic's frame for a local-space hierarchy; `protocol/cpp/body.h`), relative to his feet, in Skyrim units. With San Andreas's active camera (position, direction, field of view) it goes into the body slot every frame.
- **Drawing** (`skse/src/body/ModelView.cpp`, which draws the vehicles too): the model is built as PerkeleRim's ModelView builds MSC's (BSTriShapes cloned from a vanilla shape, hand-made textures and shaders), but each shape's vertex buffer is dynamic: every Skyrim frame each vertex is skinned on the CPU by its bones (about 1,700 vertices) and written into it. Skyrim's own skinning (`NiSkinInstance`, `NiSkinPartition`) isn't used; it would have to be put together by hand and is far more likely to crash than a rigid shape whose buffer changes. CJ is drawn two-sided (a closed mesh, so it costs nothing visible and the winding can't turn him inside out), lit and taking shadows but casting none yet. He hangs in the node the player's own 3D hangs in, at the handover spot plus his feet; the player's own 3D is hidden meanwhile.
- **Camera**: Skyrim's camera stays in its first-person state (which only draws the player's hidden arms) and its camera node is put where San Andreas's camera is, looking where it looks, with San Andreas's field of view (both are horizontal for a 4:3 screen), as PerkeleRim does for MSC's chase camera. First person isn't supported yet.
- **Smoothing** (`Body::Interpolator`): San Andreas runs at 30 frames a second and Skyrim faster, so Skyrim plays CJ's frames back 50 ms behind, blending bones (normalized quaternion lerp), feet and camera between the two frames around that time. Frames carry San Andreas's high-resolution clock; the gap to Skyrim's clock is the smallest seen, allowed to grow slowly. A new handover starts over.

### Vehicles

San Andreas's own vehicles, physics and all, on Skyrim's ground; Skyrim draws them from San Andreas's own models, the same way as CJ.

- **Spawning** (`sa/src/vehicles.cpp`, the spot rule in `protocol/cpp/vehicle.h`): ], [ and `\` in Skyrim ask for a Sandking, a ZR-350 or a Sanchez by model id, so any other vehicle costs a key (or, later, a cheat code). San Andreas parks it across CJ's path, its side to him, as its own vehicle cheat does, but on Skyrim's ground, which is rarely flat: 15 spots are tried in turn (three rings out from 2 m in front of him, straight ahead, then turned 30 and 60 degrees either way), and the first whose ground is all there under a 3 x 3 grid over the vehicle's footprint, within 0.8 m from highest to lowest, at most 1.5 m from CJ's feet, and with nothing in the way (from CJ to it, and across it) wins. It is created as a mission vehicle (San Andreas never takes those away), unlocked, a bike on its side stand, and put on its wheels (`PlaceOnRoadProperly`). Cars, monster trucks, quads, bikes and bicycles can spawn; boats, aircraft and trains come later. At most four are kept: another removes the oldest one CJ isn't in. A new handover or a re-anchor takes CJ out of his (`FlushImmediately`, as `WARP_CHAR_FROM_CAR_TO_COORD` does) and removes them all; Skyrim hides them while it has the player.
- **Driving**: San Andreas's own enter and exit tasks, animations and vehicle physics. In a vehicle the keys take San Andreas's vehicle layout on its pad (protocol/cpp/input.h), CJ is seated every frame where his vehicle is (`SetPedPositionInCar`, which also follows a bike's lean), and San Andreas's own rules apply, a rider thrown off a bike on a hard hit among them. The chase camera is San Andreas's own, copied into Skyrim like on foot.
- **The model**: every drawn part (atomic) of the vehicle is a bone, its vertices hung on it alone, in the vehicle's own colours (San Andreas swaps the paint and light colours into its materials right before drawing a vehicle, so AhShitRim does the same when reading them). A part's frame may carry a scale or a mirror (wheels), which is baked into its vertices once. Parts that look the same are merged into one mesh, so a vehicle is a few dozen shapes.
- **The pose**: nothing draws a hidden vehicle, and drawing is what turns its wheels: `PreRender` spins, steers and drops the wheels onto their suspension, turns a bike's forks and handlebars, and leans a bike (into its parts, under the upright root: the lean put on the root as well leaned it twice as far as CJ). So each frame San Andreas puts every vehicle's matrix into its clump, runs its `PreRender`, works out a bike's lean matrix afresh for seating CJ, and sends every part's frame in the vehicle's slot. A parked vehicle's parts don't move, and Skyrim skins it again only when they do.
- **Floating origin**: the vehicles move back with CJ and the chunks when the origin moves, and with them the points where their wheels last touched the ground, which San Andreas keeps in world space for the next physics step (left behind, they stopped a car at 49 m/s dead and rolled it). `scripts/fake-skyrim.ps1` drives a Sandking 1.3 km, through an origin move, on ground streamed around it.

### San Andreas's HUD

While CJ has the player, Skyrim draws San Andreas's HUD and text over its own frame, in San Andreas's own fonts and sprites.

- **Files** (`skse/src/hud/SaHud.cpp`): at startup the SKSE plugin reads `data/fonts.dat`, `models/fonts.txd` and `models/hud.txd` from the San Andreas install named in `Data/SKSE/Plugins/AhShitRim.ini` (`[SanAndreas] Path=`, written by `scripts/deploy-local.ps1` from `SaPath`), into memory only. The texture dictionaries are read by `protocol/cpp/txd.h` (RenderWare chunks, Direct3D 8 and 9 native textures, decoded by `texture_decode.h`); without them the HUD stays off and the log says why.
- **Renderer** (`skse/src/hud/Overlay.cpp`): a hook on Skyrim's swap chain `Present` draws a list of textured quads with DirectXTK's `SpriteBatch` (already linked through CommonLibSSE-NG), saving and restoring the Direct3D state it touches, since Skyrim caches its own. PerkeleRim has no overlay (its HUD is a layer in Skyrim's Scaleform HUD movie), and Scaleform can't take bitmap fonts from memory, so this is new here. The main thread builds the quads each frame; Present draws the latest, and nothing once they are a quarter second old, so a pausing menu doesn't keep a stale HUD up.
- **Fonts** (`protocol/cpp/hud_font.h`): San Andreas's `CFont` rules from gta-reversed: two 512 x 512 textures of 16 x 12.8 cells (Gothic and Menu in `font2`, Subtitles and Pricedown in `font1`, Menu and Pricedown as subfonts further down), letter widths from `fonts.dat` (proportional or fixed, widened by the outline), 32 x 20 font units per letter, 18 per line, `~n~` line breaks, word wrapping, and outlines drawn as eight black copies one edge out.
- **Layout** (`protocol/cpp/hud_layout.h`): San Andreas lays its HUD out on a 640 x 448 screen stretched over 4:3. Here the units are those of a 4:3 screen as high as Skyrim's (square pixels), and each piece is held to the edge San Andreas holds it to, so 16:9, 16:10 and 21:9 show the HUD unstretched in the corners. Top right as `CHud::DrawPlayerInfo`: the weapon icon (the fist until weapons exist), the clock (Skyrim's game time), the armour bar (hidden at 0, as nothing gives CJ armour yet), the health bar (the Skyrim player's health over its maximum, flashing under a tenth) and the money (the player's gold, green, or red when negative), then the wanted stars, shown once there is a wanted level.
- **Text styles**: the mission title (bottom right in gold Pricedown, as San Andreas puts it), big messages in the middle ("wasted" and "busted" in grey Gothic, "mission passed!" in gold Pricedown, over the fade), the help box (top left, dark box), subtitles (bottom centre, lines going up), fade to black and letterbox bars sliding in. `SaHud.h` is the interface later increments use; `/` (dev key) shows each style in turn.
- **Skyrim's own HUD**: its compass, attribute bars and crosshair (clips of its HUD movie) are hidden every frame CJ has the player, as Skyrim's HUD modes show them again, and put back as they were on F10.

### Sound and the radio

San Andreas plays its own sounds, with its own audio engine; Skyrim only sets how loud.

- **Unmuted while handed over** (`sa/src/audio.cpp`): San Andreas's audio keeps running hidden (`CAudioEngine::Service` runs in `Idle` before its render gate), and every sound buffer it makes already has DirectSound's global focus, so it plays while Skyrim has the focus. Its effects and music master volumes (0..64) are set every frame from the audio slot: Skyrim's master x effects and master x music sliders (`skse/src/audio/SaAudio.cpp`; the effects category is Skyrim.esm's `000172A1`, music comes from the default objects, master is the `fAudioMasterVolume` preference, as it has no category), and 0 while Skyrim has the player. Sounds are positioned by San Andreas's listener, its camera, which Skyrim's camera follows. A pausing Skyrim menu pauses every channel (`CAudioEngine::PauseAllSounds`), on top of San Andreas's user pause.
- **No world ambience**: `CAEWeatherAudioEntity::Service` (city noise, wind, rain), `CAEAmbienceTrackManager::Service` (area ambience tracks), `CAEPoliceScannerAudioEntity::Service` and `CAEGlobalWeaponAudioEntity::ServiceAmbientGunFire` do nothing. CJ's, his vehicles' and his weapons' sounds and his speech are untouched.
- **The radio**: San Andreas starts a vehicle's own station when CJ gets in, as usual. Skyrim's mouse wheel counts steps in the input (`radioNext`, `radioPrev`); in a vehicle San Andreas turns them into `CAudioEngine::RetuneRadio` along the dial 1..14 (`protocol/cpp/radio.h`), and reports the station in CJ's pose, the one the wheel is going to while it retunes (at most 3 s). San Andreas draws the station's name only while rendering, so Skyrim draws it (`SaHud::ShowRadioStation`, `CAERadioTrackManager::DisplayRadioStationName`'s style: Menu font, top centre, gold, dark grey while retuning). Skyrim's own music pauses while any station plays: every frame its current music type gets a `BSMusicEvent` pause (Skyrim moves on to combat or a dungeon's music on its own), and the one paused goes on after. Attenuating the music category didn't reach the tracks.
- **Radio Sovngarde**: the dial's 12 and 13 are the stations Skyrim plays. San Andreas tunes its radio off (its `eRadioID` 13; its User Track Player turns into off too when the player has no tracks) and reports the dial's value in the pose. On 12, Radio Sovngarde, Skyrim plays its own music by path from its archives (`BSAudioManager::GetSoundHandleByFile`, nothing copied): the main theme first, then by mood, combat tracks while the player is in combat and the time of day's exploration tracks otherwise, with one of the 13 bard instrumentals after every two (a mood change cuts the track short with a fade). The bards' sung songs are voice files split by verse, which can't be played from a path.
- **Sweetroll FM**: on 13 Skyrim plays AhShitRim's own CC0 tracks (`music/CREDITS.md`, picked from pages that state CC0) the same way, from loose files in `Data\Music\AhShitRim\SweetrollFM\` (`kSweetrollTracks` in `radio.h`). No mood: a shuffle plays every track once before any repeats and never one twice in a row, starting on a random one when tuned in. At data load Skyrim checks which tracks are installed and plays only those; with none (a dev build before `scripts/encode-music.ps1` ran) tuning in shows the name, stays silent and logs it once. `scripts/deploy-local.ps1` copies `music/sweetrollfm/*.xwm` and the credits there, and the release package ships them in the same place. Skyrim plays only `.wav` and `.xwm`, so the script encodes the sources (mp3, ogg) with ffmpeg and the DirectX SDK's `xWMAEncode`.

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
| `scripts/` | `build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1`, `launch-sa.ps1`, `restart-games.ps1`, `fake-skyrim.ps1`, `encode-music.ps1` |
| `music/` | `CREDITS.md` (Sweetroll FM's CC0 tracks and their sources), `sweetrollfm/*.xwm` once encoded |
| `docs/` | This document, [PROTOCOL.md](PROTOCOL.md), [DEV_SETUP.md](DEV_SETUP.md), [ROADMAP.md](ROADMAP.md) |

## 6. Risks

- **Collision at runtime**: spawned collision objects carry CJ on Skyrim's real terrain and statics, and the vehicles at full speed from Whiterun to Riverwood and past two floating origin moves (both checked in game); `scripts/fake-skyrim.ps1` covers the same on flat test ground. Skyrim's land ahead of a very fast vehicle can still be loading when it is sampled (retried quickly at speed).
- **San Andreas updates** through Steam undo the downgrade. Keep the downgraded copy outside Steam, or run the downgrader again.
- **Two engines in one session**: both games must run at once; San Andreas is light, but its frame pacing affects physics.
