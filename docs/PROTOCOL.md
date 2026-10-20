# Protocol

> Status: protocol v10. Shared memory section, version handshake and heartbeat (v1), an input slot and a pose slot (v2), CJ's crouch in the pose (v3), the handover spot in the input and a ring from Skyrim to San Andreas carrying collision chunks (v4), a body slot with CJ's skeleton and San Andreas's camera and a ring from San Andreas to Skyrim carrying CJ's model (v5), vehicles: spawn requests and the vehicle keys in the input, four vehicle slots with each vehicle's parts, and the vehicles' models on the ring back (v6), sound: the mouse wheel's radio steps in the input, an audio slot with Skyrim's volumes and the radio station in the pose (v7), the handbrake as its own button, so the classic key layout can sprint on Space, handovers that re-anchor CJ where a load door or fast travel puts the player (v8), and scenes in the input: CJ following Skyrim's player when Skyrim moves it, sitting where it sits, facing the one he talks to, and a shout counter for his gesture (v9), the pose's radio station as the dial's, with Sweetroll FM (v10).

The SKSE plugin (Skyrim side, 64-bit) and the ASI plugin (San Andreas side, 32-bit) exchange data over one shared memory section. See [DESIGN.md](DESIGN.md) for the architecture and ownership rules. The design follows PerkeleRim's link.

