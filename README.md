# AhShitRim

> **Thane of Grove Street.** San Andreas in Skyrim.

**GTA: San Andreas** and **Skyrim Anniversary Edition** run at the same time. Skyrim owns the world and draws everything; San Andreas runs hidden and simulates CJ, his cars, his guns and his cheat codes. Hijack a cart outside Helgen, do a drive-by on the Thalmor, and outrun Whiterun's guards with five stars on your head.

A sibling of [PerkeleRim](https://github.com/rackmarten/PerkeleRim) (My Summer Car in Skyrim), built the same way.

## Status

**Early development.** San Andreas runs hidden as a simulator of CJ: F10 in Skyrim hands the player to it, Skyrim's keyboard and mouse drive CJ on Skyrim's own terrain and statics, rebuilt as collision around him in San Andreas, and Skyrim draws CJ there from San Andreas's own model, animated by San Andreas, seen through San Andreas's own camera. CJ can spawn a Sandking, a ZR-350 or a Sanchez in front of him and drive or ride it on Skyrim's roads, the vehicle drawn from San Andreas's own model too. Meanwhile Skyrim shows San Andreas's HUD (clock, health, money) and text styles, in San Andreas's own fonts. The first milestone tackles the hardest part first (see [Milestone 1: on the ground](#milestone-1-on-the-ground)).

Installing and playing it: [docs/INSTALL.md](docs/INSTALL.md). Building it yourself: [docs/DEV_SETUP.md](docs/DEV_SETUP.md). Design: [docs/DESIGN.md](docs/DESIGN.md). The link between the games: [docs/PROTOCOL.md](docs/PROTOCOL.md). Where it's going: [docs/ROADMAP.md](docs/ROADMAP.md).

## Keys

While San Andreas has the player (F10), the keys follow San Andreas's own keyboard layout:

| Key | On foot | In a vehicle |
|---|---|---|
| F10 | Hand the player back to Skyrim | Hand the player back to Skyrim (CJ comes out of the vehicle at the next handover) |
| W / S | Walk forwards / back | Accelerate / brake, then reverse |
| Arrows up / down | Walk forwards / back | Lean a bike forward / back (stoppies, wheelies, clearing bumps) |
| A / D, arrows left / right | Walk left / right | Steer |
| Mouse | Look around | Look around |
| Left Shift | Sprint | |
| Space | Jump | Handbrake |
| C | Crouch (Skyrim sneaks) | |
| Left Alt | Walk | |
| F or Enter | Get into the nearest vehicle | Get out |
| H | | Horn |
| ] | Spawn a Sandking | |
| [ | Spawn a ZR-350 | |
| `\` | Spawn a Sanchez dirt bike | |
| Mouse wheel | | Next or previous radio station (San Andreas's stations, Radio Sovngarde, off) |
| `/` | Dev key: show San Andreas's next text style (mission title, wasted, busted, mission passed, help, subtitles, fade, letterbox, wanted stars, radio station, off) | Same |

A spawned vehicle is parked across CJ's path, on the first flat, clear spot in front of him (a bike on its side stand). Up to four exist at once; another one replaces the oldest one CJ isn't in. Escape, Tab, M and the console stay Skyrim's.

While CJ has the player, San Andreas plays his, his vehicles' and his speech's sounds itself, from where its camera is, at Skyrim's master and effects volume. In a vehicle its radio plays at Skyrim's master and music volume, starting on the vehicle's own station, and the station's name shows top centre. The User Track Player's slot is **Radio Sovngarde**: San Andreas goes quiet and Skyrim plays its own music from its archives, starting with the main theme, then combat tracks in a fight and the time of day's exploration tracks otherwise, with a bard's lute, flute or drum piece after every two. Skyrim's own music stops while any station plays. A pausing Skyrim menu pauses both. San Andreas's world ambience (city noise, weather, police scanner, distant gunfire) stays off.

## How it will work

- An **SKSE plugin** (C++) samples Skyrim's collision around the player, mirrors NPCs, and renders CJ, his vehicles and weapons inside Skyrim from San Andreas's own models, read from your install at play time.
- An **ASI plugin** (C++, [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)) empties San Andreas's map, builds Skyrim's collision there around a floating origin, runs CJ, vehicle and weapon physics on it, and streams poses and events back.
- The two talk over versioned shared memory.

Skyrim's own player actor stays the real player, so dialogue, quests, bounties and kill credit keep working. CJ is the body the player drives.

## Planned features

- **CJ** on foot, with San Andreas's movement and animations.
- **Cars and bikes**: San Andreas's vehicle physics on Skyrim's roads, and running people over.
- **Guns**: San Andreas's weapons doing real damage to Skyrim's NPCs, credited to the player.
- **Cheat codes**: type them in Skyrim as usual, spawn a car and go.
- **Quests**: complete Skyrim's quests as CJ. Anything that needs spells or lockpicking hands the player back to Skyrim.
- **Wanted level**: stars from the hold's bounty and the crimes CJ commits. More stars, more guards, and from 3 stars they chase CJ on horseback. Lose them by staying out of sight, or pay the guard.

## Milestone 1: on the ground

San Andreas walking and driving on Skyrim's ground is the hardest part, so it comes first:

- CJ on foot, on Skyrim terrain, drawn by Skyrim (done).
- Vehicles driven on Skyrim terrain, drawn by Skyrim: a Sandking, a ZR-350 and a Sanchez dirt bike (done).
- No guns, NPCs, quests or wanted level yet.

San Andreas loads collision as static per-sector files, so Skyrim's ground is built there at runtime: collision objects spawned around CJ from the chunks Skyrim streams, under a floating origin (see [docs/DESIGN.md](docs/DESIGN.md#ground-skyrims-collision-in-san-andreas)). Once CJ and his vehicles stand on Skyrim's ground, everything after it is known work.

## Known issues

- While San Andreas has the player, the view is San Andreas's third-person camera only. First person isn't supported yet.
- CJ and the vehicles cast no shadow in Skyrim yet, and CJ always wears his starting clothes (vest, jeans, sneakers).
- Vehicles: damage isn't shown (dents, broken windows and lost doors stay as they were), and lights and exhaust smoke aren't drawn.
- Radio Sovngarde plays the bards' instrumentals, not their sung songs: those are voice lines split by verse, which Skyrim can't play from a file.
- Vehicles live only while San Andreas has the player: they're hidden when Skyrim takes it back, and the next F10 removes them.
- Every surface counts as tarmac for San Andreas: roads, grass and dirt all grip the same. A low sports car like the ZR-350 scrapes on Skyrim's rocks and rough ground (its ground clearance, not a collision bug).
- San Andreas's HUD shows the fist as the weapon, and no armour or wanted level yet (nothing gives CJ those yet).
- Skyrim keeps every vehicle model it was sent until it quits (a few MB each), so spawning dozens of vehicles in one session uses up memory.

## Requirements

Setting them up, step by step: [docs/INSTALL.md](docs/INSTALL.md).

- Windows 10/11 and a PC that can run both games at once (San Andreas is light)
- [The Elder Scrolls V: Skyrim Anniversary Edition](https://store.steampowered.com/app/489830/) (latest Steam version) with [SKSE64](https://skse.silverlock.org) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Grand Theft Auto: San Andreas](https://store.steampowered.com/app/12120/) (Steam), **downgraded to 1.0 US** with a downgrader, plus an ASI loader. One exe version means one set of memory addresses. The Definitive Edition is a different engine and won't work.

## Game assets

This repository contains **no assets from San Andreas or Skyrim**, and contributions must not add any. You need your own copies of both games. Models, sounds and textures are read from your installs at play time, never converted into the release or redistributed.

## License

Not chosen yet (the repository is private for now).

Not affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Bethesda Softworks. Grand Theft Auto, San Andreas and Skyrim are trademarks of their respective owners.
