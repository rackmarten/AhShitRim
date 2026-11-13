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
| `hits.json` | Skyrim's attacks on CJ's vehicle (v31): CJ in a Sandking in Riverwood, a giant and two guards set on him for 90 s; the car takes the damage (`carLost > 0`) and Skyrim logs its hits on the car. The car usually burns and blows up, and CJ is Wasted (and wakes at a temple). `scripts/hit-test.ps1` runs it | the car's and the player's health, in the car, wasted, busted, every 2 s | 100 s |
| `bust.json` | The bust at the door (v31): two stars on CJ in a stopped Sandking; a guard runs to the driver's door and busts him (`busted == true`) | the same, for 60 s | 72 s |
| `mission.json` | Quests as missions (v24): CJ's intro line, a quest's title (Bleak Falls Barrow, `MQ103`) and "mission passed" with its tune, first while CJ has the player, then while Skyrim has it; four screenshots with the HUD | | 25 s |
| `stream.json` | Skyrim's streaming at a Hydra's 87 m/s (v19): the player flown 3.3 km north-east from Riverwood, 30 m up; 60 fps throughout, and the land under it and 1 s ahead always loaded with collision | per second: frames, the worst frame, land loaded under, 1 s and 2 s ahead | 58 s |
| `test-kit-smoke.json` | AhShitRim's kit commands themselves: handovers, the night, a weapon, a spawned Elegy, a tracked second, exact steps | CJ, a row a frame | 6 s |

Every scenario starts by putting the session where it needs it (most with a `coc Riverwood` and a new handover), so
they run in any order in one session. Times are on the test desktop, from a running session.

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
step) and `loadtest.csv` (every cycle's steps).

## Writing one

A scenario file is JSON with comments: `name`, then `steps`, each a request (shorthand like `"wait frames=30"`, or an
object with `cmd`, its arguments, a `label` and `expect` checks such as `"carLost > 0"` or `"shown contains
passed"`). AhShitRim's commands are in `docs/DEV_SETUP.md`'s table and `skse/src/debug/TestKit.cpp`; a long test is an
`AddScenario` there whose game logic lives in `skse/src/debug/` (game-free parts in `protocol/cpp/`). Shot sets
(scenario files whose point is their screenshots) go in `shots/` instead.
