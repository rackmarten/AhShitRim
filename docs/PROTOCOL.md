# Protocol

> Status: protocol v3. Shared memory section, version handshake and heartbeat (v1), an input slot and a pose slot (v2), CJ's crouch in the pose (v3). Rings for collision and events arrive with the milestone 1 increments that need them, each bumping the version.

The SKSE plugin (Skyrim side, 64-bit) and the ASI plugin (San Andreas side, 32-bit) exchange data over one shared memory section. See [DESIGN.md](DESIGN.md) for the architecture and ownership rules. The design follows PerkeleRim's link.

| File | What it is |
|---|---|
| [`protocol/ahshitrim_protocol.h`](../protocol/ahshitrim_protocol.h) | Source of truth: every struct, offset and constant, with `static_assert`s |
| [`protocol/cpp/link.h`](../protocol/cpp/link.h), `link.cpp` | The link, compiled into both plugins and the tests |
| [`protocol/cpp/input.h`](../protocol/cpp/input.h) | Skyrim's keys to CJ's actions (`Input::Builder`) and those to San Andreas's pad (`Input::ToPad`) |
| [`protocol/cpp/pose.h`](../protocol/cpp/pose.h) | CJ's San Andreas pose to the `PlayerState` Skyrim reads |
| `protocol/tests/` | doctest tests (layout, handshake, heartbeat, slots, input mapping, pose conversion), run by `scripts/test.ps1` |
| `protocol/tools/fake_skyrim.cpp` | Stands in for the Skyrim plugin to drive a running San Andreas (`scripts/fake-skyrim.ps1`) |

## Section layout

One section, `Local\AhShitRim.Link`, 4096 bytes. Whichever game starts first creates it; it disappears when both have closed.

| Offset | Size | Part | Written by |
|---|---|---|---|
| 0 | 128 | `LinkHeader`: magic, sizes, creator version, one `PeerInfo` per side | creator, then each side its own `PeerInfo` |
| 128 | 64 | `InputSlot`: the player's input for CJ | Skyrim |
| 192 | 64 | `PlayerSlot`: CJ's pose | San Andreas |
| 256 | 3840 | unused | |

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

`InputState`, 48 bytes. Written whenever Skyrim's input changes while it has handed the player over, and every Skyrim frame then.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | Skyrim's input counter |
| 4 | `flags` | `kInputOwnsPlayer` (0x1): apply this input to CJ. `kInputPaused` (0x2): a Skyrim menu pauses the game, San Andreas pauses too |
| 8 | `mouseX`, `mouseY` | doubles, raw mouse counts accumulated since Skyrim started (y positive down); San Andreas applies the change since its last read |
| 24 | `moveX`, `moveY` | -1..1, right and forwards positive |
| 32 | `buttons` | `kButtonSprint` 0x1, `kButtonJump` 0x2, `kButtonCrouch` 0x4, `kButtonWalk` 0x8, held |
| 36 | `handover` | counts handovers; a new value puts CJ back at the test plane's spawn |
| 40 | `heading` | the Skyrim player's heading at that handover (radians, Skyrim's convention); CJ spawns facing it |

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

Skyrim adds `offset` to the player's position at the handover. San Andreas's ground is still the flat test plane, so this only makes sense on flat Skyrim ground until the next increment builds Skyrim's own ground in San Andreas.

## Handshake and liveness

Both sides run the same state machine (`Detached` → `Waiting` → `Up`, or `Disabled`), ticked on their own thread at 20 Hz, so it also runs in menus and while loading.

1. Create or open the section. A fresh section gets its header filled in and the magic written last; a joining side waits while the magic is still 0.
2. Unknown magic or header size: the link is disabled (`not an AhShitRim link`).
3. Each side publishes its `PeerInfo` (version, pid), even when the versions differ, so both sides can report a mismatch. If the creator's version, or the peer's, differs from ours, the side logs `protocol mismatch: ... speaks vN, this ... speaks vM` and stays disabled until restarted.
4. Each tick bumps our heartbeat. The link is **up** while the peer's heartbeat keeps moving. The first read only seeds the value, so a crashed peer never looks alive. It goes **down** after 2 s without a heartbeat change, or at once when the peer detaches cleanly (pid set to 0 on exit).

Log lines on both sides, prefixed `link:`:

- `started (protocol v3)`
- `created shared memory, waiting for peer` / `joined shared memory, waiting for peer`
- `link up: <peer> pid N, protocol v3`
- `link down: the <peer> detached` / `link down: no heartbeat from the <peer> for 2000 ms`
- `link disabled: ...`

The Skyrim side logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, the San Andreas side to `<SaPath>\scripts\AhShitRim.log`. `scripts/logs.ps1` shows both.
