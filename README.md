# AhShitRim

> **Thane of Grove Street.** San Andreas in Skyrim.

**GTA: San Andreas** and **Skyrim Anniversary Edition** run at the same time. Skyrim owns the world and draws everything; San Andreas runs hidden and simulates CJ, his cars, his guns and his cheat codes. Hijack a cart outside Helgen, do a drive-by on the Thalmor, and outrun Whiterun's guards with five stars on your head.

A sibling of [PerkeleRim](https://github.com/rackmarten/PerkeleRim) (My Summer Car in Skyrim), built the same way.

## Status

**Early development.** San Andreas runs hidden as a simulator of CJ: F10 in Skyrim hands the player to it, Skyrim's keyboard and mouse drive CJ on Skyrim's own terrain and statics, rebuilt as collision around him in San Andreas, and Skyrim draws CJ there from San Andreas's own model, animated by San Andreas, seen through San Andreas's own camera. CJ can spawn a Sandking, a ZR-350 or a Sanchez in front of him and drive or ride it on Skyrim's roads, the vehicle drawn from San Andreas's own model too. He opens doors, pulls levers, searches chests, picks things up, talks to people and sits down with E, shouts with Skyrim's shout key (the Thu'um in his own words, see [Shouts as CJ](#shouts-as-cj)), stays the player through load doors, interiors and fast travel, follows Skyrim's player through scenes that take it over, takes his damage out of Skyrim's health (when it runs out that is Wasted, and he wakes in a temple), and survives saving and loading, in his car if he was in one. Meanwhile Skyrim shows San Andreas's HUD (clock, health, money) and text styles, in San Andreas's own fonts. The first milestone tackles the hardest part first (see [Milestone 1: on the ground](#milestone-1-on-the-ground)). CJ carries San Andreas's weapons: the mouse wheel switches them, the right button aims (its lock-on, free aim and the sniper, rocket and camera sights), the left one fires, each drawn in his hand from its own model with its muzzle flash, icon and ammo in San Andreas's HUD, and the loadout is in the save. They hurt Skyrim's people and creatures, hit tested in Skyrim: headshots count, damage scales with the target's level and size (the minigun is for dragons), the player gets the kill and the bounty, and San Andreas's weapon skills grow with every hit and are saved. Rockets and grenades blow up as Skyrim explosions that throw people around and open locks nearby (a crime if anyone sees), Molotovs leave burning oil that spreads, tear gas chokes, and the flamethrower sets people on fire, all with San Andreas's own sounds and camera shake; CJ gets caught in his own blasts and burns as he does in San Andreas. Satchel charges stick to whatever they land on, mammoths and dragons included, and ride along until the detonator blows them, and the heat seeker locks on to dragons, horses, giants, mammoths and centurions with San Andreas's marker and chases them. San Andreas's cheat codes work too: type them while CJ has the player (see [Cheat codes](#cheat-codes)). Guns come without cheats: the blacksmiths of the hold capitals (Warmaiden's in Whiterun first) keep an Ammu-Nation that sells every weapon, its ammo and body armour for gold, and the main quest up to the Greybeards and the bosses on the way give guns. CJ loots without menus: walking over a body takes its gold and quest items, and so does E on a chest. Quests start and end like San Andreas missions: a quest's name comes up as a mission title, and finishing one is MISSION PASSED with San Andreas's tune; a misc quest done (a favour, a delivery) is an odd job, MISSION PASSED without the title or RESPECT + (see [Settings](#settings)). A new game opens with the line AhShitRim is named after: right after Ralof wakes you in the Helgen cart, CJ says "Ah shit, here we go again."

Installing and playing it: [docs/INSTALL.md](docs/INSTALL.md). Cheat codes: [docs/CHEATS.md](docs/CHEATS.md). Building it yourself: [docs/DEV_SETUP.md](docs/DEV_SETUP.md). Design: [docs/DESIGN.md](docs/DESIGN.md). The link between the games: [docs/PROTOCOL.md](docs/PROTOCOL.md). Where it's going: [docs/ROADMAP.md](docs/ROADMAP.md).

## Keys

While San Andreas has the player (F10), the keys follow San Andreas's keyboard layout, modern by default. `KeyLayout=Classic` under `[Input]` in `Data/SKSE/Plugins/AhShitRim.ini` switches to San Andreas's own PC defaults (the keys in bold differ):

| Key (modern) | Key (classic) | On foot | In a vehicle |
|---|---|---|---|
| F10 | F10 | Hand the player back to Skyrim | Hand the player back to Skyrim (CJ comes out of the vehicle at the next handover) |
| F11 | F11 | Screenshot with the HUDs on it, to `Screenshots` next to `Start AhShitRim.bat` (also while Skyrim has the player) | Same |
| W / S | W / S | Walk forwards / back | Accelerate / brake, then reverse; both together a burnout (A / D turn the car on the spot) |
| Arrows up / down | Arrows up / down | Walk forwards / back | Lean a bike forward / back (stoppies, wheelies, clearing bumps); pitch an aircraft |
| A / D, arrows left / right | A / D, arrows left / right | Walk left / right | Steer |
| Mouse | Mouse | Look around | Look around |
| Left Shift | **Space** | Sprint (mash it for San Andreas's faster sprint) | |
| Space | **Left Shift or Right Ctrl** | Jump | |
| Space | **Space or Right Ctrl** | | Handbrake; in the Hydra, held, its lock-on (see the left and right mouse buttons) |
| Left Ctrl | **C** | Crouch (Skyrim sneaks) | |
| Left Alt | Left Alt | Walk | |
| F or Enter | F or Enter | Get into the nearest vehicle | Get out |
| H or Caps Lock | Caps Lock or H | | Horn; with hydraulics (LOWRIDER), raise and lower the car; a police helicopter's searchlight |
| Q / E | Q / E | E: activate what CJ faces, through Skyrim (open a door, pull a lever, pick up an item, talk, sit down); the help box top left says what ("E: Open Breezehome"). On a body or an unlocked chest it takes the gold and quest items, no menu; on a blacksmith with an Ammu-Nation it opens the shop | Look left / right (both together: look behind); an aircraft's rudder |
| Numpad 8 / 2 (or Page Up / Page Down), numpad 4 / 6 | Same | | San Andreas's turret keys: the Hydra's nozzles between hovering and forward flight; with hydraulics (LOWRIDER), held with numpad 5, they bounce the car |
| Numpad 5, 2 or numpad + | Same | | San Andreas's sub-mission key (R3): a plane's landing gear up and down (the Hydra's too, in the air), the crop duster's and Stuntplane's smoke, the hydraulics' jump with the numpad's arrows |
| Skyrim's Shout key (Z) | Skyrim's Shout key (Z) | Shout, as in Skyrim (hold for more words), from where CJ stands, where the camera looks; CJ shoves the air and shouts one of his own lines instead of the dragon words | Skyrim's shout and CJ's line, no gesture |
| ] | ] | Spawn a Sandking | |
| [ | [ | Spawn a ZR-350 | |
| `\` | `\` | Spawn a Sanchez dirt bike | |
| `;` | `;` | Dev key: pick the next vehicle for `'` (NRG-500, BMX, Quad, Monster, Rhino, Hunter, Hydra, Sparrow, Maverick, Dodo, Stuntplane, Vortex, the jetpack, the parachute); the help box names it. `[Debug] Vehicles=` in `AhShitRim.ini` sets your own list of model ids, `[Debug] VehicleKeys=0` turns both keys off; they go once cheats exist | Same |
| `'` | `'` | Dev key: spawn the pick (the jetpack goes on CJ's back, the parachute into his hands). Boats, trains, trailers and RC vehicles are refused, and the help box says why | |
| Mouse wheel | Mouse wheel | Next or previous weapon (San Andreas's slots) | Next or previous radio station (San Andreas's stations, Radio Sovngarde, Sweetroll FM, off) |
| Insert or 4 / Delete or R | Same | | Next / previous radio station |
| Right mouse button | Right mouse button | Aim: San Andreas's lock-on and free aim; the sniper rifle, rocket launchers, M4 and camera go to their own first-person sights. With fists or a melee weapon, locks on to the nearest enemy in front and turns CJ to face them; with a gun, locks on to whoever the camera looks at and shoots them (the head when the crosshair is on it). The marker goes from green to red with their health | The vehicle's second weapon (San Andreas's secondary fire, L1): the Hunter's machine gun, the Hydra's missiles (heat-seeking once Space has held a dragon, mammoth, giant, centurion or rider in view until its marker turns red), the Rustler's and Seasparrow's guns; on a BMX or mountain bike the bunny hop (hold to crouch, let go to jump) |
| Left Ctrl or numpad 0 | Left Ctrl or numpad 0 | Left Ctrl: crouch (modern layout) | Same as the right mouse button |
| Left mouse button | Left mouse button | Fire (a melee weapon attacks, turning CJ to the nearest enemy in reach in front); with the parachute in the air, open it | The vehicle's weapon (San Andreas's fire): the Hunter's rockets, the Hydra's flares, the Rhino's cannon (its turret follows the camera, either mouse button fires it); a car's nitro |
| Left Alt or Right Ctrl | Left Alt or **Right Ctrl** | Left Alt: walk (classic: Right Ctrl also jumps) | Same as the left mouse button (in the classic layout Right Ctrl is the handbrake too, as San Andreas binds it) |
| X | X | Insult: CJ shouts one of his insults at whoever's about and gives the finger | Insult out of the window (no finger) |
| Middle mouse button | Middle mouse button | Swear: CJ swears to himself ("oh shit") | Look behind |
| V or Home | V or Home | | Change the camera distance (San Andreas's camera key) |
| W / S, arrows up / down | W / S, arrows up / down | In Ammu-Nation: browse | |
| E, Left Shift | E, **Space** | In Ammu-Nation: choose a category, buy | |
| F or Enter | F or Enter | In Ammu-Nation: back, then leave | |
| `=` | `=` | Dev key: give CJ San Andreas's next weapon set (1 to 3, the gear they leave out, then each melee weapon and gift one at a time). Off with `[Debug]` `WeaponKey=0` in `AhShitRim.ini`; it goes once cheats exist | Same |
| `/` | `/` | Dev key: show San Andreas's next text style (mission title, wasted, busted, mission passed, help, subtitles, fade, letterbox, wanted stars, radio station, off) | Same |

Walking over a dead body takes its gold and quest items, as does E on a body or an unlocked chest; the gold counts up in the money display with San Andreas's money pickup sound, and quest items show in the help box. Everything else stays where it is: Skyrim's inventory, barter and container menus don't open while San Andreas has the player, so press F10 for them.

Ammu-Nation is under the counter at the blacksmiths: Adrianne Avenicci and Ulfberth War-Bear at Warmaiden's in Whiterun, Beirand in Solitude, Oengul War-Anvil in Windhelm, Balimund in Riften, Ghorza gra-Bagol in Markarth, Rustleif in Dawnstar and Lod in Falkreath. E on one opens San Andreas's shop menu (instead of talking: F10 to talk to them as Skyrim's player). Everything is for sale from the start, paid in gold: pistols from 150, shotguns, sub-machine guns, assault rifles, rifles, heavy weapons (a rocket launcher is 6,000, the minigun 15,000), thrown weapons and body armour (200). A gun CJ has sells its ammo instead. The main quest gives guns too, up to the Greybeards (a rocket launcher), and so do the bosses of Embershard Mine, Bleak Falls Barrow, Halted Stream Camp and Silent Moons Camp.

The people CJ kills don't stay dead if they're anyone in particular. Every named NPC (the blacksmiths, the Jarl, Farengar, Delphine, the Greybeards, anyone unique) gets up again an in-game hour later, back where they belong, once their body and their spot are both out of CJ's sight. NOMERCY lets you kill the essential ones, and they come back too, but a quest that failed when they died stays failed. Only CJ's kills: what Skyrim's own player or a dragon kills stays dead.

A spawned vehicle is parked across CJ's path, on the first flat, clear spot in front of him (a bike on its side stand). Any of San Andreas's land and air vehicles can come: bikes and bicycles, quads, monster trucks, the Rhino, helicopters and planes. In a helicopter or a plane W and S are the throttle, A and D roll, the arrows pitch and Q and E the rudder, as in San Andreas. On the jetpack Shift goes up and Space down (Space and Shift in the classic layout), F takes it off standing still. Carrying the parachute, CJ takes it out and skydives off anything high enough: W dives, S slows, A and D turn, the left mouse button opens the chute, and it's used up once he lands.

**The Hydra is experimental.** Skyrim's cell streaming keeps up with it over land (measured with `[Debug] StreamTest=87`: 3.4 km from Riverwood in 39 s at a steady 60 fps, no frame over 50 ms), but Skyrim only has collision for its land about a second ahead at that speed (88 m; 175 m out never was), so flying low and fast you can outrun the ground San Andreas builds and fly through hills. Out over the Sea of Ghosts, past the north coast, Skyrim dropped to about 5 fps. Fly high, or slow down to land. Up to four exist at once; another one replaces the oldest one CJ isn't in. Escape, Tab and the console stay Skyrim's. M doesn't open Skyrim's map while CJ has the player (it's a letter of the cheat codes): the map is in Tab's menu, as San Andreas's is in its pause menu.

CJ stays the player when the game moves it: through a load door (on foot only), into an interior and back out, by fast travel or a script. San Andreas rebuilds his ground where the player arrives and holds him there until it's in. Fast travelling in a vehicle leaves the vehicle behind.

While someone talks to the player, CJ stands still facing them, and the conversation plays like a San Andreas cutscene: letterbox bars, a two-shot of CJ and the speaker, then the speaker over CJ's shoulder, and Skyrim's subtitles in San Andreas's style. CJ greets them, answers each topic you pick in his own voice (agreeing to a job, refusing, asking, threatening, joking) and says goodbye when you leave. Skyrim's topic list works as usual, and E or the left mouse button skips the speaker's line. E on someone sitting (the Jarl on his throne) talks to them rather than offering the seat. When Skyrim takes the player over itself (a scene that takes the controls away, a chair or bench, a bed), CJ goes where the player goes, sitting with San Andreas's sitting animation where it sits and walking or running otherwise, and you get him back when Skyrim lets go. W, S, A or D (or Skyrim's activate key) stands him up from a seat. Word walls and other triggers work as CJ walks into them.

While CJ has the player, San Andreas plays his, his vehicles' and his speech's sounds itself, from where its camera is, at Skyrim's master and effects volume. In a vehicle its radio plays at Skyrim's master and music volume, starting on the vehicle's own station, and the station's name shows top centre. The User Track Player's slot is **Radio Sovngarde**: San Andreas goes quiet and Skyrim plays its own music from its archives, starting with the main theme, then combat tracks in a fight and the time of day's exploration tracks otherwise, with a bard's lute, flute or drum piece after every two. After it comes **Sweetroll FM**, AhShitRim's own station: about half an hour of CC0 (public domain) music, shuffled so every track plays once before any repeats, starting on a random one. Its tracks ship with AhShitRim in `Data\Music\AhShitRim\SweetrollFM\` (`scripts/deploy-local.ps1` copies them there for a dev build, and the release zip carries them in the same place); with none installed it shows its name and stays quiet. Skyrim's own music stops while any station plays. A pausing Skyrim menu pauses both. San Andreas's world ambience (city noise, weather, police scanner, distant gunfire) stays off.

## Settings

`Data/SKSE/Plugins/AhShitRim.ini` (the launcher writes it and keeps these):

| Section | Key | What |
|---|---|---|
| `[Input]` | `KeyLayout` | `Modern` (default) or `Classic`, the keys above |
| `[Missions]` | `MiscPassed` | `1` (default): a Miscellaneous quest done (a favour, a delivery) shows MISSION PASSED! with San Andreas's jingle, as its odd jobs do, without RESPECT + and without a title when it starts. `0`: Skyrim's own notifications for those |

## Shouts as CJ

CJ has the Thu'um his own way. Shouting works as in Skyrim (learn the words, hold the Shout key for more of them, wait out the cooldown), and the shout's effect is Skyrim's: Unrelenting Force still throws a bandit. But the Dragonborn's dragon words are gone: when the shout goes off, CJ yells one of his San Andreas lines instead, picked for that shout and how many words you charged, so a tap and a full Fus Ro Dah sound different. A shout with no lines of its own gets one of his generic angry lines, so he's never silent. Only the player's voice changes; dragons, draugr and the Greybeards still shout in theirs. Whirlwind Sprint carries CJ along with the dash, and WANRLTW (infinite ammo) also means infinite shouting: no cooldown while it's on.

Which lines go with which shout is picked by ear in the voice picker ([`scripts/voice-picker`](scripts/voice-picker/README.md)), which builds a local page from your own San Andreas install.

## CJ talks on his own

CJ comments on what happens the way he does in San Andreas: now and then when a punch lands, when a burst of gunfire hits someone, when he aims a gun on foot, after a sneak kill, when he loots gold, when he buys at Ammu-Nation and when he leaves after buying, when he sprints away from someone fighting him, and when he crashes a vehicle hard. Left sitting in a car with the radio off, or standing idle, for a minute and a half, he sings, and stops when the radio comes on or he moves off. He isn't chatty: nothing comes sooner than 4 s after his last line, each situation has its own odds (about one punch in three), he never talks over himself, and the insult and swear keys always cut in. Each situation's lines are picked in the voice picker too; until they are, San Andreas picks them as it would.

## Cheat codes

Type San Andreas's PC cheat codes while CJ has the player (F10), as in San Andreas: no console, just the letters, and the help box says "Cheat activated" (or "Cheat deactivated" for one that toggles) with San Andreas's beep. HESOYAM, BAGUVIX, the weapon sets, the vehicles (JUMPJET, OHDUDE, AIWPRTON...), flying cars, mega punch, floating cars, the jetpack and the parachute all work, with San Andreas's alternative spellings (ROCKETMAN, FULLCLIP...), AEZAKMI keeps the wanted level away, OSRBLHH and LJSPQK bring it on (two more stars, six), ASNAEB clears it, plus our own: NOMERCY, MINIGUN, PAINT, LOWRIDER, and every vehicle by its name (SANCHEZ, HYDRA, MAVERICK), or a plane already flying with AIR before its name (AIRAT400). M doesn't open Skyrim's map while CJ has the player; the map is in Tab's menu.

Every code, with its aliases and what it does: [docs/CHEATS.md](docs/CHEATS.md).

**NOMERCY can break quests.** It takes the essential flag off every person and creature but the player, so quest givers, followers and anyone a quest still needs can die for good, and Skyrim's own quests weren't written for that. Typing it again gives exactly those flags back (someone who died meanwhile stays dead). It stays on in your saves until you type it again, so save before you try it.

## How it will work

- An **SKSE plugin** (C++) samples Skyrim's collision around the player, mirrors NPCs, and renders CJ, his vehicles and weapons inside Skyrim from San Andreas's own models, read from your install at play time.
- An **ASI plugin** (C++, [plugin-sdk](https://github.com/DK22Pac/plugin-sdk)) empties San Andreas's map, builds Skyrim's collision there around a floating origin, runs CJ, vehicle and weapon physics on it, and streams poses and events back.
- The two talk over versioned shared memory.

Skyrim's own player actor stays the real player, so dialogue, quests, bounties and kill credit keep working. CJ is the body the player drives.

## Planned features

- **CJ** on foot, with San Andreas's movement and animations.
- **Cars and bikes**: San Andreas's vehicle physics on Skyrim's roads, and running people over. Skyrim's arrows, blows and spells hit the car, not CJ inside, and bandits, wolves and giants can wreck it until it burns and blows up (a bike only half shields him).
- **Guns**: San Andreas's weapons in CJ's hands, hurting Skyrim's people, who react like San Andreas's peds: a civilian CJ points a gun at cowers, gunfire and the chainsaw send them running, and to Skyrim it's all magic ("Whoa, whoa, whoa! Watch the magic!") (done).
- **Quests**: complete Skyrim's quests as CJ. Anything that needs spells or lockpicking hands the player back to Skyrim.
- **Wanted level**: stars from the hold's bounty and the crimes CJ commits. More stars, more guards, and from 3 stars the hold's wolves run CJ down, on foot or in a car. Lose them by staying out of sight, or pay the guard. Stopped in a car with a guard at the door, you're dragged out and Busted (done, see [Wanted level and Busted](#wanted-level-and-busted)).

## Wanted level and Busted

Skyrim's crime system stays: the bounty is Skyrim's, kept per hold, and its guards still arrest, fine and fight the player as they do. On top of it San Andreas's wanted stars show in the HUD while CJ has the player:

- Each crime heats things up, as in San Andreas: shooting a guard is one star, killing two is two stars, four is three, and six stars take a real rampage. Petty crimes (trespassing, theft) are a star at most.
- Out of the guards' sight for a few seconds the stars start to flash, then go one at a time. Killing the witnesses wipes Skyrim's bounty but not the stars. A guard who fights CJ later knows him by his bounty, and a star or two come back.
- The guards never run out: reinforcements keep coming while CJ is wanted, one at a time and never where he's looking (behind him, round a corner, out of the city's barracks). Kill them all and there's a short breather to get away. From 3 stars the hold sets its tamed wolves on CJ, a couple at first and a pack of four by 5 stars (ice wolves from 5): they fight on the guards' side and are fast enough to catch him sprinting, and the further behind they are the faster they run, so even a car doesn't shake them for long. From 4 a captain and a guard wizard join, from 5 the hold's soldiers and a dragon, and at 6 two giants and a mammoth that run CJ down if he stays on foot (dragons, giants and mammoths only out in the open world, not in a city). At most 12 at once.
- Skyrim's guards hit much harder than San Andreas's cops, so at first they go easy on CJ: a quarter of their damage at 1 star, half at 3, all of it from 4. Bandits, wolves and everyone else hit as usual.
- CJ has his own lines for being chased and busted.
- A giant's club or a mammoth throws CJ through the air as a car's blast does, and he gets back up San Andreas's way.
- In a car, stop for a second or two and the nearest guard puts his weapon away and runs round to the driver's door, then drags CJ out: Busted. On a bike he runs to CJ and knocks him off. Keep moving and they can't.
- On foot, Skyrim's guard dialogue is as it was: pay the fine, resist, persuade. Only "go to jail" is Busted instead.
- Busted takes the bounty out of CJ's gold (all of it if that's short), clears it, and CJ comes to outside the hold's barracks (Dragonsreach in Whiterun) with all his weapons. No jail. Once he's arrested nobody shoots him.
- Paying the fine to a guard ends the stars too.
- AEZAKMI: every bounty cleared and the guards leave him alone at once; after that, no stars and crimes put no bounty on him.

## Milestone 1: on the ground

San Andreas walking and driving on Skyrim's ground is the hardest part, so it comes first:

- CJ on foot, on Skyrim terrain, drawn by Skyrim (done).
- Vehicles driven on Skyrim terrain, drawn by Skyrim: a Sandking, a ZR-350 and a Sanchez dirt bike (done).
- No guns, NPCs, quests or wanted level yet.

San Andreas loads collision as static per-sector files, so Skyrim's ground is built there at runtime: collision objects spawned around CJ from the chunks Skyrim streams, under a floating origin (see [docs/DESIGN.md](docs/DESIGN.md#ground-skyrims-collision-in-san-andreas)). Once CJ and his vehicles stand on Skyrim's ground, everything after it is known work.

## Known issues

- While San Andreas has the player, the view is San Andreas's third-person camera only. First person isn't supported yet.
- CJ and the vehicles cast no shadow in Skyrim yet, and CJ always wears his starting clothes (vest, jeans, sneakers).
- Vehicles: damage isn't shown (dents, broken windows and lost doors stay as they were), and lights aren't drawn. Of San Andreas's vehicle particles the Hydra's jet exhaust, the heat haze over hot exhausts and jets, a submerged exhaust's bubbles and the rotor dust's sand and water versions aren't drawn yet (the dust is always the plain kind), and the muzzle flashes are round where San Andreas stretches them along the barrel.
- Radio Sovngarde plays the bards' instrumentals, not their sung songs: those are voice lines split by verse, which Skyrim can't play from a file.
- Vehicles live only while San Andreas has the player: they're hidden when Skyrim takes it back, and the next F10, load door or fast travel removes them.
- CJ sits on anything Skyrim's player uses as furniture, in the same chair pose: at a forge, an alchemy table, a bed or on a horse too. A scene that leaves the player standing or kneeling (Helgen's execution block) shows CJ standing.
- In conversations CJ's lines are, for now, his calm lines of the San Andreas speech contexts nearest each category, picked by context rather than by ear, so some fit oddly. He answers as the speaker starts replying (Skyrim's player is silent, so its people answer at once), and the cutscene camera ignores whatever stands between it and the two of them except walls.
- The Helgen intro (the cart and the execution) isn't supported as CJ yet: play it as Skyrim's own player and press F10 afterwards.
- In a carriage CJ sits on the ground beside it, where the player got on: Skyrim's animation carries the player into the cart, and the carriage only says where it gets on. The ride itself (fast travel) works.
- Every surface counts as tarmac for San Andreas: roads, grass and dirt all grip the same. A low sports car like the ZR-350 scrapes on Skyrim's rocks and rough ground (its ground clearance, not a collision bug).
- Wanted level: the police scanner stays off, a car CJ was busted out of is left behind (gone with the next load door), and reinforcements only come outdoors.
- Looting as CJ takes only gold and quest items; for anything else, or to talk to a blacksmith with an Ammu-Nation, press F10 and use Skyrim's menus. A boss's gun reward comes again if a save from before it is loaded and the boss killed again (that save didn't have it).
- Guns: right-click's lock-on has no Skyrim actor to lock onto (free aim works), and bullet holes leave no decal.
- Explosives: San Andreas keeps 60 fires at a time, so with four or five Molotovs burning at once the later ones' outer flames are silent and don't set CJ alight (Skyrim's flames and damage still work). The spray can and extinguisher do nothing yet. Rockets leave no smoke trail. The heat seeker's homing is our own (San Andreas's isn't reversed), its marker doesn't spin as San Andreas's does, and a homing rocket's roar flies on straight where the rocket turns. Satchels are dropped when CJ goes through a load door or F10 hands the player back, and one stuck to an actor that unloads stays where it was. Locks only a key opens stay locked.
- Skyrim keeps every vehicle model it was sent until it quits (a few MB each), so spawning dozens of vehicles in one session uses up memory.

## Requirements

Setting them up, step by step: [docs/INSTALL.md](docs/INSTALL.md).

- Windows 10/11 and a PC that can run both games at once (San Andreas is light)
- [The Elder Scrolls V: Skyrim Anniversary Edition](https://store.steampowered.com/app/489830/) (latest Steam version) with [SKSE64](https://skse.silverlock.org) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [Grand Theft Auto: San Andreas](https://store.steampowered.com/app/12120/) (Steam), **downgraded to 1.0 US** with a downgrader, plus an ASI loader. One exe version means one set of memory addresses. The Definitive Edition is a different engine and won't work.

## Game assets

This repository contains **no assets from San Andreas or Skyrim**, and contributions must not add any. You need your own copies of both games. Models, sounds and textures are read from your installs at play time, never converted into the release or redistributed.

The one exception is our own music: Sweetroll FM's tracks are CC0 (public domain) music by other artists, listed with their sources in [`music/CREDITS.md`](music/CREDITS.md). `scripts/encode-music.ps1` downloads and encodes them to `music/sweetrollfm/*.xwm`, which ship with AhShitRim. Still nothing from either game.

## License

Not chosen yet (the repository is private for now).

Not affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Bethesda Softworks. Grand Theft Auto, San Andreas and Skyrim are trademarks of their respective owners.
