# Protocol

> Status: protocol v1. Shared memory section, version handshake and heartbeat. Slots and rings for input, poses, collision and events arrive with the milestone 1 increments that need them, each bumping the version.

The SKSE plugin (Skyrim side, 64-bit) and the ASI plugin (San Andreas side, 32-bit) exchange data over one shared memory section. See [DESIGN.md](DESIGN.md) for the architecture and ownership rules. The design follows PerkeleRim's link.

| File | What it is |
|---|---|
| [`protocol/ahshitrim_protocol.h`](../protocol/ahshitrim_protocol.h) | Source of truth: every struct, offset and constant, with `static_assert`s |
| [`protocol/cpp/link.h`](../protocol/cpp/link.h), `link.cpp` | The link, compiled into both plugins and the tests |
| `protocol/tests/` | doctest tests (layout, handshake, heartbeat), run by `scripts/test.ps1` |

## Section layout

One section, `Local\AhShitRim.Link`, 4096 bytes. Whichever game starts first creates it; it disappears when both have closed.

| Offset | Size | Part | Written by |
|---|---|---|---|
| 0 | 128 | `LinkHeader`: magic, sizes, creator version, one `PeerInfo` per side | creator, then each side its own `PeerInfo` |
| 128 | 3968 | unused in v1 | |

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

## Handshake and liveness

Both sides run the same state machine (`Detached` → `Waiting` → `Up`, or `Disabled`), ticked on their own thread at 20 Hz, so it also runs in menus and while loading.

1. Create or open the section. A fresh section gets its header filled in and the magic written last; a joining side waits while the magic is still 0.
2. Unknown magic or header size: the link is disabled (`not an AhShitRim link`).
3. Each side publishes its `PeerInfo` (version, pid), even when the versions differ, so both sides can report a mismatch. If the creator's version, or the peer's, differs from ours, the side logs `protocol mismatch: ... speaks vN, this ... speaks vM` and stays disabled until restarted.
4. Each tick bumps our heartbeat. The link is **up** while the peer's heartbeat keeps moving. The first read only seeds the value, so a crashed peer never looks alive. It goes **down** after 2 s without a heartbeat change, or at once when the peer detaches cleanly (pid set to 0 on exit).

Log lines on both sides, prefixed `link:`:

- `started (protocol v1)`
- `created shared memory, waiting for peer` / `joined shared memory, waiting for peer`
- `link up: <peer> pid N, protocol v1`
- `link down: the <peer> detached` / `link down: no heartbeat from the <peer> for 2000 ms`
- `link disabled: ...`

The Skyrim side logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, the San Andreas side to `<SaPath>\scripts\AhShitRim.log`. `scripts/logs.ps1` shows both.
