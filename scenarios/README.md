# In-game tests

AhShitRim's in-game tests are [skse-test-kit](https://github.com/rackmarten/skse-test-kit) scenario files: steps
(the kit's commands and AhShitRim's `asr.*`) with checks, run against a hidden test session that never takes the
foreground. How to set the kit up is in `docs/DEV_SETUP.md`, "Test sessions"; the commands in the kit's
`docs/COMMANDS.md` and in `scripts/skytest.ps1 cmd help`.

```powershell
scripts/skytest.ps1 session                      # once per build: Riverwood, CJ there, San Andreas beside it (~20 s)
scripts/skytest.ps1 run scenarios/hits.json      # any file here; results in out/<time>-<name>/
scripts/skytest.ps1 stop                         # when done: closes both games, releases the games lock
```

`run` exits 0 when every check passed, 1 when one failed, 2 when Skyrim crashed, exited or hung (the reason and the
crash log in its last line and in `summary.json`'s `Fault`). Each run writes `results.csv` (a row per check),
`step-<n>-rows.csv` for the steps that report rows, `responses.jsonl` and any screenshots.

| File | What it checks | Rows | Time |
|---|---|---|---|
| `hits.json` | Skyrim's attacks on CJ's vehicle (v31): CJ in a Sandking in Riverwood, a giant and two guards set on him for 90 s; the car takes the damage (`carLost > 0`, `scripts/hit-test.ps1` prints this run's `car hits:` lines). The car usually burns and blows up, and CJ is Wasted (and wakes at a temple). `scripts/hit-test.ps1` runs it | the car's and the player's health, in the car, wasted, busted, every 2 s | 100 s |
| `bust.json` | The bust at the door (v31): two stars on CJ in a stopped Sandking; a guard runs to the driver's door and busts him (`busted == true`) | the same, for 60 s | 80 s |
| `mission.json` | Quests as missions (v24): CJ's intro line, a quest's title (Bleak Falls Barrow, `MQ103`) and "mission passed" with its tune, first while CJ has the player, then while Skyrim has it; four screenshots with the HUD | | 33 s |
| `stream.json` | Skyrim's streaming at a Hydra's 87 m/s (v19): the player flown 3.3 km north-east from Riverwood, 30 m up; no frame over 50 ms, and the land under it and 1 s ahead always loaded with collision | per second: frames, the worst frame, land loaded under, 1 s and 2 s ahead | 62 s |
| `test-kit-smoke.json` | AhShitRim's kit commands themselves: handovers, the night, a weapon, a spawned Elegy, a tracked second, exact steps | CJ, a row a frame | 6 s |
| `muzzle-flash.json` | The muzzle flash (v12) at the muzzle: CJ fires a pistol, a shotgun, an SMG and an M4 at night; in the flash's frame the flash against the drawn gun's barrel end (`along` within 8 units, `across` under 6), paused on it for a close shot and a wider one, its light probed; a single Tec-9 and pistol aimed, leaning in (`weapon.roll` -10 to -60 degrees); with Hitman skill (NCSGDAG) twin pistols leaning in toward each other and twin sawn-offs, each flash on its own gun; then the same after a trip into an interior and back (the light still among the renderer's) | | 38 s |
| `vehicle-lights.json` | The headlights and tail lights (v36; needs them): a ZR-350 at 23:00 with CJ in it, its four lamps lit and both scene lights on, A/B pairs with `asr.lamps off` (each tail lens 15 darker, the road behind 1.5, the front lamps 10, the road ahead 5); out by day; out when blown up (CJ out and well away first); still lighting after an interior and back | | 55 s |
| `crew-follow.json` | The Grove Street crew following CJ (v39; needs it): at the Whiterun stables, three homies recruited and recorded at 10 Hz on a set route (past a fence's end, round the stalls, through a narrow gap by the cliff and back, a sprint down the road); per homie: moved, never stuck 10 s, within 1000 of CJ's path, within 900 of CJ 10 s after he stops; CJ never held up. `stables-crew-overlay.png` draws every track | `stables-crew.csv`: a row per sample and track | 67 s |

Every scenario starts by putting the session where it needs it (a `coc Riverwood`, Skyrim's player healed, most with a
new handover), so they run in any order in one session. Times are on the test desktop, from a running session. The
last two check features that came after the kit (the headlights, the crew): on a build without them their first
step that needs them fails, saying so. Their limits are in each file's opening comment, with what they were set from.
`crew-follow.json` leaves its three homies following CJ: run it last, or `skytest stop` before the others.

Until those features are on `main`, run their scenarios on a build of `main` merged with the pull request that brings
them, in a worktree of your own: the headlights are PR 85 (`add-headlights-and-oil`, protocol v36), the crew PR 100
(`add-grove-street-crew-1`, v39). Neither merges cleanly into `main` as it is (the hit test, `skse/CMakeLists.txt` and
the docs conflict): resolve that in the worktree, `scripts/build.ps1`, and `skytest.ps1 session` starts again for the
new build. Never push such a merge.

Known failures (13 November 2026): `muzzle-flash.json` and `vehicle-lights.json` fail their last step on every build,
"after an interior": a light AhShitRim adds to Skyrim's renderer once (the flash's, the vehicles') is no longer among
its lights after a trip into an interior and back. That's a bug in the mod, which those steps were written to catch;
every step before them passes.

## Starts and saves

The default session (`skytest.psd1`) is `coc:Riverwood`: a new character on Riverwood's road, no save needed, and
CJ given the player once San Andreas has it (`[Test] CJ=1`). Every file above runs from it. Other starts:
`scripts/skytest.ps1 session -Start save:<name>` (a save in Skyrim's `Saves` folder, `save:latest` the most recent),
`cow:<worldspace> <x> <y>`, `newgame` (Helgen's cart), `menu`.

A death in a test session isn't a test's end: CJ is Wasted while San Andreas has the player, as in play. Should the
Skyrim player die, though, Skyrim loads the most recent save, which may be someone else's game; `asr.hits` fails
when a game loads halfway, and every scenario here keeps Skyrim's player out of harm's way.

## The load test

`scripts/load-test.ps1` isn't a file here: it runs hundreds of cycles of `asr.load <save> <scenario>` (a load through
the console, the game settled, the player handed to San Andreas, one of `idle`, `flee`, `chase`, `asnaeb`, `travel`,
`aezakmi`, `handback`) and starts the session again after each crash. It needs two saves, `StressA` and `StressB`
(copies of any two saves, `.ess` and `.skse`), in Skyrim's `Saves` folder; `-Saves` names others. Results go to
`Documents/My Games/AhShitRim/`: `loadtest-results.csv` (a line per crash, hang or stuck cycle, with the plugin's last
step) and `loadtest.csv` (every cycle's steps). It exits 1 when any cycle crashed, hung or got stuck. `StressA` on the
test desktop loads into a fight its player loses within seconds (nothing to do with AhShitRim): such a save shows up as
cycles stuck at the main menu.

## Writing one

A scenario file is JSON with comments: `name`, then `steps`, each a request (shorthand like `"wait frames=30"`, or an
object with `cmd`, its arguments, a `label` and `expect` checks such as `"carLost > 0"` or `"shown contains
passed"`). AhShitRim's commands are in `docs/DEV_SETUP.md`'s table and `skse/src/debug/TestKit.cpp`; a long test is an
`AddScenario` there whose game logic lives in `skse/src/debug/` (game-free parts in `protocol/cpp/`). Shot sets
(scenario files whose point is their screenshots) go in `shots/` instead.
