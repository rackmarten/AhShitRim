# Installing AhShitRim

AhShitRim runs **Skyrim Anniversary Edition** and **GTA: San Andreas** side by side: San Andreas runs hidden and simulates CJ, Skyrim draws him. You need your own copies of both games, and a PC that can run them at once (San Andreas is light). Windows 10 or 11.

The release zip has two parts:

- `AhShitRim-Skyrim-<version>.zip`: the Skyrim part, installed with your mod manager (Mod Organizer 2 or Vortex).
- Everything else: the launcher (`Start AhShitRim.bat`), San Andreas's part (`San Andreas\scripts\AhShitRim.asi`, which the launcher copies into San Andreas for you) and this guide.

## 1. Skyrim

1. Install **The Elder Scrolls V: Skyrim Special Edition** from Steam, with the Anniversary upgrade if you own it, and keep it on the latest Steam version. To stop Steam updating it behind your back: Steam > Skyrim > Properties > Updates > "Only update this game when I launch it".
2. Install **SKSE64** from <https://skse.silverlock.org> (the "Current Anniversary Edition build"): copy `skse64_loader.exe` and the `skse64_*.dll` files next to `SkyrimSE.exe`.
3. Install **Address Library for SKSE Plugins** from <https://www.nexusmods.com/skyrimspecialedition/mods/32444>, the "All in one (Anniversary Edition)" file, with your mod manager.

Already playing PerkeleRim? Then Skyrim is ready as it is.

## 2. San Andreas, downgraded to 1.0 US

AhShitRim runs on exactly one San Andreas exe, **1.0 US**. The Steam copy is a later build, so it's downgraded once. The Definitive Edition is a different game engine and won't work.

1. Install the classic Steam **Grand Theft Auto: San Andreas** (not the Definitive Edition). Steam > Properties > Updates: only update when launched.
2. Copy the whole game folder (`steamapps\common\Grand Theft Auto San Andreas`) somewhere outside Steam, for example `C:\Games\Grand Theft Auto San Andreas`, so Steam never touches the downgraded copy.
3. Download `gtasa-open-downgrader-windows.exe` from <https://github.com/xxanqw/gtasa-open-downgrader/releases/latest> (open source). Run it, point it at the copied folder, check it identifies the Steam build, and downgrade to **1.0 US**.
4. In the same app, install the **ASI Loader** and **SilentPatch**, and apply the 4GB flag. Leave out the other mods (widescreen, frame rate, graphics): AhShitRim isn't tested with them.
5. Start `gta_sa.exe` from that folder once. A new game should load. Quit it.

The folder now has `gta_sa.exe`, the 1.0 US exe. A `gta-sa.exe` may sit next to it: that's the old Steam exe, leave it.

## 3. Install the Skyrim part

### Mod Organizer 2

1. Extract the release zip somewhere of its own, for example `C:\Games\AhShitRim`.
2. In MO2: Install a new mod from an archive (the CD icon), pick `AhShitRim-Skyrim-<version>.zip` from the extracted folder, name it AhShitRim, OK.
3. Tick AhShitRim in the left pane.

### Vortex

1. Extract the release zip somewhere of its own, for example `C:\Games\AhShitRim`.
2. In Vortex: Mods > Install From File, pick `AhShitRim-Skyrim-<version>.zip`, then Enable it and let Vortex deploy.

Either way, AhShitRim has no `.esp`: there's nothing to add to the load order.

## 4. First launch