| File | What it is |
|---|---|
| [`protocol/ahshitrim_protocol.h`](../protocol/ahshitrim_protocol.h) | Source of truth: every struct, offset and constant, with `static_assert`s |
| [`protocol/cpp/link.h`](../protocol/cpp/link.h), `link.cpp` | The link, compiled into both plugins and the tests |
| [`protocol/cpp/input.h`](../protocol/cpp/input.h) | Skyrim's keys to CJ's actions and spawn requests (`Input::Builder`), and those to San Andreas's pad on foot or in a vehicle (`Input::ToPad`) |
| [`protocol/cpp/pose.h`](../protocol/cpp/pose.h) | CJ's San Andreas pose to the `PlayerState` Skyrim reads |
| [`protocol/cpp/collision.h`](../protocol/cpp/collision.h) | Skyrim side of collision chunks: the chunk grid, the heightfield, building a payload (from PerkeleRim) |
| [`protocol/cpp/ground.h`](../protocol/cpp/ground.h) | San Andreas side: the floating origin, a payload to San Andreas's collision format, which chunks it keeps |
| [`protocol/cpp/fall_rescue.h`](../protocol/cpp/fall_rescue.h) | Where CJ goes when he ends up below the ground |
| [`protocol/cpp/skin_model.h`](../protocol/cpp/skin_model.h) | CJ's model blob: building it, cutting it into chunks, putting it back together, reading it |
| [`protocol/cpp/skin_build.h`](../protocol/cpp/skin_build.h) | San Andreas's skinned geometry to the model's meshes |
| [`protocol/cpp/texture_decode.h`](../protocol/cpp/texture_decode.h) | San Andreas's Direct3D 9 texture formats to RGBA |
| [`protocol/cpp/body.h`](../protocol/cpp/body.h) | San Andreas's skin matrices to bones, skinning a vertex, the interpolation between San Andreas's frames |
| [`protocol/cpp/camera.h`](../protocol/cpp/camera.h) | Turning Skyrim's camera node to a view (from PerkeleRim) |
| [`protocol/cpp/radio.h`](../protocol/cpp/radio.h) | Skyrim's sliders to San Andreas's volumes, the radio's dial and station names, Radio Sovngarde's track picks, Sweetroll FM's tracks and shuffle |
| [`protocol/cpp/vehicle.h`](../protocol/cpp/vehicle.h) | Where a spawned vehicle goes, a vehicle's paint and lights colours, baking a part's scale or mirror into its vertices, merging a vehicle's meshes |
| `protocol/tests/` | doctest tests (layout, handshake, heartbeat, slots, rings, input mapping, pose conversion, collision chunks, floating origin, chunk bookkeeping, fall rescue, model blob, mesh building, texture decoding, bones, interpolation, vehicle spawn spots, part baking and mesh merging, volumes, the radio dial, Radio Sovngarde and Sweetroll FM's shuffle), run by `scripts/test.ps1` |
| `protocol/tools/fake_skyrim.cpp` | Stands in for the Skyrim plugin to drive a running San Andreas and check CJ walks and his model and skeleton arrive, and that the three vehicles spawn on the ground, the Sandking drives with CJ in it and its radio dials round all 14 stops, the Sanchez rides, and a re-anchor far off and far below (fast travel in a car) takes CJ out, removes them and holds him until the ground there is in (`scripts/fake-skyrim.ps1`) |

## Section layout

One section, `Local\AhShitRim.Link`, 8,398,720 bytes. Whichever game starts first creates it; it disappears when both have closed.

| Offset | Size | Part | Written by |
|---|---|---|---|
| 0 | 128 | `LinkHeader`: magic, sizes, creator version, one `PeerInfo` per side | creator, then each side its own `PeerInfo` |
| 128 | 144 | `InputSlot`: the player's input for CJ (v9: 144 bytes, keeping the body slot on a 64-byte boundary) | Skyrim |
| 272 | 64 | `PlayerSlot`: CJ's pose | San Andreas |
| 336 | 48 | `AudioSlot`: San Andreas's volumes (v7) | Skyrim |
| 384 | 1920 | `BodySlot`: CJ's skeleton and San Andreas's camera (v5) | San Andreas |
| 2304 | 4 x 1920 | vehicle slots, one `BodySlot` per vehicle (v6) | San Andreas |
| 9984 | 64 + 4 MiB | ring Skyrim → San Andreas (collision chunks) | Skyrim produces, San Andreas consumes |
| 4204352 | 64 + 4 MiB | ring San Andreas → Skyrim (CJ's and the vehicles' models, v5) | San Andreas produces, Skyrim consumes |

`LinkHeader` (frozen across versions):

| Offset | Field | Meaning |
|---|---|---|
| 0 | `magic` | `0x4D525341`, "ASRM" in memory. Written last by the creator; 0 means still initializing |
| 4 | `headerSize` | 128 |
| 8 | `mappingSize` | the creator's `kMappingSize` |
| 12 | `creatorVersion` | the creator's `kProtocolVersion` |
| 16 | `peers[0]` | Skyrim's `PeerInfo` |
| 48 | `peers[1]` | San Andreas's `PeerInfo` |
| 80 | reserved | 48 bytes |

`PeerInfo` (32 bytes): `version` (u32, 0 while not attached), `pid` (u32, 0 after a clean detach), `heartbeat` (u64, bumped every tick), 16 reserved bytes.

Layout rules:

- Everything is packed, little-endian and fixed size, with fixed-width types only (no pointers, `size_t` or `long`): the same header compiles 64-bit into the SKSE plugin and 32-bit into the ASI, and its `static_assert`s run in both builds.
- 64-bit fields that are used atomically (the heartbeats) sit on 8-byte boundaries.
- `LinkHeader` is frozen across versions. Any other layout change bumps `kProtocolVersion`.

## Latest-value slots

Continuous state where the reader only wants the newest value: no queue, a seqlock instead. The writer bumps `seq` to odd, writes the value, bumps `seq` to even; the reader copies the value between two reads of `seq` and retries if they differ or are odd. `seq` 0 means never written. Writes are dropped until our side is attached; reads fail while the link isn't up. Doubles sit on 8-byte boundaries in the mapping.

### Input (Skyrim to San Andreas)

`InputState`, 88 bytes. Written whenever Skyrim's input changes while it has handed the player over, and every Skyrim frame then.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | Skyrim's input counter |
| 4 | `flags` | `kInputOwnsPlayer` (0x1): apply this input to CJ. `kInputPaused` (0x2): a Skyrim menu pauses the game, San Andreas pauses too. `kInputFace` (0x4, v9): a dialogue has the input, CJ stands still and turns to `sceneHeading`. `kInputScripted` (0x8, v9): Skyrim moves the player itself (a scene, a seat, a bed), CJ is put at `sceneOffset` facing `sceneHeading` and the keys do nothing. `kInputSeated` (0x10, v9, with `kInputScripted`): the player sits |
| 8 | `mouseX`, `mouseY` | doubles, raw mouse counts accumulated since Skyrim started (y positive down); San Andreas applies the change since its last read |
| 24 | `moveX`, `moveY` | -1..1, right and forwards positive; `moveY` is W and S only, the up and down arrows are `kButtonLean*` |
| 32 | `buttons` | `kButtonSprint` 0x1, `kButtonJump` 0x2, `kButtonCrouch` 0x4, `kButtonWalk` 0x8, `kButtonEnterExit` 0x10, `kButtonHorn` 0x20, `kButtonLeanForward` 0x40 (up arrow), `kButtonLeanBack` 0x80 (down arrow) (v6), `kButtonHandbrake` 0x100 (v8: Space, and Right Ctrl in the classic layout), held. Actions, not keys: Skyrim maps the keys in the modern or classic layout (`protocol/cpp/input.h`) |
| 36 | `handover` | counts handovers and re-anchors (v8: the game moved the player through a load door, by fast travel or a script while CJ has it, and the handover goes on); a new value puts CJ at `anchor`, drops the collision chunks of older ones and removes every vehicle |
| 40 | `heading` | the Skyrim player's heading at that handover or re-anchor (radians, Skyrim's convention); CJ spawns facing it |
| 44 | `anchor[3]` | the Skyrim player's feet at that handover, Skyrim world units: the floating origin's base (v4) |
| 56 | `spawnModel` | u16, the San Andreas vehicle model the last spawn request asked for (v6) |
| 58 | `spawnCount` | u16, counts spawn requests; a new value spawns `spawnModel` (v6) |
| 60 | `radioNext` | u16, counts mouse wheel steps up (v7); in a vehicle each one tunes the next station |
| 62 | `radioPrev` | u16, counts wheel steps down, the previous station (v7) |
| 64 | `sceneOffset[3]` | with `kInputScripted`: the Skyrim player's feet relative to `anchor`, Skyrim units; with `kInputSeated` the seat (the furniture marker the player uses), and San Andreas puts CJ's pelvis over it (v9) |
| 76 | `sceneHeading` | with `kInputScripted`: the player's heading; with `kInputFace`: the heading to the speaker (radians, Skyrim's convention, v9) |
| 80 | `shoutCount` | u16, counts shouts the player let out (Skyrim's voice cooldown starting, v9); a new value plays CJ's gesture on foot. San Andreas only acts on a change after the first value it read |
| 82 | reserved | 6 bytes |

Keys map to actions with GTA's keyboard layout (`input.h`): W/S/A/D or the arrows move (in a vehicle the up and down arrows lean a bike instead), Left Shift sprints, Space jumps, C crouches, Left Alt walks, F or Enter gets in and out of a vehicle, H sounds the horn. ], [ and `\` ask for a Sandking (495), a ZR-350 (477) and a Sanchez (468): each press sets `spawnModel` and counts `spawnCount` up once. Escape, Tab, M, `~` and F10 stay with Skyrim. While a Skyrim menu has the input (paused, or a cursor menu like dialogue), the axes and buttons are zero but the mouse totals and the spawn requests keep counting. San Andreas writes the actions into pad 0 right after `CPad::UpdatePads` and the mouse change into `CPad::NewMouseControllerState`, vertical flipped (passed through as is, the camera looked the wrong way). On foot: left stick, Cross for sprint, Square for jump, L3 for crouch, the walk flag, Triangle to get into a vehicle. In a vehicle: W holds Cross (accelerate) and S Square (brake, then reverse), the left stick's X steers and its Y leans a bike (up arrow forward, down arrow back), jump holds R1 (handbrake), the horn L3 and Triangle gets out. San Andreas spawns only on a change of `spawnCount` after the first value it read (a San Andreas that starts after Skyrim doesn't spawn the last vehicle again), and only while it has the player and CJ is on foot. A read of the slot that stays torn keeps the last input rather than counting as the link going down.

### Pose (San Andreas to Skyrim)

`PlayerState`, 56 bytes (48 before v7). Written every San Andreas frame (30 a second) once CJ exists. Skyrim axes and units: game units (70 to the metre), Z up, heading 0 = north and clockwise, pitch positive looking down.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | San Andreas frame counter |
| 4 | `flags` | `kPlayerValid` (0x1), `kPlayerGrounded` (0x2): CJ stands on something (or sits in a vehicle), `kPlayerCrouching` (0x4): CJ crouches, Skyrim sneaks, `kPlayerInVehicle` (0x8, v6): CJ drives or rides a vehicle, `kPlayerRadioRetuning` (0x10, v7): the radio is between stations |
| 8 | `offset[3]` | CJ relative to where the current handover spawned him |
| 20 | `velocity[3]` | units per second; in a vehicle the vehicle's, which Skyrim samples the ground ahead by |
| 32 | `heading` | CJ's body |
| 36 | `viewHeading`, `viewPitch` | where San Andreas's camera looks; Skyrim's first-person camera follows it |
| 44 | `handover` | the `InputState.handover` this pose belongs to; Skyrim ignores poses from older handovers |
| 48 | `radioStation` | i8 (v7, v10): the dial's station in CJ's vehicle: 0 emergency, 1..11 San Andreas's stations (its `eRadioID`), 12 Radio Sovngarde, 13 Sweetroll FM, 14 off, -1 (`kRadioNone`) not in a vehicle. Before v10 it was San Andreas's `eRadioID` with 12 as Radio Sovngarde and 13 off. While the wheel retunes it is the station it goes to |
| 49 | reserved | 7 bytes |

Skyrim adds `offset` to the player's position at the handover.

### Audio (Skyrim to San Andreas)

`AudioState`, 32 bytes (v7). Written every Skyrim frame.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `effects` | 0..1, Skyrim's master x effects sliders: San Andreas's effects master volume (everything but the radio) |
| 4 | `music` | 0..1, master x music: San Andreas's music master volume (the radio) |
| 8 | reserved | 24 bytes |

Both are 0 while Skyrim has the player. San Andreas sets them every frame (0..64); a pausing menu pauses its sounds through `kInputPaused` instead. Mouse wheel steps in the input dial San Andreas's radio only while CJ is in a vehicle and nothing pauses (`protocol/cpp/radio.h`: 1..11, Radio Sovngarde, Sweetroll FM, off, round and round); on Radio Sovngarde and Sweetroll FM San Andreas's radio is off, and Skyrim plays its own music, or Sweetroll FM's tracks.

Log lines: `audio: sound on (effects N/64, music N/64)` / `audio: muted`, `audio: the wheel tunes NAME`, `audio: radio on NAME` / `audio: no radio`, `audio: San Andreas's radio at ID (on B, retuning B)` in San Andreas's log; `audio: menu sound category ID "NAME" at V (master|music|effects)`, `audio: radio NAME`, `audio: master volume V (fAudioMasterVolume)`, `audio: Skyrim's music paused for the radio|back`, `audio: Radio Sovngarde|Sweetroll FM plays PATH (N s)`, `audio: Sweetroll FM has N of 12 tracks installed in ...` at data load, `audio: Sweetroll FM has no tracks installed (...), so it stays silent` (once) in Skyrim's.

### Body (San Andreas to Skyrim)

`BodyState`, 1,856 bytes (v5). Written every San Andreas frame once CJ's model is built. Skyrim axes and units; `feet` and the camera are relative to where the handover put CJ's feet (`InputState.anchor` in Skyrim), the bones to `feet`.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | San Andreas frame counter, as `PlayerState.frame` |
| 4 | `flags` | `kBodyValid` (0x1) |
| 8 | `handover` | the `InputState.handover` this belongs to |
| 12 | `model` | the `ModelChunkHeader.model` the bones belong to |
| 16 | `boneCount` | bones in use (CJ has 32), the model's bone count |
| 20 | `timeMs` | San Andreas's high-resolution clock in ms, for Skyrim's interpolation |
| 24 | `feet[3]` | CJ's feet |
| 36 | `cameraPosition[3]` | San Andreas's active camera |
| 48 | `cameraForward[3]` | where it looks, a unit vector |
| 60 | `cameraFov` | its field of view in degrees, horizontal for a 4:3 screen like Skyrim's own setting |
| 64 | `bones[64]` | `BodyBone`: `rotation` (quaternion x, y, z, w) and `translation[3]`; takes a model vertex (bind pose) to its place relative to `feet` |

A posed vertex is the weighted sum of where its (up to four) bones take it, its normal turned the same way. Skyrim draws the frame 50 ms behind the newest, blended between the two around it (`Body::Interpolator`). San Andreas walks CJ on Skyrim's own collision there (see below), so the offset follows Skyrim's terrain and statics; the floating origin moving in San Andreas doesn't change it.

### Vehicles (San Andreas to Skyrim)

Four vehicle slots (v6), one per vehicle San Andreas keeps (`kVehicleMax`), each a `BodySlot` holding a `BodyState`, written every San Andreas frame once that vehicle's model is built:

| Field | For a vehicle |
|---|---|
| `flags` | `kBodyValid` (0x1); `kBodyDriven` (0x2) while CJ is in it. 0 once the vehicle is gone: Skyrim draws nothing for the slot |
| `model` | the vehicle's `ModelChunkHeader.model` |
| `boneCount` | its parts (one per drawn atomic: chassis, wheels, doors, bonnet, a bike's forks and handlebars) |
| `feet[3]` | the vehicle's position, relative to where the handover put CJ's feet |
| `bones` | each part's frame relative to `feet`: its world matrix after `PreRender` (wheels turned, steered and on their suspension, forks and handlebars, and a bike's lean, which `PreRender` puts into the parts under the upright root) |
| camera | unused (zero, forward a unit vector) |

Skyrim draws each vehicle like CJ, 50 ms behind with its own `Body::Interpolator`, and only while it has handed the player over. The chase camera needs nothing of its own: San Andreas's active camera in the body slot follows the vehicle while CJ drives it.

## Rings

Messages that must all arrive, one after another: two single-producer, single-consumer byte rings of 4 MiB, one each way (Skyrim to San Andreas since v4, San Andreas to Skyrim since v5). `head` (producer) and `tail` (consumer) are monotonic byte counts on separate 32-byte halves of the 64-byte `RingHeader`; a record is an 8-byte `MessageHeader` (`type` u16, reserved u16, `size` u32) plus its payload, and may wrap around the end. `Send` puts a message in the ring to the other side and refuses while the link is down, when the ring is full or when the message can never fit; each side drains its own ring every frame. A side empties its ring when it attaches, so messages sent to an earlier instance never arrive.

| Type | Message | Direction | Payload |
|---|---|---|---|
| 1 | `CollisionChunk` | Skyrim → San Andreas | `CollisionChunkHeader` (chunk x/y, worldspace, generation, origin, counts), vertices relative to origin, triangle indices |
| 2 | `ModelChunk` | San Andreas → Skyrim (v5; vehicles since v6) | `ModelChunkHeader` (model id, total size, offset, size), then that part of the model blob |

### CJ's model

San Andreas sends CJ's model whenever the link comes up (a Skyrim that just started has none of it) and whenever his geometry changes, as one blob cut into `ModelChunk`s of at most 256 KiB (`protocol/cpp/skin_model.h`). Chunks of a model come in order from offset 0; Skyrim builds the model once the last one is in, and drops one that comes out of order. Every model gets a new id; `BodyState.model` names the one its bones belong to. CJ's model and the vehicles' go one whole model after another (`sa/src/model_sender.cpp`), CJ's first, all of them again whenever the link comes up.

The blob: `SkinModelHeader` (48 bytes: name, texture, mesh and bone counts, `kind`: `kSkinModelBody` 0 or `kSkinModelVehicle` 1 (v6)), `boneCount` u32 bone ids (San Andreas's node ids, for the logs), then per texture a `SkinTexture` (width, height, format 1 = RGBA8) and its pixels, top row first, then per mesh a `SkinMesh` (texture index or -1, vertex and index counts, flags `kSkinMeshCutout` 0x1 / `kSkinMeshBlend` 0x2, RGBA colour) with `vertexCount` `SkinVertex` (52 bytes: position, normal, UV, four bone indices, four weights; bind pose, Skyrim units) and `indexCount` u16 triangle indices. CJ comes as 5 meshes, about 1,750 vertices and 2,370 triangles, 4 textures and 32 bones, about 580 KB.

### A vehicle's model

Sent once per spawned vehicle, after its first `PreRender` (`sa/src/vehicles.cpp`, the game-free half in `protocol/cpp/vehicle.h`). Every drawn atomic of the vehicle's clump is a part, and a bone: hidden atomics (the damaged versions of parts) and the low-detail ones (`_vlo`) are left out. Each part's vertices are in its own frame's space and hang on its bone alone (weight 1). A part's frame can carry a scale or a mirror besides its rotation (wheels are scaled to the wheel size), which a bone can't, so it is baked into the part's vertices once (the frame is the bone's rotation times a constant matrix; a mirror turns the triangles round). Materials take the vehicle's own colours as San Andreas does right before drawing (the four paint placeholders from the colour table, the lights texture's materials white); one with alpha in its colour (glass) is `kSkinMeshBlend`. Parts' meshes that look the same are merged, so a vehicle comes as a few dozen meshes: the Sandking as 17 parts, 21 meshes, about 3,800 vertices and 13 textures (1.1 MB), the Sanchez as 7 parts and 8 meshes (870 KB).

Skyrim multiplies a textured mesh's texture by its colour (a vehicle's paint is a grey texture in the vehicle's colour) into a copy of the texture, since its shader has no tint. A blended mesh is drawn with alpha blending, its colour's alpha as the material's, without writing depth.

### Collision chunks

Chunks are `kCollisionChunkUnits` (2048 units, about 29 m) squares on a grid anchored at Skyrim's world (0, 0); chunk `(x, y)` starts at `(x * 2048, y * 2048)`. The payload is the 40-byte header, `vertexCount` vertices (`float[3]`, Skyrim units relative to `origin`) and `indexCount` triangle indices (u32). Triangles wind counterclockwise seen from outside in Skyrim (from above for the land); San Andreas swaps two corners per triangle, since it faces them the other way. `generation` is the `InputState.handover` the chunk belongs to: San Andreas builds chunks of the handover it has read, keeps newer ones until it reads that handover, and drops older ones. A chunk with the same x/y and generation replaces the one before. A chunk has at most 65,000 vertices (San Andreas indexes them with 16 bits); static triangles past that are left out and logged.

Skyrim side (`skse/src/collision/CollisionSampler.cpp`, `StaticMeshes.cpp`, ported from PerkeleRim; geometry in `protocol/cpp/collision.h`): while San Andreas has the player, each chunk is two parts in one payload.

- **Land**: a heightfield of 33 x 33 samples (64 units apart) from top-down Havok raycasts that keep only the landscape (the terrain or ground layer where it belongs to no reference) and pass through everything else. A ray that misses is tried again 8 units to each side; a sample still missed with at least three hit neighbours takes their mean height, three passes over, so only wide misses leave a hole. Interiors have no land and skip this.
- **Statics**: the real collision of every loaded reference with static collision (layers static, animated static, terrain, ground, trees, stair helpers and transparent, plus clutter on bodies that don't move), read from its Havok shapes: triangle meshes as they are, boxes and convex hulls as their faces, capsules as 8-sided prisms. Spheres and transform wrappers that don't read as a rotation are skipped and counted in the log. A triangle belongs to the chunk its center is in. When a door opens or closes, or a gate or trapdoor moves and settles, the chunks around it are read and sent again.

The 5 x 5 chunks around the player are sampled nearest first, then the 5 x 5 around a point 2 s of travel ahead (at most 3 chunks ahead, a car at 50 m/s), about 2 ms of raycasts per frame, 4 ms while the player moves faster than 10 m/s (a chunk takes about 6 frames, and a fast car needs a new row of 5 every half second). A chunk is done once at least 99% of its land samples hit and no static in it is still waiting for its 3D; otherwise it is sampled again every 2 s, every 0.4 s above 10 m/s (up to 5 tries), and resent when it improves. Ahead of a fast car Skyrim is often still loading the land (a chunk came with 33 of 1089 land samples at 40 m/s). Skyrim forgets chunks more than 3 away from both centers; San Andreas drops them more than 7 away from CJ, so a chunk San Andreas dropped is always one Skyrim resends.

San Andreas side (`sa/src/chunk_world.cpp`, the game-free half in `protocol/cpp/ground.h`): one invisible `CBuilding` per chunk, its model's `CColModel` holding the chunk's triangles, all of them tarmac (`SURFACE_TARMAC`: chunks carry no material yet), under the floating origin described in [DESIGN.md](DESIGN.md#ground-skyrims-collision-in-san-andreas).

Log lines for CJ: `body: CJ's model ID read in N ms: N meshes, N vertices, N triangles (...), N bones (world|local space), N textures: ...`, `body: CJ's model ID sent to Skyrim in N chunks`, `body: texture NAME of CJ left white: why` in San Andreas's log; `model view: model ID CJ built in N ms: ...`, `model view: CJ ID attached under NODE`, `model view: CJ ID shown|hidden`, `player sync: CJ drawn from San Andreas's model ID, Skyrim's camera on San Andreas's (fov N)` in Skyrim's.

Log lines for vehicles, in San Andreas's log: `vehicle: NAME (MODEL) spawned in slot N at (x,y,z), spot N of 15 tried, colours N/N`, or why nothing was (`CJ is in a vehicle`, `no flat, clear spot for the NAME in front of CJ`, ...); `vehicle: the NAME's model ID read in N ms: N parts (N with a scale or mirror baked in; ...), N meshes, ...`, `vehicle: the NAME's model ID sent to Skyrim in N chunks`; `vehicle: driving at N m/s, N wheels on the ground, gas N, brake N, gear N, engine on|off, status N` every 5 s while CJ drives; `vehicle: the NAME in slot N removed|is gone`, `vehicle: CJ taken out of his vehicle` at a new handover; `ground: CJ's vehicle was below the ground at (...), put back at (...)`. In Skyrim's: `model view: NAME ID built|shown|hidden`.

Log lines: `collision: chunk (x,y) gen N try T: terrain hits/1089 (...), statics N triangles from N refs (...), ms over N frames + ms statics, sent|nothing new` in Skyrim's log; `ground: handover N at Skyrim (x,y,z), waiting for the ground under CJ`, `ground: the ground under CJ is in after S s, letting him go`, `ground: N chunks built (N resent, N triangles, N left out), N far ones dropped, N stale; N chunks around CJ` (every 5 s while anything happens), `ground: origin moved by (x,y,z) m with CJ`, `ground: CJ was below the ground at (...), put back at (...)` in San Andreas's.

## Handshake and liveness

Both sides run the same state machine (`Detached` → `Waiting` → `Up`, or `Disabled`), ticked on their own thread at 20 Hz, so it also runs in menus and while loading.

1. Create or open the section. A fresh section gets its header filled in and the magic written last; a joining side waits while the magic is still 0.
2. Unknown magic or header size: the link is disabled (`not an AhShitRim link`).
3. Each side publishes its `PeerInfo` (version, pid), even when the versions differ, so both sides can report a mismatch. If the creator's version, or the peer's, differs from ours, the side logs `protocol mismatch: ... speaks vN, this ... speaks vM` and stays disabled until restarted.
4. Each tick bumps our heartbeat. The link is **up** while the peer's heartbeat keeps moving. The first read only seeds the value, so a crashed peer never looks alive. It goes **down** after 2 s without a heartbeat change, or at once when the peer detaches cleanly (pid set to 0 on exit).

Log lines on both sides, prefixed `link:`:

- `started (protocol v10)`
- `created shared memory, waiting for peer` / `joined shared memory, waiting for peer`
- `link up: <peer> pid N, protocol v10`
- `link down: the <peer> detached` / `link down: no heartbeat from the <peer> for 2000 ms`
- `link disabled: ...`

The Skyrim side logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, the San Andreas side to `<SaPath>\scripts\AhShitRim.log`. `scripts/logs.ps1` shows both.
