# AhShitRim

> **Thane of Grove Street.** San Andreas in Skyrim.

**GTA: San Andreas** and **Skyrim Anniversary Edition** run at the same time. Skyrim owns the world and draws everything; San Andreas runs hidden and simulates CJ, his cars, his guns and his cheat codes. Hijack a cart outside Helgen, do a drive-by on the Thalmor, and outrun Whiterun's guards with five stars on your head.

A sibling of [PerkeleRim](https://github.com/rackmarten/PerkeleRim) (My Summer Car in Skyrim), built the same way.

## Status

**Early development.** San Andreas runs hidden as a simulator of CJ: F10 in Skyrim hands the player to it, Skyrim's WASD and mouse drive CJ on Skyrim's own terrain and statics, rebuilt as collision around him in San Andreas, and his movement moves the Skyrim player. Nothing from San Andreas is drawn in Skyrim yet. The first milestone tackles the hardest part first (see [Milestone 1: on the ground](#milestone-1-on-the-ground)).

Building it yourself: [docs/DEV_SETUP.md](docs/DEV_SETUP.md). Design: [docs/DESIGN.md](docs/DESIGN.md). The link between the games: [docs/PROTOCOL.md](docs/PROTOCOL.md). Where it's going: [docs/ROADMAP.md](docs/ROADMAP.md).

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

- CJ on foot, on Skyrim terrain, drawn by Skyrim.
- One car, driven on Skyrim terrain, drawn by Skyrim.
- No guns, NPCs, quests or wanted level yet.

San Andreas loads collision as static per-sector files, so Skyrim's ground is built there at runtime: collision objects spawned around CJ from the chunks Skyrim streams, under a floating origin (see [docs/DESIGN.md](docs/DESIGN.md#ground-skyrims-collision-in-san-andreas)). Once CJ and the car stand on Skyrim's ground, everything after it is known work.

## Requirements (planned)

- Windows 10/11 and a PC that can run both games at once (San Andreas is light)
- [The Elder Scrolls V: Skyrim Anniversary Edition](https://store.steampowered.com/app/489830/) (latest Steam version) with [SKSE64](https://skse.silverlock.org) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Grand Theft Auto: San Andreas](https://store.steampowered.com/app/12120/) (Steam), **downgraded to 1.0 US** with a downgrader, plus an ASI loader. One exe version means one set of memory addresses. The Definitive Edition is a different engine and won't work.

## Game assets

This repository contains **no assets from San Andreas or Skyrim**, and contributions must not add any. You need your own copies of both games. Models, sounds and textures are read from your installs at play time, never converted into the release or redistributed.

## License

Not chosen yet (the repository is private for now).

Not affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Bethesda Softworks. Grand Theft Auto, San Andreas and Skyrim are trademarks of their respective owners.
