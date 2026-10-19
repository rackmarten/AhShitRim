# Protocol

> Status: protocol v4. Shared memory section, version handshake and heartbeat (v1), an input slot and a pose slot (v2), CJ's crouch in the pose (v3), the handover spot in the input and a ring from Skyrim to San Andreas carrying collision chunks (v4). A ring the other way arrives with the increment that needs it.

The SKSE plugin (Skyrim side, 64-bit) and the ASI plugin (San Andreas side, 32-bit) exchange data over one shared memory section. See [DESIGN.md](DESIGN.md) for the architecture and ownership rules. The design follows PerkeleRim's link.

| File | What it is |
|---|---|
| [`protocol/ahshitrim_protocol.h`](../protocol/ahshitrim_protocol.h) | Source of truth: every struct, offset and constant, with `static_assert`s |
| [`protocol/cpp/link.h`](../protocol/cpp/link.h), `link.cpp` | The link, compiled into both plugins and the tests |
| [`protocol/cpp/input.h`](../protocol/cpp/input.h) | Skyrim's keys to CJ's actions (`Input::Builder`) and those to San Andreas's pad (`Input::ToPad`) |
| [`protocol/cpp/pose.h`](../protocol/cpp/pose.h) | CJ's San Andreas pose to the `PlayerState` Skyrim reads |
| [`protocol/cpp/collision.h`](../protocol/cpp/collision.h) | Skyrim side of collision chunks: the chunk grid, the heightfield, building a payload (from PerkeleRim) |
| [`protocol/cpp/ground.h`](../protocol/cpp/ground.h) | San Andreas side: the floating origin, a payload to San Andreas's collision format, which chunks it keeps |
| [`protocol/cpp/fall_rescue.h`](../protocol/cpp/fall_rescue.h) | Where CJ goes when he ends up below the ground |
| `protocol/tests/` | doctest tests (layout, handshake, heartbeat, slots, ring, input mapping, pose conversion, collision chunks, floating origin, chunk bookkeeping, fall rescue), run by `scripts/test.ps1` |
| `protocol/tools/fake_skyrim.cpp` | Stands in for the Skyrim plugin to drive a running San Andreas (`scripts/fake-skyrim.ps1`) |

## Section layout

One section, `Local\AhShitRim.Link`, 4,194,688 bytes. Whichever game starts first creates it; it disappears when both have closed.

| Offset | Size | Part | Written by |
|---|---|---|---|
| 0 | 128 | `LinkHeader`: magic, sizes, creator version, one `PeerInfo` per side | creator, then each side its own `PeerInfo` |
| 128 | 80 | `InputSlot`: the player's input for CJ | Skyrim |
| 208 | 64 | `PlayerSlot`: CJ's pose | San Andreas |
| 272 | 48 | unused | |
| 320 | 64 + 4 MiB | ring Skyrim → San Andreas (collision chunks) | Skyrim produces, San Andreas consumes |

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

`InputState`, 64 bytes. Written whenever Skyrim's input changes while it has handed the player over, and every Skyrim frame then.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | Skyrim's input counter |
| 4 | `flags` | `kInputOwnsPlayer` (0x1): apply this input to CJ. `kInputPaused` (0x2): a Skyrim menu pauses the game, San Andreas pauses too |
| 8 | `mouseX`, `mouseY` | doubles, raw mouse counts accumulated since Skyrim started (y positive down); San Andreas applies the change since its last read |
| 24 | `moveX`, `moveY` | -1..1, right and forwards positive |
| 32 | `buttons` | `kButtonSprint` 0x1, `kButtonJump` 0x2, `kButtonCrouch` 0x4, `kButtonWalk` 0x8, held |
| 36 | `handover` | counts handovers; a new value puts CJ at the handover spot and drops the collision chunks of older handovers |
| 40 | `heading` | the Skyrim player's heading at that handover (radians, Skyrim's convention); CJ spawns facing it |
| 44 | `anchor[3]` | the Skyrim player's feet at that handover, Skyrim world units: the floating origin's base (v4) |
| 56 | reserved | 8 bytes |

Keys map to actions with GTA's keyboard layout (`input.h`): W/S/A/D or the arrows move, Left Shift sprints, Space jumps, C crouches, Left Alt walks. Escape, Tab, M, `~` and F10 stay with Skyrim. While a Skyrim menu has the input (paused, or a cursor menu like dialogue), the axes and buttons are zero but the mouse totals keep counting. San Andreas writes the actions into pad 0 right after `CPad::UpdatePads` (left stick, Cross for sprint, Square for jump, L3 for crouch, the walk flag) and the mouse change into `CPad::NewMouseControllerState`, vertical flipped (passed through as is, the camera looked the wrong way).

### Pose (San Andreas to Skyrim)

`PlayerState`, 48 bytes. Written every San Andreas frame (30 a second) once CJ exists. Skyrim axes and units: game units (70 to the metre), Z up, heading 0 = north and clockwise, pitch positive looking down.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | San Andreas frame counter |
| 4 | `flags` | `kPlayerValid` (0x1), `kPlayerGrounded` (0x2): CJ stands on something, `kPlayerCrouching` (0x4): CJ crouches, Skyrim sneaks |
| 8 | `offset[3]` | CJ relative to where the current handover spawned him |
| 20 | `velocity[3]` | units per second |
| 32 | `heading` | CJ's body |
| 36 | `viewHeading`, `viewPitch` | where San Andreas's camera looks; Skyrim's first-person camera follows it |
| 44 | `handover` | the `InputState.handover` this pose belongs to; Skyrim ignores poses from older handovers |

Skyrim adds `offset` to the player's position at the handover. San Andreas walks CJ on Skyrim's own collision there (see below), so the offset follows Skyrim's terrain and statics; the floating origin moving in San Andreas doesn't change it.