1. Close both games.
2. Double-click `Start AhShitRim.bat` in the extracted folder.
3. The first time, it finds Skyrim through Steam and asks for the San Andreas folder: pick the downgraded copy from step 2. Both are kept in `launcher.ini` next to it (delete that file to pick them again).
4. It checks Skyrim's version, SKSE, Address Library and AhShitRim's Skyrim part, then that `gta_sa.exe` is 1.0 US and the ASI loader is there. Anything missing is listed in red with where to get it; fix it and run it again.
5. It copies `AhShitRim.asi` into San Andreas's `scripts` folder and tells AhShitRim where San Andreas is (`Data\SKSE\Plugins\AhShitRim.ini` in Skyrim's folder, which it writes).
6. It starts San Andreas, which stays minimized, and waits until it's ready (up to two minutes).
7. Then it starts Skyrim through SKSE. With **Mod Organizer 2**, it starts Skyrim through MO2, so Skyrim sees MO2's mods: it asks MO2 to run the instance's SKSE executable (the one pointing at this Skyrim's `skse64_loader.exe`), whether MO2 is open already or not. The first time, it may ask where `ModOrganizer.exe` is. If the instance has no SKSE executable, it says so and leaves it to you: start **SKSE** from MO2's executables list.

Leave San Andreas minimized while you play. Closing it hands the player back to Skyrim.

## 5. Playing: F10

Load a save (or start a new game) and press **F10**: the player is now CJ, on Skyrim's ground. Press F10 again to hand the player back to Skyrim.

| Key | On foot | In a vehicle |
|---|---|---|
| F10 | Back to Skyrim | Back to Skyrim |
| F11 | Screenshot, HUD and all, to `Screenshots` next to `Start AhShitRim.bat` | Same |
| W A S D, mouse | Walk, look | Drive, look |
| Left Shift / Space / C / Left Alt | Sprint / jump / crouch / walk | Space: handbrake |
| F or Enter | Get into the nearest vehicle | Get out |
| H | | Horn |
| ] / [ / `\` | Spawn a Sandking / a ZR-350 / a Sanchez | |
| Mouse wheel | Next or previous weapon | Next or previous radio station |
| Right / left mouse button | Aim / fire the weapon in CJ's hands | |
| `=` | Give CJ the next set of San Andreas's weapons (a test key, for now) | |

The full list is in the [README](../README.md#keys).

## Troubleshooting

- **"Skyrim ... is a Special Edition runtime"**: AhShitRim needs Anniversary Edition (1.6.317 or later). Let Steam update Skyrim, then update SKSE and Address Library to match.
- **"SKSE64 is for another Skyrim version"** or **"Address Library ... is not installed for Skyrim 1.6.x"**: Skyrim updated. Get the SKSE build and Address Library file for the version named in the message.
- **"AhShitRim's Skyrim part is not installed"**: install `AhShitRim-Skyrim-<version>.zip` with your mod manager (step 3) and enable it. With Vortex, deploy. With MO2, the mod has to be ticked in the profile MO2 has selected (the launcher says so when it isn't). With a portable MO2 install, add `Mo2Path=<the MO2 folder>` to `launcher.ini` so the launcher can find its mods.
- **"A loose AhShitRim.dll is in Skyrim's own Data\SKSE\Plugins as well"** (yellow): an older copy sits in Skyrim's real Data folder, outside any mod manager. The launcher starts Skyrim through MO2 anyway, but that copy loads whenever Skyrim starts without MO2. Delete `AhShitRim.dll` (and `AhShitRim.pdb`) from `<Skyrim>\Data\SKSE\Plugins`; leave `AhShitRim.ini` there, the launcher writes it.
- **The launcher finds the wrong ModOrganizer.exe, or asks for it every time**: set `Mo2Exe=<path to ModOrganizer.exe>` in `launcher.ini`.
- **MO2 says the shortcut is for another instance**: MO2 is open with a different instance. Switch it to the one with AhShitRim, or close MO2, and run the launcher again.
- **"gta_sa.exe is the current Steam version"** (or 1.01, or EU): that's not the 1.0 US exe. Run the downgrader on that folder again (step 2). If Steam updated or verified the copy, the downgrade is undone.
- **"gta_sa.exe isn't one this launcher knows"** (yellow): the launcher goes on. If San Andreas's `scripts\AhShitRim.log` then says its version isn't supported, downgrade again.
- **"San Andreas has no ASI loader"**: install the ASI Loader from the downgrader's mod list.
- **San Andreas doesn't get ready, and there's no `scripts\AhShitRim.log`**: the ASI loader isn't loading the `scripts` folder. Reinstall the ASI loader from the downgrader.
- **San Andreas closes or crashes at start**: another ASI mod is the usual suspect. Only SilentPatch and the ASI loader are tested; move other `.asi` files out of the San Andreas folder and its `scripts` folder.
- **F10 does nothing**: San Andreas isn't running or AhShitRim isn't loaded in Skyrim. Check `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log` for `link up`, and that the launcher got San Andreas ready.
- **No San Andreas HUD in Skyrim**: `Data\SKSE\Plugins\AhShitRim.ini` doesn't point at San Andreas. Run the launcher again, it rewrites it.
- **Skyrim crashes at start, or SKSE says the plugin is incompatible**: the game updated. Update SKSE and Address Library to match.

The logs: Skyrim's in `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log` (the last two runs' beside it as `AhShitRim.1.log` and `AhShitRim.2.log`, so a crash's log survives starting the game again), San Andreas's in its `scripts\AhShitRim.log`.
