# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- CJ in Skyrim (protocol v5): San Andreas sends CJ's model once over a new ring back to Skyrim (his skinned geometry, skin weights and textures, read from the running game: nothing shipped or written to disk), and his 32 bones and its camera every frame in a new body slot. San Andreas animates CJ in full while hidden and poses his bones itself, shoulders included (`CPed::ShoulderBoneRotation`, which only runs while drawing). CJ is dressed in his vest, jeans and sneakers, as the intro script does. Skyrim builds the model in memory, skins it on the CPU into dynamic vertex buffers every frame, draws CJ at the player's place with the player's own body hidden, and puts its camera on San Andreas's third-person camera (position, direction and field of view), all smoothed between San Andreas's 30 frames a second. Skyrim's sneak eye is hidden while CJ stands, so it no longer stays up after a crouch. First person isn't supported yet. Game-free tests for the model blob, mesh building, texture decoding, the bone math and the interpolation; `scripts/fake-skyrim.ps1` also checks CJ's model arrives and skins to a man's height with moving bones.
- Skyrim's ground in San Andreas: while San Andreas has the player, the SKSE plugin samples Skyrim's terrain (a heightfield) and the statics' real collision around the player in 29 m chunks (PerkeleRim's collision sampler) and streams them over a new 4 MiB ring (protocol v4). San Andreas builds each chunk as an invisible collision object around CJ, under a floating origin that moves with him, drops far ones, holds CJ at the handover spot until the ground under him is in, and puts him back where he last stood if he ends up below the ground. Game-free tests for the ring, the chunk payloads, the floating origin, the chunk bookkeeping and the fall rescue; `scripts/fake-skyrim.ps1` sends flat ground and checks CJ walks on it.
- Hidden San Andreas runner: the ASI starts San Andreas windowed and minimized (`scripts/launch-sa.ps1`, `-Visible` for a small 800x600 window), keeps it simulating without the focus, skips the splashes, movies and main menu, and starts a new game without the mission script in an empty world (no map collision or IPLs, no peds, traffic or wanted level). Physics runs at 30 frames a second, nothing is rendered while hidden, and sounds are muted.
- F10 in Skyrim hands the player to San Andreas and back. While handed over, WASD, mouse look, Shift (sprint), Space (jump), C (crouch) and Alt (walk) drive CJ, and his position, heading and velocity move the Skyrim player in first person. Escape, Tab, M and the console stay Skyrim's.
- Protocol v2 and v3: an input slot (Skyrim to San Andreas) and a pose slot (San Andreas to Skyrim, with CJ's crouch, which makes Skyrim sneak), with game-free tests for the input mapping and the pose conversion.
- `scripts/restart-games.ps1` (the whole test loop in one command) and `scripts/fake-skyrim.ps1` (drives a running San Andreas without Skyrim and checks CJ walks).
- README: concept, architecture, planned features, milestone 1, requirements and asset policy.
- Scaffold: SKSE plugin (`skse/`, CommonLibSSE-NG v10.1.0, CMake + vcpkg) and 32-bit San Andreas ASI plugin (`sa/`, plugin-sdk, CMake with the Visual Studio generator). Both log when they load; the ASI refuses to start on any exe but GTA: San Andreas 1.0 US and logs why.
- Protocol v1 link ([docs/PROTOCOL.md](docs/PROTOCOL.md)): a shared memory section with magic, protocol version and a heartbeat per side. Each side logs when the other appears, disappears or speaks another version. The same link code compiles into both plugins (64-bit and 32-bit).
- Game-free unit tests for the protocol header layout and the handshake (doctest, `scripts/test.ps1`).
- `scripts/build.ps1`, `deploy-local.ps1`, `test.ps1`, `logs.ps1`; `local.props.example` for game paths.
- [docs/DESIGN.md](docs/DESIGN.md), [docs/PROTOCOL.md](docs/PROTOCOL.md), [docs/DEV_SETUP.md](docs/DEV_SETUP.md) (Build Tools, plugin-sdk, the San Andreas downgrade and ASI loader).