## Ring

Messages that must all arrive, one after another: a single-producer, single-consumer byte ring from Skyrim to San Andreas, 4 MiB. `head` (Skyrim) and `tail` (San Andreas) are monotonic byte counts on separate 32-byte halves of the 64-byte `RingHeader`; a record is an 8-byte `MessageHeader` (`type` u16, reserved u16, `size` u32) plus its payload, and may wrap around the end. `Send` refuses while the link is down, when the ring is full or when the message can never fit; San Andreas drains it every frame. San Andreas empties the ring when it attaches, so chunks sent to an earlier San Andreas never arrive.

| Type | Message | Payload |
|---|---|---|
| 1 | `CollisionChunk` | `CollisionChunkHeader` (chunk x/y, worldspace, generation, origin, counts), vertices relative to origin, triangle indices |

### Collision chunks

Chunks are `kCollisionChunkUnits` (2048 units, about 29 m) squares on a grid anchored at Skyrim's world (0, 0); chunk `(x, y)` starts at `(x * 2048, y * 2048)`. The payload is the 40-byte header, `vertexCount` vertices (`float[3]`, Skyrim units relative to `origin`) and `indexCount` triangle indices (u32). Triangles wind counterclockwise seen from outside in Skyrim (from above for the land); San Andreas swaps two corners per triangle, since it faces them the other way. `generation` is the `InputState.handover` the chunk belongs to: San Andreas builds chunks of the handover it has read, keeps newer ones until it reads that handover, and drops older ones. A chunk with the same x/y and generation replaces the one before. A chunk has at most 65,000 vertices (San Andreas indexes them with 16 bits); static triangles past that are left out and logged.

Skyrim side (`skse/src/collision/CollisionSampler.cpp`, `StaticMeshes.cpp`, ported from PerkeleRim; geometry in `protocol/cpp/collision.h`): while San Andreas has the player, each chunk is two parts in one payload.

- **Land**: a heightfield of 33 x 33 samples (64 units apart) from top-down Havok raycasts that keep only the landscape (the terrain or ground layer where it belongs to no reference) and pass through everything else. A ray that misses is tried again 8 units to each side; a sample still missed with at least three hit neighbours takes their mean height, three passes over, so only wide misses leave a hole. Interiors have no land and skip this.
- **Statics**: the real collision of every loaded reference with static collision (layers static, animated static, terrain, ground, trees, stair helpers and transparent, plus clutter on bodies that don't move), read from its Havok shapes: triangle meshes as they are, boxes and convex hulls as their faces, capsules as 8-sided prisms. Spheres and transform wrappers that don't read as a rotation are skipped and counted in the log. A triangle belongs to the chunk its center is in. When a door opens or closes, or a gate or trapdoor moves and settles, the chunks around it are read and sent again.

The 5 x 5 chunks around the player are sampled nearest first, then the 5 x 5 around a point 2 s of travel ahead (at most 2 chunks ahead), about 2 ms of raycasts per frame. A chunk is done once at least 99% of its land samples hit and no static in it is still waiting for its 3D; otherwise it is sampled again every 2 s (up to 5 tries) and resent when it improves. Skyrim forgets chunks more than 3 away from both centers; San Andreas drops them more than 6 away from CJ, so a chunk San Andreas dropped is always one Skyrim resends.

San Andreas side (`sa/src/chunk_world.cpp`, the game-free half in `protocol/cpp/ground.h`): one invisible `CBuilding` per chunk, its model's `CColModel` holding the chunk's triangles, under the floating origin described in [DESIGN.md](DESIGN.md#ground-skyrims-collision-in-san-andreas).

Log lines: `collision: chunk (x,y) gen N try T: terrain hits/1089 (...), statics N triangles from N refs (...), ms over N frames + ms statics, sent|nothing new` in Skyrim's log; `ground: handover N at Skyrim (x,y,z), waiting for the ground under CJ`, `ground: the ground under CJ is in after S s, letting him go`, `ground: N chunks built (N resent, N triangles, N left out), N far ones dropped, N stale; N chunks around CJ` (every 5 s while anything happens), `ground: origin moved by (x,y,z) m with CJ`, `ground: CJ was below the ground at (...), put back at (...)` in San Andreas's.

## Handshake and liveness

Both sides run the same state machine (`Detached` → `Waiting` → `Up`, or `Disabled`), ticked on their own thread at 20 Hz, so it also runs in menus and while loading.

1. Create or open the section. A fresh section gets its header filled in and the magic written last; a joining side waits while the magic is still 0.
2. Unknown magic or header size: the link is disabled (`not an AhShitRim link`).
3. Each side publishes its `PeerInfo` (version, pid), even when the versions differ, so both sides can report a mismatch. If the creator's version, or the peer's, differs from ours, the side logs `protocol mismatch: ... speaks vN, this ... speaks vM` and stays disabled until restarted.
4. Each tick bumps our heartbeat. The link is **up** while the peer's heartbeat keeps moving. The first read only seeds the value, so a crashed peer never looks alive. It goes **down** after 2 s without a heartbeat change, or at once when the peer detaches cleanly (pid set to 0 on exit).

Log lines on both sides, prefixed `link:`:

- `started (protocol v4)`
- `created shared memory, waiting for peer` / `joined shared memory, waiting for peer`
- `link up: <peer> pid N, protocol v4`
- `link down: the <peer> detached` / `link down: no heartbeat from the <peer> for 2000 ms`
- `link disabled: ...`

The Skyrim side logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, the San Andreas side to `<SaPath>\scripts\AhShitRim.log`. `scripts/logs.ps1` shows both.
