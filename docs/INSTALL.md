# Installing AhShitRim

AhShitRim runs **Skyrim** (Anniversary Edition, or Special Edition 1.5.97) and **GTA: San Andreas** side by side: San Andreas runs hidden and simulates CJ, Skyrim draws him. You need your own copies of both games, and a PC that can run them at once (San Andreas is light). Windows 10 or 11.

The release zip has:

- `Start AhShitRim.bat`, the launcher: it sets everything up the first time, checks both games every time, and starts them.
- `Uninstall AhShitRim.bat`, which takes out again what the launcher put into the games.
- `AhShitRim-Skyrim-<version>.zip`, the Skyrim part. The launcher installs it for you, or you install it with Mod Organizer 2 or Vortex.
- `San Andreas\scripts\AhShitRim.asi`, San Andreas's part, which the launcher copies into San Andreas.
- This guide.

## 1. Before you start

1. Install **The Elder Scrolls V: Skyrim Special Edition** from Steam, with the Anniversary upgrade if you own it, and keep it on the latest Steam version. To stop Steam updating it behind your back: Steam > Skyrim > Properties > Updates > "Only update this game when I launch it".
2. Install **SKSE64** from <https://skse.silverlock.org> (the "Current Anniversary Edition build"): open the archive and copy `skse64_loader.exe` and the `skse64_*.dll` files next to `SkyrimSE.exe`.
3. Install **Address Library for SKSE Plugins** from <https://www.nexusmods.com/skyrimspecialedition/mods/32444>, the "All in one (Anniversary Edition)" file: with your mod manager, or without one by copying the archive's `SKSE` folder into Skyrim's `Data` folder.
4. Install the classic Steam **Grand Theft Auto: San Andreas** (not the Definitive Edition, which is a different engine and won't work). Steam > Properties > Updates: only update when launched.

**Staying on Special Edition 1.5.97** (a downgraded copy, or a modlist that needs it)? AhShitRim works there too, with the same files. Take SKSE64's "Special Edition build" (2.0.20, `skse64_1_5_97.dll`) and Address Library's "All in one (Special Edition)" file instead: it has `version-1-5-97-0.bin`, where the Anniversary Edition file has `versionlib-*.bin`. Check the version under `SkyrimSE.exe` > Properties > Details > File version. Older Special Edition versions (1.5.80 and before) aren't supported.

Already playing PerkeleRim? Then Skyrim is ready as it is.

## 2. Setup

1. Extract the release zip into a folder of its own, for example `C:\Games\AhShitRim`, and close both games.
2. Double-click `Start AhShitRim.bat`. It finds Skyrim through Steam (or asks for its folder once).
3. The first time, setup walks you through San Andreas, with a dialog for each step:
   1. It finds your San Andreas (Steam, the Rockstar Games Launcher or a retail install) and asks whether to use it. If it finds none, you pick the folder with `gta_sa.exe` or `gta-sa.exe`.
   2. It offers to **copy** San Andreas into a folder you pick (recommended). Your original install stays as it is, and Steam can never undo the downgrade on the copy. Pick an empty folder, or one to make `San Andreas (AhShitRim)` in, outside Program Files, for example `C:\Games`. The copy needs as much space as San Andreas (about 4.7 GB for Steam's) and takes a few minutes.
   3. San Andreas runs AhShitRim on exactly one exe, **1.0 US**, and Steam's is a later build, so setup offers to download the open-source downgrader, [gtasa-open-downgrader](https://github.com/xxanqw/gtasa-open-downgrader) 0.2.3 (57 MB, from its GitHub release). The launcher checks the file's SHA-256 before running it and keeps it in `Tools` next to the launcher. Then the downgrader opens. In it:
      - choose the copy as the game folder (setup put its path on the clipboard: paste it with Ctrl+V),
      - downgrade it to **1.0 US** (say yes if it asks to download its patches),
      - optionally install **SilentPatch** from its mod list; leave the other mods (widescreen, frame rate, graphics) out, AhShitRim isn't tested with them. The 4GB patch from its tools isn't needed: the launcher applies it (below),
      - close the downgrader. Setup checks that `gta_sa.exe` is now 1.0 US and offers another go if it isn't.
   4. If San Andreas has no ASI loader yet, setup offers to download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) 9.7.4 (0.9 MB, from its GitHub release, SHA-256 checked) and puts its `dinput8.dll` into the copy.
   5. It asks how AhShitRim's Skyrim part goes in, if you have **Mod Organizer 2** or **Vortex** for this Skyrim. With neither, the launcher installs it itself (section 3).
4. Setup keeps the folders and your choice in `launcher.ini` next to the launcher. Delete that file to run setup again.

Downloads only ever come from those two projects' own GitHub releases, pinned to a version and a SHA-256; a file that doesn't match is deleted, never run. VirusTotal's reports on those exact files: [gtasa-open-downgrader 0.2.3](https://www.virustotal.com/gui/file/b9868a87fb93ab5d8ec6ea75d16dabe51010623e4291c627d271bc87cbd89637), [Ultimate ASI Loader 9.7.4](https://www.virustotal.com/gui/file/14b3a1ad018899571ac9aa01482977f3c6d49e6cba99f552d01c5acacd1315e1). The download dialog shows the link too.

### Check your download

Every release zip is built by GitHub Actions from the release's tag, never on someone's PC, and its release notes say how to check it:

- **SHA-256**: `Get-FileHash AhShitRim-<version>.zip` in PowerShell should print the hash in the notes. `SHA256SUMS.txt` next to the zip lists every file in it.
- **Build provenance**: with the [GitHub CLI](https://cli.github.com), `gh attestation verify AhShitRim-<version>.zip --repo rackmarten/AhShitRim` checks GitHub's signature that the zip came from this repository's release workflow at that tag.
- **VirusTotal**: the notes link the zip's scan. AhShitRim's plugins hook into both games' code, which a few heuristic engines can mistake for malware; the source of every file is in this repository.

## 3. The Skyrim part

**Without a mod manager** (or when you answered No to installing it with yours): the launcher copies `AhShitRim-Skyrim-<version>.zip` straight into Skyrim's `Data` folder, and writes down every file it copied in `Data\SKSE\Plugins\AhShitRim.installed.txt`. A new release's zip next to the launcher is installed over it the next time you start, and `Uninstall AhShitRim.bat` removes exactly those files again. A loose `AhShitRim.dll` already in `Data` from a hand install or an older release is installed over, so it can't stay a version that doesn't match San Andreas's part.

**With Mod Organizer 2:** in MO2, Install a new mod from an archive (the CD icon), pick `AhShitRim-Skyrim-<version>.zip` from the extracted folder, name it AhShitRim, OK, and tick it in the left pane.

**With Vortex:** Mods > Install From File, pick `AhShitRim-Skyrim-<version>.zip`, then Enable it and let Vortex deploy.

Either way, AhShitRim has no `.esp`: there's nothing to add to the load order.

## 4. Every launch

1. Close both games and double-click `Start AhShitRim.bat`.
2. It checks Skyrim's version, SKSE, Address Library and AhShitRim's Skyrim part, then that `gta_sa.exe` is 1.0 US and the ASI loader is there. If Steam undid the downgrade or the ASI loader is gone, it offers setup's step again. It also applies the downgrader's **4GB patch** (Large Address Aware) to the plain 1.0 US exe, as AhShitRim is tested with it: only that one bit in the exe, and only when the result is the known 1.0 US exe with the 4GB flag. Anything else missing is listed in red with where to get it, and for SKSE or Address Library it offers to open their download pages; fix it and run it again.
3. It copies `AhShitRim.asi` into San Andreas's `scripts` folder and tells AhShitRim where San Andreas is (`Data\SKSE\Plugins\AhShitRim.ini` in Skyrim's folder, which it writes; F11 screenshots go to `Screenshots` next to the launcher).
   It also paints the radar's San Andreas-style map of Skyrim from your own plugins into `Documents\My Games\AhShitRim` (a few seconds the first time, then again only when your plugins change).
4. It starts San Andreas, which stays minimized, and waits until it's ready (up to two minutes).
5. Then it starts Skyrim through SKSE. With **Mod Organizer 2**, it starts Skyrim through MO2, so Skyrim sees MO2's mods: it asks MO2 to run the instance's SKSE executable (the one pointing at this Skyrim's `skse64_loader.exe`), whether MO2 is open already or not. The first time, it may ask where `ModOrganizer.exe` is. If the instance has no SKSE executable, it says so and leaves it to you: start **SKSE** from MO2's executables list.

Leave San Andreas minimized while you play. Closing it hands the player back to Skyrim.

## Uninstall

Close both games and double-click `Uninstall AhShitRim.bat`. It removes the Skyrim part the launcher installed (from its record), `Data\SKSE\Plugins\AhShitRim.ini` and San Andreas's `scripts\AhShitRim.asi`, and leaves your saves alone. With Mod Organizer 2 or Vortex, remove the AhShitRim mod there as well. The San Andreas copy is a folder of its own: delete it when you don't need it any more. The radar's map is in `Documents\My Games\AhShitRim`.

## Setting up San Andreas by hand

If you'd rather not use setup (or it can't do it for you):

1. Copy the whole game folder (`steamapps\common\Grand Theft Auto San Andreas`) somewhere outside Steam and Program Files, for example `C:\Games\Grand Theft Auto San Andreas`, so Steam never touches the downgraded copy.
2. Download `gtasa-open-downgrader-windows.exe` from <https://github.com/xxanqw/gtasa-open-downgrader/releases/latest> (open source). Run it, point it at the copied folder, check it identifies the Steam build, and downgrade to **1.0 US**.
3. In the same app, install the **ASI Loader** and **SilentPatch**, and apply the 4GB flag. Leave out the other mods.
4. Start `gta_sa.exe` from that folder once. A new game should load. Quit it.
5. Run `Start AhShitRim.bat`. When setup offers the Steam install, answer No and pick the copy: it sees a 1.0 US San Andreas and skips the copy and the downgrade. Or put `SaPath=<the copy's folder>` in `launcher.ini` yourself before the first start.

The folder now has `gta_sa.exe`, the 1.0 US exe. A `gta-sa.exe` may sit next to it: that's the old Steam exe, leave it.

## Playing: F10

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

- **Setup can't download the downgrader or the ASI loader, or says a file "isn't the file AhShitRim expects"**: the download failed or was changed on the way, so nothing was run. Check your internet connection (and that nothing blocks github.com) and run the launcher again, or set San Andreas up by hand (above).
- **The downgrader doesn't offer a 1.0 US downgrade for your San Andreas**: it patches Steam's current build. A Rockstar Games Launcher or retail install may be another build; see the downgrader's page for what it supports, or use the Steam version.
- **Setup won't copy into a folder**: it refuses Program Files (Windows keeps it read-only for games), a folder that already has files in it, a folder inside the original install, and a drive with too little room. Pick another one.
- **"Skyrim ... is too old"**: AhShitRim needs Special Edition 1.5.97 or Anniversary Edition (1.6.317 or later). Let Steam update Skyrim, then update SKSE and Address Library to match.
- **"SKSE64 is for another Skyrim version"** or **"Address Library ... is not installed for Skyrim 1.x"**: Skyrim updated, or SKSE and Address Library are for the other edition. Get the SKSE build and Address Library file for the version named in the message: on 1.5.97 the Special Edition ones (`skse64_1_5_97.dll`, `version-1-5-97-0.bin`), on 1.6 and later the Anniversary Edition ones (`versionlib-*.bin`).
- **"AhShitRim's Skyrim part is not installed"**: install `AhShitRim-Skyrim-<version>.zip` with your mod manager (step 3) and enable it. With Vortex, deploy. With MO2, the mod has to be ticked in the profile MO2 has selected (the launcher says so when it isn't). With a portable MO2 install, add `Mo2Path=<the MO2 folder>` to `launcher.ini` so the launcher can find its mods. To have the launcher install it instead, set `ModManager=None` in `launcher.ini`.
- **"A loose AhShitRim.dll is in Skyrim's own Data\SKSE\Plugins as well"** (yellow): an older copy sits in Skyrim's real Data folder, outside any mod manager. The launcher starts Skyrim through MO2 anyway, but that copy loads whenever Skyrim starts without MO2. Run `Uninstall AhShitRim.bat` if the launcher installed it, or delete `AhShitRim.dll` (and `AhShitRim.pdb`) from `<Skyrim>\Data\SKSE\Plugins`; leave `AhShitRim.ini` there, the launcher writes it.
- **The launcher finds the wrong ModOrganizer.exe, or asks for it every time**: set `Mo2Exe=<path to ModOrganizer.exe>` in `launcher.ini`.
- **MO2 says the shortcut is for another instance**: MO2 is open with a different instance. Switch it to the one with AhShitRim, or close MO2, and run the launcher again.
- **"gta_sa.exe is the current Steam version"** (or 1.01, or EU): that's not the 1.0 US exe. Let the launcher run the downgrader again. If Steam updated or verified the folder, the downgrade is undone: that's why setup copies it.
- **"gta_sa.exe isn't one this launcher knows"** (yellow): the launcher goes on. If San Andreas's `scripts\AhShitRim.log` then says its version isn't supported, downgrade again.
- **"San Andreas has no ASI loader"**: let the launcher install it when it offers, or install the ASI Loader from the downgrader's mod list.
- **San Andreas doesn't get ready, and there's no `scripts\AhShitRim.log`**: the ASI loader isn't loading the `scripts` folder. Reinstall the ASI loader from the downgrader.
- **San Andreas closes or crashes at start**: another ASI mod is the usual suspect. Only SilentPatch and the ASI loader are tested; move other `.asi` files out of the San Andreas folder and its `scripts` folder.
- **"The Skyrim plugin ... is AhShitRim X (link version N), but San Andreas's ... is ..."**: the two halves come from different releases (often a leftover from an older install) and can't talk to each other. Replace the file the message names with the one from the same release zip as the other, then run the launcher again.
- **In Skyrim: "AhShitRim: San Andreas has version N, Skyrim has version M. Install the same version in both"**: the same, found while playing. Update the file it names (San Andreas's `scripts\AhShitRim.asi`, or the Skyrim plugin `AhShitRim.dll`) and restart only that game: the other one links up by itself once both match. San Andreas's `scripts\AhShitRim.log` says `protocol mismatch` meanwhile.
- **F10 does nothing**: San Andreas isn't running or AhShitRim isn't loaded in Skyrim. Check `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log` for `link up`, and that the launcher got San Andreas ready.
- **No San Andreas HUD in Skyrim**: `Data\SKSE\Plugins\AhShitRim.ini` doesn't point at San Andreas. Run the launcher again, it rewrites it.
- **Both games go silent and stay silent**: if your sound comes from the monitor's speakers (HDMI or DisplayPort audio), the display turning off takes that audio device with it, and neither game reconnects when it comes back; restart both games to get sound again. While San Andreas has the player, AhShitRim keeps the display on, but with Skyrim in control (or the screen locked) Windows's display timeout still applies. Headphones or speakers on the PC's own audio jack don't go away, or set "Turn off my screen" in Windows's power settings to Never while you play.
- **Skyrim crashes at start, or SKSE says the plugin is incompatible**: the game updated. Update SKSE and Address Library to match.

The logs: Skyrim's in `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log` (the last two runs' beside it as `AhShitRim.1.log` and `AhShitRim.2.log`, so a crash's log survives starting the game again), San Andreas's in its `scripts\AhShitRim.log`.
