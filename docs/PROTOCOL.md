# Protocol

> Status: protocol v20. Shared memory section, version handshake and heartbeat (v1), an input slot and a pose slot (v2), CJ's crouch in the pose (v3), the handover spot in the input and a ring from Skyrim to San Andreas carrying collision chunks (v4), a body slot with CJ's skeleton and San Andreas's camera and a ring from San Andreas to Skyrim carrying CJ's model (v5), vehicles: spawn requests and the vehicle keys in the input, four vehicle slots with each vehicle's parts, and the vehicles' models on the ring back (v6), sound: the mouse wheel's radio steps in the input, an audio slot with Skyrim's volumes and the radio station in the pose (v7), the handbrake as its own button, so the classic key layout can sprint on Space, handovers that re-anchor CJ where a load door or fast travel puts the player (v8), and scenes in the input: CJ following Skyrim's player when Skyrim moves it, sitting where it sits, facing the one he talks to, and a shout counter for his gesture (v9), the pose's radio station as the dial's, with Sweetroll FM (v10), and health and the save: the damage San Andreas works out for CJ and the vehicle he is in in the pose, the dying flag and a loaded save's car in the input (v11), and weapons: the mouse's fire and aim buttons, the wheel's weapon switches, CJ's loadout, the weapon in his hands, the shots and the camera mode in the pose, two weapon slots with the models of what he holds, weapon commands on the ring to San Andreas, and the loadout in the co-save (v12), and shots: each bullet, shotgun shot and melee blow CJ deals sent to Skyrim to hit test, his hits sent back to raise his weapon skills, the skills in the pose, the loadout command and the co-save (v13), and Wasted as San Andreas plays it: CJ's fall and the death camera in the input, the camera's up in the body slot (v14), and explosives and fire: each rocket, grenade, tear gas canister and Molotov CJ launches sent to Skyrim to fly, and each puff of the flamethrower as a shot (v15), and the insult and swear keys' counters in the input (v16), and satchel charges and the detonator on the projectile ring (v17), and vehicle damage: each vehicle slot's health, wrecked and burning flags and engine (v18), and every land and air vehicle and CJ's gear: the look, turret and second-weapon buttons in the input, the jetpack and the parachute as spawn requests, a mask of the parts each vehicle slot hides (a damaged or whole version, a rotor standing or spinning), and a gear slot with the jetpack or the open parachute (v19), and cheat codes: San Andreas's own toggles and one-shot cheats typed in Skyrim as Cheat messages on the ring to San Andreas (v20).

The SKSE plugin (Skyrim side, 64-bit) and the ASI plugin (San Andreas side, 32-bit) exchange data over one shared memory section. See [DESIGN.md](DESIGN.md) for the architecture and ownership rules. The design follows PerkeleRim's link.

| File | What it is |
|---|---|
| [`protocol/ahshitrim_protocol.h`](../protocol/ahshitrim_protocol.h) | Source of truth: every struct, offset and constant, with `static_assert`s |
| [`protocol/cpp/link.h`](../protocol/cpp/link.h), `link.cpp` | The link, compiled into both plugins and the tests |
| [`protocol/cpp/input.h`](../protocol/cpp/input.h) | Skyrim's keys to CJ's actions and spawn requests (`Input::Builder`), and those to San Andreas's pad on foot or in a vehicle (`Input::ToPad`) |
| [`protocol/cpp/pose.h`](../protocol/cpp/pose.h) | CJ's San Andreas pose to the `PlayerState` Skyrim reads |
| [`protocol/cpp/collision.h`](../protocol/cpp/collision.h) | Skyrim side of collision chunks: the chunk grid, the heightfield, building a payload (from PerkeleRim) |
| [`protocol/cpp/ground.h`](../protocol/cpp/ground.h) | San Andreas side: the floating origin, a payload to San Andreas's collision format, which chunks it keeps |
| [`protocol/cpp/fall_rescue.h`](../protocol/cpp/fall_rescue.h) | Where CJ goes when he ends up below the ground |
| [`protocol/cpp/skin_model.h`](../protocol/cpp/skin_model.h) | CJ's model blob: building it, cutting it into chunks, putting it back together, reading it |
| [`protocol/cpp/skin_build.h`](../protocol/cpp/skin_build.h) | San Andreas's skinned geometry to the model's meshes |
| [`protocol/cpp/texture_decode.h`](../protocol/cpp/texture_decode.h) | San Andreas's Direct3D 9 texture formats to RGBA |
| [`protocol/cpp/body.h`](../protocol/cpp/body.h) | San Andreas's skin matrices to bones, skinning a vertex, the interpolation between San Andreas's frames |
| [`protocol/cpp/camera.h`](../protocol/cpp/camera.h) | Turning Skyrim's camera node to a view (from PerkeleRim) |
| [`protocol/cpp/radio.h`](../protocol/cpp/radio.h) | Skyrim's sliders to San Andreas's volumes, the radio's dial and station names, Radio Sovngarde's track picks, Sweetroll FM's tracks and shuffle |
| [`protocol/cpp/health.h`](../protocol/cpp/health.h) | CJ's health and armour: the damage San Andreas worked out turned into new damage, armour absorbing first, a share of Skyrim's health |
| [`protocol/cpp/wasted.h`](../protocol/cpp/wasted.h) | The Wasted sequence (CJ down, fade, move, wake), which way he falls and which temple the player wakes in |
| [`protocol/cpp/cosave.h`](../protocol/cpp/cosave.h) | The versioned co-save record: handover on or off, armour, the car, the weapons |
| [`protocol/cpp/weapons.h`](../protocol/cpp/weapons.h) | San Andreas's weapon ids and what Skyrim needs of them (model and icon names, ammo, flash), the scopes its camera modes mean, the wheel's presses on its pad, shots counted from the ammo, the muzzle, the loadout and the debug key's steps |
| [`protocol/cpp/img.h`](../protocol/cpp/img.h) | San Andreas's IMG archives: the directory and entries, so Skyrim reads the weapons' icons and scopes out of `gta3.img` |
| [`protocol/cpp/flash_model.h`](../protocol/cpp/flash_model.h) | The muzzle flash model: three crossed blended quads with a star-shaped glow |
| [`protocol/cpp/vehicle.h`](../protocol/cpp/vehicle.h) | Where a spawned vehicle goes, a vehicle's paint and lights colours, baking a part's scale or mirror into its vertices, merging a vehicle's meshes |
| [`protocol/cpp/vehicle_kinds.h`](../protocol/cpp/vehicle_kinds.h) | (v19) What kind each of San Andreas's 212 vehicle models is, which kinds spawn and why the rest don't, how CJ sits in each (the seat table), what fires, which of a rotor's or propeller's two parts shows, and the dev keys' picks |
| [`protocol/cpp/parachute.h`](../protocol/cpp/parachute.h) | (v19) The parachute on foot, after San Andreas's parachute script: when a fall becomes a skydive, its speeds and turns, opening, floating, flaring and landing, and parachute.ifp's animation for each |
| [`protocol/cpp/cheats.h`](../protocol/cpp/cheats.h) | (v20) The cheat codes AhShitRim has and what each does, San Andreas's keys of them (`CKeyGen::GetKey`), the cheat string as San Andreas types it (`Typist`, with the M key's rule), which toggles are on, and the Cheat command |
| [`protocol/cpp/mercy.h`](../protocol/cpp/mercy.h) | (v20) NOMERCY: the essential flags it cleared (actor bases, and actors with their base), who it never touches (the player), and its co-save record |
| [`protocol/cpp/vehicle_damage.h`](../protocol/cpp/vehicle_damage.h) | A damaged vehicle: San Andreas's smoke and fire thresholds, a slot's stage, the engine's place, a wreck's scorched colours, the smoke and fire Skyrim shows and spotting the blow-up |
| [`protocol/cpp/smoke_model.h`](../protocol/cpp/smoke_model.h) | A damaged vehicle's smoke: San Andreas's `overheat_car` WhiteSmoke and BlackSmoke (rates, lives, sizes, fades, drift) drawn with its `bullethitsmoke` sprite as camera-facing quads |
| `protocol/tests/` | doctest tests (layout, handshake, heartbeat, slots, rings, input mapping, pose conversion, collision chunks, floating origin, chunk bookkeeping, fall rescue, model blob, mesh building, texture decoding, bones, interpolation, vehicle spawn spots, part baking and mesh merging, vehicle kinds and the seat table, the parachute, the cheats' keys, typing and the M key, NOMERCY's ledger, vehicle damage stages, volumes, the radio dial, Radio Sovngarde and Sweetroll FM's shuffle, the weapons, the IMG reader, the flash and the scopes' layout), run by `scripts/test.ps1` |
| `protocol/tools/fake_skyrim.cpp` | Stands in for the Skyrim plugin to drive a running San Andreas and check CJ walks and his model and skeleton arrive, and that the three vehicles spawn on the ground, the Sandking drives with CJ in it and its radio dials round all 14 stops, the Sanchez rides, and a re-anchor far off and far below (fast travel in a car) takes CJ out, removes them and holds him until the ground there is in (`scripts/fake-skyrim.ps1`) |
| `protocol/tools/fake_vehicles.cpp` | (v19) Stands in for the Skyrim plugin to check every kind of vehicle and the gear against a running San Andreas: through the dev key it rides an NRG-500 and a BMX, sees a boat refused, flies a Hunter (its rotor swapping, its guns) and a Hydra (hover, forward flight on the nozzle keys, missiles), fires the Rhino's shell, flies up on the jetpack and comes down on the parachute (`scripts/fake-vehicles.ps1`) |
| `protocol/tools/fake_weapons.cpp` | Stands in for the Skyrim plugin to check CJ's weapons against a running San Andreas: gives the sets, switches through every weapon on the wheel, aims and fires each, checks the model of the weapon in his hand arrives, shots are counted, twin pistols come in two hands and a loadout set and cleared comes back as sent (`scripts/fake-weapons.ps1`) |

## Section layout

One section, `Local\AhShitRim.Link`, 8,404,736 bytes (8,402,816 before v19). Whichever game starts first creates it; it disappears when both have closed.

| Offset | Size | Part | Written by |
|---|---|---|---|
| 0 | 128 | `LinkHeader`: magic, sizes, creator version, one `PeerInfo` per side | creator, then each side its own `PeerInfo` |
| 128 | 144 | `InputSlot`: the player's input for CJ (v9: 144 bytes, keeping the later slots on 64-byte boundaries) | Skyrim |
| 272 | 304 | `PlayerSlot`: CJ's pose (v13: 304 bytes) | San Andreas |
| 576 | 64 | `AudioSlot`: San Andreas's volumes (v7) | Skyrim |
| 640 | 1920 | `BodySlot`: CJ's skeleton and San Andreas's camera (v5) | San Andreas |
| 2560 | 4 x 1920 | vehicle slots, one `BodySlot` per vehicle (v6) | San Andreas |
| 10240 | 2 x 1920 | weapon slots, one `BodySlot` per hand (v12) | San Andreas |
| 14080 | 1920 | gear slot, a `BodySlot`: the jetpack or the open parachute on CJ (v19) | San Andreas |
| 16000 | 64 + 4 MiB | ring Skyrim → San Andreas (collision chunks, weapon commands; at 14080 before v19) | Skyrim produces, San Andreas consumes |
| 4210368 | 64 + 4 MiB | ring San Andreas → Skyrim (CJ's, the vehicles', the weapons' and the gear's models, v5; the shots, v13; the projectiles, v15) | San Andreas produces, Skyrim consumes |

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

`InputState`, 112 bytes (88 before v11). Written whenever Skyrim's input changes while it has handed the player over, and every Skyrim frame then.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | Skyrim's input counter |
| 4 | `flags` | `kInputOwnsPlayer` (0x1): apply this input to CJ. `kInputPaused` (0x2): a Skyrim menu pauses the game, San Andreas pauses too. `kInputFace` (0x4, v9): a dialogue has the input, CJ stands still and turns to `sceneHeading`. `kInputScripted` (0x8, v9): Skyrim moves the player itself (a scene, a seat, a bed), CJ is put at `sceneOffset` facing `sceneHeading` and the keys do nothing. `kInputSeated` (0x10, v9, with `kInputScripted`): the player sits. `kInputWasted` (0x20, v11): the player is dying, the axes and buttons are zero and San Andreas's damage isn't counted; (v14) CJ falls dead on his front and lies there, at full speed (until v14 San Andreas ran at 0.3 speed). `kInputWastedCamera` (0x40, v14, with `kInputWasted`): San Andreas's death camera cuts in over him |
| 8 | `mouseX`, `mouseY` | doubles, raw mouse counts accumulated since Skyrim started (y positive down); San Andreas applies the change since its last read |
| 24 | `moveX`, `moveY` | -1..1, right and forwards positive; `moveY` is W and S only, the up and down arrows are `kButtonLean*` |
| 32 | `buttons` | `kButtonSprint` 0x1, `kButtonJump` 0x2, `kButtonCrouch` 0x4, `kButtonWalk` 0x8, `kButtonEnterExit` 0x10, `kButtonHorn` 0x20, `kButtonLeanForward` 0x40 (up arrow), `kButtonLeanBack` 0x80 (down arrow) (v6), `kButtonHandbrake` 0x100 (v8: Space, and Right Ctrl in the classic layout), `kButtonFire` 0x200 (v12: the left mouse button) and `kButtonAim` 0x400 (v12: the right one), `kButtonLookLeft` 0x800 (v19: Q), `kButtonLookRight` 0x1000 (v19: E in a vehicle), `kButtonTurretLeft` 0x2000, `kButtonTurretRight` 0x4000, `kButtonTurretUp` 0x8000 and `kButtonTurretDown` 0x10000 (v19: the numpad's 4, 6, 8 and 2, 8 and 2 also on Page Up and Page Down), `kButtonHydraulics` 0x20000 (v20: numpad 5, in a vehicle San Andreas's R3, the hydraulics' jump), held. Actions, not keys: Skyrim maps the keys in the modern or classic layout (`protocol/cpp/input.h`) |
| 36 | `handover` | counts handovers and re-anchors (v8: the game moved the player through a load door, by fast travel or a script while CJ has it, and the handover goes on); a new value puts CJ at `anchor`, drops the collision chunks of older ones and removes every vehicle |
| 40 | `heading` | the Skyrim player's heading at that handover or re-anchor (radians, Skyrim's convention); CJ spawns facing it |
| 44 | `anchor[3]` | the Skyrim player's feet at that handover, Skyrim world units: the floating origin's base (v4) |
| 56 | `spawnModel` | u16, the San Andreas vehicle model the last spawn request asked for (v6); (v19) 370 puts San Andreas's jetpack on CJ and 371 gives him the parachute (`VehicleKinds::kJetpack`, `kParachute`); (v20) a vehicle cheat's model or the jetpack's or parachute's cheat asks the same way |
| 58 | `spawnCount` | u16, counts spawn requests; a new value spawns `spawnModel` (v6) |
| 60 | `radioNext` | u16, counts mouse wheel steps up (v7); in a vehicle each one tunes the next station, on foot (v12) each one switches to the next weapon |
| 62 | `radioPrev` | u16, counts wheel steps down, the previous station (v7), on foot the previous weapon (v12) |
| 64 | `sceneOffset[3]` | with `kInputScripted`: the Skyrim player's feet relative to `anchor`, Skyrim units; with `kInputSeated` the seat (the furniture marker the player uses), and San Andreas puts CJ's pelvis over it (v9) |
| 76 | `sceneHeading` | with `kInputScripted`: the player's heading; with `kInputFace`: the heading to the speaker (radians, Skyrim's convention, v9) |
| 80 | `shoutCount` | u16, counts shouts the player let out (Skyrim's voice cooldown starting, v9); a new value plays CJ's gesture on foot. San Andreas only acts on a change after the first value it read |
| 82 | `carModel` | u16 (v11), San Andreas's vehicle model id of the car a loaded save had CJ in, 0 = none. Only read with a new `handover`: once the ground there is in, San Andreas puts the car at `carOffset` from CJ, facing `carHeading`, in `carColours`, and CJ at its wheel |
| 84 | `insultCount` | u16 (v16), counts presses of the insult key (X); a new value has CJ say an insult, with the finger on foot. San Andreas only acts on a change after the first value it read |
| 86 | `carColours[4]` | u8 (v11), the car's four colour table indices |
| 90 | `swearCount` | u16 (v16), counts presses of the swear key (the middle mouse button); a new value has CJ swear to himself, no gesture. Same first-value rule |
| 92 | `carOffset[3]` | the car relative to `anchor`, Skyrim units (v11) |
| 104 | `carHeading` | the car's heading, Skyrim radians (v11) |
| 108 | reserved | 4 bytes |

Keys map to actions with GTA's keyboard layout (`input.h`): W/S/A/D or the arrows move (in a vehicle the up and down arrows lean a bike instead), Left Shift sprints, Space jumps, Left Ctrl crouches (C in the classic layout, v16), Left Alt walks, F or Enter gets in and out of a vehicle, H sounds the horn. ], [ and `\` ask for a Sandking (495), a ZR-350 (477) and a Sanchez (468): each press sets `spawnModel` and counts `spawnCount` up once. Escape, Tab, `~` and F10 stay with Skyrim (v20: M, Skyrim's map, opens nothing; it is a letter of the cheat codes). While a Skyrim menu has the input (paused, or a cursor menu like dialogue), the axes and buttons are zero but the mouse totals and the spawn requests keep counting. San Andreas writes the actions into pad 0 right after `CPad::UpdatePads` and the mouse change into `CPad::NewMouseControllerState`, vertical flipped (passed through as is, the camera looked the wrong way). On foot: left stick, Cross for sprint, Square for jump, L3 for crouch, the walk flag, Triangle to get into a vehicle. In a vehicle: W holds Cross (accelerate) and S Square (brake, then reverse), the left stick's X steers and its Y leans a bike (up arrow forward, down arrow back), jump holds R1 (handbrake), the horn L3 and Triangle gets out. San Andreas spawns only on a change of `spawnCount` after the first value it read (a San Andreas that starts after Skyrim doesn't spawn the last vehicle again), and only while it has the player and CJ is on foot. A read of the slot that stays torn keeps the last input rather than counting as the link going down. v12, on foot only: the left mouse button is Circle (fire; a melee weapon attacks), the right one R1 (aim: San Andreas's lock-on and free aim, with the sniper rifle's, rocket launchers' and camera's first-person modes), and each wheel step is one press of R2 (next weapon) or L2 (previous) on a frame of its own after one without it, which is the edge San Andreas's weapon switch reads (`Weapons::WheelPulse`; steps that come in together go out one after another, and a counter that jumps by more than a hand does in a frame, a restarted Skyrim, is followed, not played). The wheel's counters are followed whatever CJ does, so the radio's steps in a vehicle never switch a weapon later. Weapons are San Andreas's own state: its slots, ammo, switching and animations. `=` is the debug key (below). v19, in a vehicle: the left mouse button is Circle and the right one L1, San Andreas's vehicle weapons (`CPad::GetCarGunFired` 1 and 2: the Hunter's guns and rockets, the Hydra's missiles, the Rhino's shell; drive-bys), Q and E hold L2 and R2 (look left and right; an aircraft's rudder; E is Skyrim's activate key on foot, so Skyrim passes it on only in a vehicle), and the turret buttons are the right stick (`GetCarGunLeftRight`, `GetCarGunUpDown`: the Hydra's nozzles between hover and forward flight). The dev keys (v19, while San Andreas has the player; off with `[Debug] VehicleKeys=0`): `;` steps through the dev picks (`VehicleKinds::kDevPicks`: NRG-500, BMX, Quad, Monster, Rhino, Hunter, Hydra, Sparrow, Maverick, Dodo, Stuntplane, Vortex, the jetpack and the parachute, or `[Debug] Vehicles`, a list of model ids) and says which in the help box, `'` spawns it as the spawn keys do; a boat, train, trailer or radio-controlled model is refused on Skyrim's side with the reason in the help box and never sent.

### Pose (San Andreas to Skyrim)

`PlayerState`, 248 bytes (224 before v13, 80 before v12, 56 before v11, 48 before v7). Written every San Andreas frame (30 a second) once CJ exists. Skyrim axes and units: game units (70 to the metre), Z up, heading 0 = north and clockwise, pitch positive looking down.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | San Andreas frame counter |
| 4 | `flags` | `kPlayerValid` (0x1), `kPlayerGrounded` (0x2): CJ stands on something (or sits in a vehicle), `kPlayerCrouching` (0x4): CJ crouches, Skyrim sneaks, `kPlayerInVehicle` (0x8, v6): CJ drives or rides a vehicle, `kPlayerRadioRetuning` (0x10, v7): the radio is between stations, `kPlayerOnFire` (0x20, v15): CJ burns in San Andreas, Skyrim shows flames on him |
| 8 | `offset[3]` | CJ relative to where the current handover spawned him |
| 20 | `velocity[3]` | units per second; in a vehicle the vehicle's, which Skyrim samples the ground ahead by |
| 32 | `heading` | CJ's body |
| 36 | `viewHeading`, `viewPitch` | where San Andreas's camera looks; Skyrim's first-person camera follows it |
| 44 | `handover` | the `InputState.handover` this pose belongs to; Skyrim ignores poses from older handovers |
| 48 | `radioStation` | i8 (v7, v10): the dial's station in CJ's vehicle: 0 emergency, 1..11 San Andreas's stations (its `eRadioID`), 12 Radio Sovngarde, 13 Sweetroll FM, 14 off, -1 (`kRadioNone`) not in a vehicle. Before v10 it was San Andreas's `eRadioID` with 12 as Radio Sovngarde and 13 off. While the wheel retunes it is the station it goes to |
| 49 | reserved | 1 byte |
| 50 | `vehicleModel` | u16 (v11), San Andreas's model id of the vehicle CJ drives or rides, 0 on foot |
| 52 | `vehicleColours[4]` | u8 (v11), its colour table indices |
| 56 | `vehicleOffset[3]` | the vehicle's position relative to CJ, Skyrim units (v11) |
| 68 | `vehicleHeading` | the vehicle's heading, radians (v11) |
| 72 | `damage` | f32 (v11): the damage San Andreas has worked out for CJ since it started, in its health points (100 = a full health bar), running. Only counted while Skyrim has the player; Skyrim takes the difference between reads off its own player (`protocol/cpp/health.h`) |
| 76 | reserved | 4 bytes |
| 80 | `weaponType` | u8 (v12): the weapon in CJ's hands, San Andreas's `eWeaponType`, 0 = fists |
| 81 | `weaponSlot` | u8: its slot, an index into `loadout` |
| 82 | `weaponFlags` | u16: `kWeaponInHand` 0x1 (a weapon model is in his hand), `kWeaponTwin` 0x2 (a twin pistol: the second hand's slot is drawn too), `kWeaponAiming` 0x4 (a scope or aim camera is up), `kWeaponFiring` 0x8, `kWeaponReloading` 0x10, `kWeaponMelee` 0x20 (fists, a melee weapon or a gift: no ammo to show) |
| 84 | `shots` | u16: counts the shots fired, from the held weapon's rounds going down; wraps. Skyrim flashes for each |
| 86 | `cameraMode` | u8: San Andreas's `eCamMode`: 0x07 and 0x27 the sniper scope, 0x08, 0x28, 0x33 and 0x34 a rocket sight, 0x22 the first-person rifle, 0x2E the camera, 0x35 free aim |
| 87 | `ammoStyle` | u8: how the HUD writes the rounds: `kAmmoTotal` 0 ("200"), `kAmmoClip` 1 ("83-17", the rest then the clip), `kAmmoFlame` 2 (the same in tenths) |
| 88 | `muzzle[2][3]` | where the shots leave, relative to CJ's feet, Skyrim units: the weapon's fire offset on its frame. `[1]` is the left hand's |
| 112 | `crosshair[2]` | where San Andreas's crosshair sits on the screen, as fractions of its width and height |
| 120 | `loadout[13]` | `WeaponEntry` (8 bytes: `type` u8, reserved, `clip` u16, `ammo` u32 up to 99,999): San Andreas's weapons, one per slot, type 0 for an empty slot |
| 224 | `skills[11]` | u16 (v13): San Andreas's weapon skill stats, `STAT_PISTOL_SKILL` (69) to `STAT_SNIPERRIFLE_SKILL` (79) in order (pistol, silenced pistol, Desert Eagle, shotgun, sawn-off, combat shotgun, micro SMG, SMG, AK-47, M4, sniper rifle), 0 to 1000: poor, gangster and hitman by `weapon.dat`'s thresholds |
| 246 | `aimRadius` | u16 (v13): the free-aim crosshair's size in thousandths, San Andreas's `CPlayerPed::GetWeaponRadiusOnScreen` (200 held steady, growing while he fires, by the weapon's accuracy and range); Skyrim draws `siteM16`'s four quarters that size |

Skyrim adds `offset` to the player's position at the handover.

### Audio (Skyrim to San Andreas)

`AudioState`, 32 bytes (v7); the slot is 64 bytes since v11. Written every Skyrim frame.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `effects` | 0..1, Skyrim's master x effects sliders: San Andreas's effects master volume (everything but the radio) |
| 4 | `music` | 0..1, master x music: San Andreas's music master volume (the radio) |
| 8 | reserved | 24 bytes |

Both are 0 while Skyrim has the player. San Andreas sets them every frame (0..64); a pausing menu pauses its sounds through `kInputPaused` instead. Mouse wheel steps in the input dial San Andreas's radio only while CJ is in a vehicle and nothing pauses (`protocol/cpp/radio.h`: 1..11, Radio Sovngarde, Sweetroll FM, off, round and round); on Radio Sovngarde and Sweetroll FM San Andreas's radio is off, and Skyrim plays its own music, or Sweetroll FM's tracks.

Log lines: `audio: sound on (effects N/64, music N/64)` / `audio: muted`, `audio: the wheel tunes NAME`, `audio: radio on NAME` / `audio: no radio`, `audio: San Andreas's radio at ID (on B, retuning B)` in San Andreas's log; `audio: menu sound category ID "NAME" at V (master|music|effects)`, `audio: radio NAME`, `audio: master volume V (fAudioMasterVolume)`, `audio: Skyrim's music paused for the radio|back`, `audio: Radio Sovngarde|Sweetroll FM plays PATH (N s)`, `audio: Sweetroll FM has N of 12 tracks installed in ...` at data load, `audio: Sweetroll FM has no tracks installed (...), so it stays silent` (once) in Skyrim's.

### Body (San Andreas to Skyrim)

`BodyState`, 1,892 bytes (1,884 before v19, 1,868 before v18, 1,856 before v14, v5). Written every San Andreas frame once CJ's model is built. Skyrim axes and units; `feet` and the camera are relative to where the handover put CJ's feet (`InputState.anchor` in Skyrim), the bones to `feet`.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `frame` | San Andreas frame counter, as `PlayerState.frame` |
| 4 | `flags` | `kBodyValid` (0x1) |
| 8 | `handover` | the `InputState.handover` this belongs to |
| 12 | `model` | the `ModelChunkHeader.model` the bones belong to |
| 16 | `boneCount` | bones in use (CJ has 32), the model's bone count |
| 20 | `timeMs` | San Andreas's high-resolution clock in ms, for Skyrim's interpolation |
| 24 | `feet[3]` | CJ's feet |
| 36 | `cameraPosition[3]` | San Andreas's active camera |
| 48 | `cameraForward[3]` | where it looks, a unit vector |
| 60 | `cameraFov` | its field of view in degrees, horizontal for a 4:3 screen like Skyrim's own setting |
| 64 | `bones[64]` | `BodyBone`: `rotation` (quaternion x, y, z, w) and `translation[3]`; takes a model vertex (bind pose) to its place relative to `feet` |
| 1856 | `cameraUp[3]` | (v14) the camera's up, a unit vector: its roll, which San Andreas's death camera swings; zero in vehicle and weapon slots |
| 1868 | `health` | (v18) vehicle slots: San Andreas's vehicle health (`m_fHealth`, 1000 new); 0 elsewhere |
| 1872 | `engine[3]` | (v18) vehicle slots: where its engine is, relative to `feet`; zero elsewhere |
| 1884 | `hiddenParts` | u64 (v19): bit i set, bone i isn't drawn (Skyrim collapses its vertices onto its joint); bones past 63 always are. Vehicle slots: the parts San Andreas doesn't draw now (a part's whole version once it is damaged, the damaged one before; one of a rotor's or a propeller's two versions); zero elsewhere |

A posed vertex is the weighted sum of where its (up to four) bones take it, its normal turned the same way. Skyrim draws the frame 50 ms behind the newest, blended between the two around it (`Body::Interpolator`). San Andreas walks CJ on Skyrim's own collision there (see below), so the offset follows Skyrim's terrain and statics; the floating origin moving in San Andreas doesn't change it.

### Vehicles (San Andreas to Skyrim)

Four vehicle slots (v6), one per vehicle San Andreas keeps (`kVehicleMax`), each a `BodySlot` holding a `BodyState`, written every San Andreas frame once that vehicle's model is built:

| Field | For a vehicle |
|---|---|
| `flags` | `kBodyValid` (0x1); `kBodyDriven` (0x2) while CJ is in it; (v18) `kBodyWrecked` (0x4) once it blew up (`STATUS_WRECKED`), `kBodyBurning` (0x8) while it burns: its engine (health under 250, not wrecked or drowning, as `CAutomobile::ProcessCarOnFireAndExplode` tests) or the wreck (San Andreas's fire on it, `m_pFire`). 0 once the vehicle is gone: Skyrim draws nothing for the slot |
| `model` | the vehicle's `ModelChunkHeader.model` |
| `boneCount` | its parts (one per drawn atomic: chassis, wheels, doors, bonnet, a bike's forks and handlebars) |
| `feet[3]` | the vehicle's position, relative to where the handover put CJ's feet |
| `bones` | each part's frame relative to `feet`: its world matrix after `PreRender` (wheels turned, steered and on their suspension, forks and handlebars, and a bike's lean, which `PreRender` puts into the parts under the upright root) |
| camera | unused (zero, forward a unit vector) |
| `health` | (v18) its health: it smokes under 650 and burns under 250, and San Andreas blows it up after 5 s of burning |
| `engine` | (v18) its engine, relative to `feet`: the model's engine dummy (`m_avDummyPos[7]`, where San Andreas puts `overheat_car` and `fire_car`) in the vehicle's frame, or a point on the bonnet for a model without one |
| `hiddenParts` | (v19) the parts not drawn now: an atomic without `rpATOMICRENDER` (`CVehicle::SetComponentVisibility` swaps a door's, a bonnet's or a panel's whole and damaged versions), and of a helicopter's rotors (`static_rotor`, `moving_rotor`, their `2`s) and a plane's propellers (`static_prop`, `moving_prop`) the standing one while it spins faster than 0.1 radians a step (`m_fHeliRotorSpeed`, `CPlane::m_fPropSpeed`), the blurred disc otherwise (`VehicleKinds::SpinnerShows`; San Andreas sets their alpha when it draws them) |

Skyrim draws each vehicle like CJ, 50 ms behind with its own `Body::Interpolator`, and only while it has handed the player over. From v18 it shows the damage at `engine` (`skse/src/player/VehicleFx.cpp`, `protocol/cpp/vehicle_damage.h`): San Andreas's own white and black smoke from 650 (`protocol/cpp/smoke_model.h`, its sprite from the install's `models/effectsPC.txd`), a tall vanilla fire (four stacked) with the black smoke while `kBodyBurning`, a fireball twice a rocket's (hurting and throwing Skyrim's people within 9 m, and CJ unless `kBodyDriven`) when the slot turns `kBodyWrecked`, and fire on a burning wreck. A wreck's model comes again, scorched (its colours at a fifth, San Andreas's scorched lighting), under a new id. The chase camera needs nothing of its own: San Andreas's active camera in the body slot follows the vehicle while CJ drives it.

Any land or air vehicle spawns (v19): cars, monster trucks, quads, bikes and bicycles, helicopters (`CHeli`) and planes (`CPlane`); boats, trains, trailers and the radio-controlled ones don't (`VehicleKinds::TraitsOf`, checked against the model's `m_nVehicleType`). Their rotors, propellers, landing gear, a Hydra's nozzles and a Rhino's turret are frames `PreRender` turns, so they come in `bones` like the wheels.

### Gear (San Andreas to Skyrim)

The gear slot (v19, one `BodySlot` at 14080) holds what CJ wears on foot, posed like a weapon (`feet` CJ's, `bones` relative to them), every San Andreas frame while there is one, flags 0 otherwise (`sa/src/cj_gear.cpp`):

| `flags` | What |
|---|---|
| `kBodyValid` + `kBodyJetpack` (0x10) | San Andreas's jetpack (`CTaskSimpleJetPack`): its clump's three parts (`jetpack`, `jetball1`, `jetball2`), placed on his back as `RenderJetPack` places them (his skeleton's matrix 3, nudged and turned a quarter about Y), the jet balls turned with the thrust. A `kSkinModelWeapon` model, `ModelSender::kGearKey` (31) |
| `kBodyValid` + `kBodyParachute` (0x20) | the open parachute: the `PARACHUTE` model's skinned canopy (31 bones), its root on CJ's matrix and its skeleton playing parachute.ifp's `_O` animation for the phase (`PARA_open_O`, `PARA_float_O`, the steers, `PARA_decel_O`, `PARA_Land_O`). A `kSkinModelBody` model under `kGearKey` + 1 |

The parachute's skydive is San Andreas's mission script's, which never runs, so it follows `protocol/cpp/parachute.h`: carrying the parachute, falling at 5 m/s or more with 15 m of air under him (a line down to San Andreas's ground), CJ skydives, the parachute put in his hands as the script does (`FALL_skyDive`, `FALL_SkyDive_Accel` on W, the turns on A and D: right before his animations update, `CjGear::HoldCjAnim` gives the phase's `parachute` block animation all of his pose, San Andreas's partial fall (`FALL_FALL`, `FALL_GLIDE`, the flailing its in-air reaction blends in) and everything else none but his face's animations, and his idle a 2% sliver: parachute.ifp keys 26 of his 32 bones, not his face, and San Andreas normalizes each bone's weighted rotations, so the idle alone sets his face (`Parachute::HoldOf`)), at up to 40 m/s down (55 diving, 32 spread out) and 4 m/s ahead (14, 1), turning 1.5 radians a second. The fire key opens the chute (`PARA_open`, 1.1 s, braking him), then he floats down at 4.5 m/s (7 on W, 2.5 flared on S) and 7 m/s ahead (11, 2.5), turning 0.9 radians a second. Touching down he lands (`PARA_Land`, 1.3 s), and the parachute is taken from him (as the script's `REMOVE_WEAPON_FROM_CHAR`). His speed is set every frame while he flies; hitting the ground with it shut, San Andreas's own fall does the rest (its damage too). The jetpack, a vehicle, a scene or Wasted ends it.

### Weapons (San Andreas to Skyrim)

Two weapon slots (v12, `kWeaponHandMax`), the right and the left hand, each a `BodySlot` holding a `BodyState` like a vehicle's: `feet` is CJ's feet relative to where the handover put them, `boneCount` the weapon's parts and `bones` each part's frame relative to `feet`, every San Andreas frame. San Andreas places the weapon on CJ's hand bone the way it does when it draws a ped (`CVisibilityPlugins::RenderWeaponPedsForPC`: the weapon's frame takes the right hand's matrix, a twin pistol's second hand the left's turned over and nudged, a parachute the spine's), which never runs while it is hidden, and writes the parts' transforms from there. The slot is empty (flags 0) while he holds no weapon with a model; the left hand's only for a twin pistol (the same model, a model id of its own). Skyrim draws them like CJ, with their own `Body::Interpolator`s and under the same node, and not at all while the camera looks through a scope (sniper rifle, rocket launcher, camera), when CJ isn't drawn either.

San Andreas's own routines need nothing more than that to work hidden, except two: the minigun's barrels are turned by `CPed::PreRenderAfterTest`, which it does itself, and a gun task only marks its shot and `CPedIntelligence::ProcessAfterPreRender` (part of drawing the ped) fires it, so it calls that after posing his bones.

## Rings

Messages that must all arrive, one after another: two single-producer, single-consumer byte rings of 4 MiB, one each way (Skyrim to San Andreas since v4, San Andreas to Skyrim since v5). `head` (producer) and `tail` (consumer) are monotonic byte counts on separate 32-byte halves of the 64-byte `RingHeader`; a record is an 8-byte `MessageHeader` (`type` u16, reserved u16, `size` u32) plus its payload, and may wrap around the end. `Send` puts a message in the ring to the other side and refuses while the link is down, when the ring is full or when the message can never fit; each side drains its own ring every frame. A side empties its ring when it attaches, so messages sent to an earlier instance never arrive.

| Type | Message | Direction | Payload |
|---|---|---|---|
| 1 | `CollisionChunk` | Skyrim → San Andreas | `CollisionChunkHeader` (chunk x/y, worldspace, generation, origin, counts), vertices relative to origin, triangle indices |
| 2 | `ModelChunk` | San Andreas → Skyrim (v5; vehicles since v6, weapons since v12) | `ModelChunkHeader` (model id, total size, offset, size), then that part of the model blob |
| 3 | `WeaponCommand` | Skyrim → San Andreas (v12) | `WeaponCommand` (136 bytes since v13): `kind`, `argument`, 13 `WeaponEntry`s and 11 skills |
| 4 | `Shot` | San Andreas → Skyrim (v13) | `ShotEvent` (64 bytes): a bullet, a shotgun's shot or a blow CJ dealt, (v15) or a puff of the flamethrower, spray can or fire extinguisher |
| 5 | `Projectile` | San Andreas → Skyrim (v15) | `ProjectileEvent` (128 bytes): a rocket, grenade, tear gas canister, Molotov or (v17) satchel charge CJ launched, or (v17) the detonator |
| 6 | `Effect` | Skyrim → San Andreas (v15) | `EffectEvent` (32 bytes): what CJ's explosives did, for San Andreas's own sound, fire and camera shake |
| 7 | `Cheat` | Skyrim → San Andreas (v20) | `CheatCommand` (8 bytes): one of San Andreas's own cheats typed in Skyrim |

### CJ's model

San Andreas sends CJ's model whenever the link comes up (a Skyrim that just started has none of it) and whenever his geometry changes, as one blob cut into `ModelChunk`s of at most 256 KiB (`protocol/cpp/skin_model.h`). Chunks of a model come in order from offset 0; Skyrim builds the model once the last one is in, and drops one that comes out of order. Every model gets a new id; `BodyState.model` names the one its bones belong to. CJ's model and the vehicles' go one whole model after another (`sa/src/model_sender.cpp`), CJ's first, all of them again whenever the link comes up.

The blob: `SkinModelHeader` (48 bytes: name, texture, mesh and bone counts, `kind`: `kSkinModelBody` 0, `kSkinModelVehicle` 1 (v6) or `kSkinModelWeapon` 2 (v12)), `boneCount` u32 bone ids (San Andreas's node ids, for the logs), then per texture a `SkinTexture` (width, height, format 1 = RGBA8) and its pixels, top row first, then per mesh a `SkinMesh` (texture index or -1, vertex and index counts, flags `kSkinMeshCutout` 0x1 / `kSkinMeshBlend` 0x2, RGBA colour) with `vertexCount` `SkinVertex` (52 bytes: position, normal, UV, four bone indices, four weights; bind pose, Skyrim units) and `indexCount` u16 triangle indices. CJ comes as 5 meshes, about 1,750 vertices and 2,370 triangles, 4 textures and 32 bones, about 580 KB.

### Weapon commands

`WeaponCommand` (v12): `kind` u32, `argument` u32, `entries[13]`, (v13) `skills[11]` u16 and 2 reserved bytes.

| `kind` | Meaning |
|---|---|
| `SetLoadout` 1 | Replace CJ's weapons with `entries` (a loaded save, a new game: all empty) and hold slot `argument`; San Andreas clears his weapons, loads each weapon's model and gives it with its ammo and clip, and a slot he doesn't hold means fists. (v13) Sets his weapon skill stats to `skills` too (a save from before v13, a new game: all 0) |
| `GiveSet` 2 | Give one of San Andreas's weapon sets: `argument` 1 to 3 are its three weapon cheats' (`CCheat::WeaponCheat1` to `3`), 4 the gear they leave out (tear gas, satchel charges and detonator, minigun, camera, parachute) |
| `GiveWeapon` 3 | Give one weapon: `argument` is its type, `entries[0].ammo` its ammo (at least 1) |
| `Hit` 4 | (v13) CJ hit someone in Skyrim with weapon `argument`, `entries[0].ammo` times (at most 64 a command): San Andreas counts each as its own hit (`CStats::UpdateStatsWhenWeaponHit`), which raises his skill with the guns that have one. Skyrim sends one per weapon each frame it hit with it |

Commands carry no handover (a loadout isn't a place), so San Andreas applies them as it reads them. A weapon in a slot needs its model in memory (a weapon San Andreas gives itself arrives with the game's own streaming flags), so every command loads and keeps the models of the weapons CJ carries (`CStreaming`, kept in memory) before giving them. Skyrim sends `SetLoadout` for the save's loadout whenever the link is up after a load (and again when it comes up after San Andreas restarted), and a new game or a save without weapons sends an empty one. `GiveSet` and `GiveWeapon` are the debug key's (below); `GiveSet` 1 to 3 are also the weapon cheats' (v20).

### Cheats

While CJ has the player, Skyrim types every letter key pressed into a cheat string as San Andreas's `CCheat::AddToCheatString` does (`protocol/cpp/cheats.h`, `skse/src/player/CheatCodes.cpp`): the letter goes in front of the 29 kept, and the first of the newest 6, 7, ... letters whose key (`CKeyGen::GetKey`: a CRC-32's register over them, newest first, not flipped at the end) is a cheat's finishes that cheat and empties the string. Keys, not codes, are compared, so San Andreas's alternative codes that share a key work (FULLCLIP for WANRLTW). A key is a letter by its place on a US keyboard (DirectInput scan codes are places). Skyrim's input sink types, since Skyrim has the keys. M, Skyrim's map key, opens nothing while CJ has the player (its `MenuOpenHandler` hook lets only Escape, Tab and the console through, `Input::OpensSkyrimMenu`), since codes have it in the middle (UZUMYMW). The help box says San Andreas's "Cheat activated", or "Cheat deactivated" for a toggle typed off.

What a finished cheat does (its `Kind` in `kCheats`):

| Kind | Skyrim | San Andreas |
|---|---|---|
| `Health` (HESOYAM) | the player's health and CJ's armour full, 250,000 gold | its `MoneyArmourHealthCheat`, which mends CJ's vehicle (its health and armour are set over by the next frame's fixed health, `Vitals`) |
| `Invincible` (BAGUVIX) | while on, the player takes no damage: San Andreas's is dropped and Skyrim's undone every frame | its infinite health toggle |
| `Weapons` | `WeaponCommand` `GiveSet` 1, 2 or 3 | its `WeaponCheat1` to `3` (through the weapon command, which loads the models) |
| `Spawn` | a spawn request (`spawnModel`, `spawnCount`); a vehicle only with CJ on foot | `Vehicles::Spawn`, or the jetpack or parachute (`CjGear::Give`) |
| `SaToggle` | on or off, sent again when the link comes back up | the toggle set to the state Skyrim sends |
| `SaOnce` | | its cheat function, once |
| `Suicide` (SZCMAWO) | 1,000 San Andreas points through `Vitals::Hurt`, armour first: Wasted | |
| `NoMercy` (ours) | see below | |
| `Weapon` (ours: MINIGUN) | `WeaponCommand` `GiveWeapon` of the minigun (38) with 500 rounds | gives it, its model loaded |
| `Respray` (ours: PAINT) | only with CJ in a vehicle | `Vehicles::Respray`: the model's next colour set (`CVehicleModelInfo::ChooseVehicleColour`), or any of carcols' for a model with one, `Fix`, health 1000, fire out, its model built and sent again in the new colours |
| `Hydraulics` (ours: LOWRIDER) | only with CJ in a car | `Vehicles::AddHydraulics`: upgrade 1087 (`CVehicle::AddVehicleUpgrade`) unless its handling says `HYDRAULIC_NONE`; the horn raises and lowers it, R3 (`kButtonHydraulics`) with the right stick bounces it (`CAutomobile::HydraulicControl`) |

AhShitRim's own codes (NOMERCY, MINIGUN, PAINT, LOWRIDER) and the vehicles' names match by their letters, not by San Andreas's key, from 3 letters long, and the top row's digits count. The names are every model in the `cars` section of the player's own `data/vehicles.ide` (`Cheats::ParseVehicleNames`: the model name upper case, letters and digits only), read at data load; one typed asks for that model as a spawn key does. Of all the cheats the shortest run of the newest letters wins, so a name can go off inside a longer code (MONSTERMASH brings MONSTER); the protocol tests type every code and its alternatives against the player's real `vehicles.ide` when `SaPath` is set. A Cheat command of ours has `cheat` 0xFF (`SaCheat::None`) and our code's key, and San Andreas finds it among `kCheats`' rows.

`CheatCommand` (8 bytes, v20): `key` u32 (San Andreas's key of the code), `cheat` u8 (its `eCheats` index as plugin-sdk numbers them), `flags` u8 (`kCheatOn` 0x1: a toggle is on; set for a one-shot cheat), 2 reserved bytes. San Andreas looks the key up in its own `CCheat::m_aCheatHashKeys` (0x8A5CC8), at `cheat` if it is there, else the last index that has it (as `AddToCheatString` picks), and applies it as `CCheat::ApplyCheat` would: a cheat with a function (`m_aCheatFunctions`, 0x8A5B58) runs it when `kCheatOn` is set, one without is a toggle, `m_aCheatsActive` (0x969130) set to `kCheatOn`. With flying cars on, AhShitRim's extra air control leaves cars alone (San Andreas flies them like planes); with infinite ammo the rounds never go down, so the pose's `shots` count the bullets sent instead.

San Andreas's toggles don't outlive a load, so a loaded save or a new game turns them all off, and BAGUVIX with them. Skyrim sends every toggle as it is (off ones too) after a load, whenever the link comes up and after a send that failed, so a San Andreas that restarted or missed one agrees again. Log lines: `cheats: CODE typed: WHAT on|off|given`, `vitals: the player takes no damage (BAGUVIX)` in Skyrim's log; `cheats: N cheats found in San Andreas's table`, `cheats: San Andreas's cheat N done|on|off` in San Andreas's.

**NOMERCY** (`protocol/cpp/mercy.h`): on, every actor base's essential flag (`TESNPC`, `ACTOR_BASE_DATA::kEssential`) is cleared once and every loaded actor's own (`Actor::boolFlags` `kEssential`, which its base and a quest alias set) every second, the player's never (it is essential for Wasted). Each one cleared is noted, an actor with its base; off, those get their flag back, an actor only if it still has that base. The co-save has a record of its own for it, `ASCH` version 1: on (u8), the bases (u32 count, form ids), the actors (u32 count, form id and base each). The bases' flags are this session's memory, not the save's, so a load first gives back the ones this session cleared, then takes the save's record and, if it is on, clears again on the next scan.

### Shots

San Andreas's world has no one in it, so nothing CJ fires hurts anything there; Skyrim decides what each shot hits (`sa/src/cj_shots.cpp`, `skse/src/player/Shots.cpp`, the numbers in `protocol/cpp/damage.h`). San Andreas sends a `ShotEvent` (64 bytes, v13) for each:

| Offset | Field | |
|---|---|---|
| 0 | `handover` | u32: the `InputState.handover` `origin` belongs to; Skyrim drops a shot of another |
| 4 | `weaponType` | u8: San Andreas's `eWeaponType` (0 for fists) |
| 5 | `kind` | u8: `kShotBullet` 0 (a bullet, or a shotgun's shot), `kShotMelee` 1 (a blow at its hit frame), `kShotFlame` 2 (v15: a puff of the flamethrower, or, with no layout change, of the spray can (`weaponType` 41) or the fire extinguisher (42); `range` is how far it reaches) |
| 6 | `skill` | u8: CJ's skill level with the weapon when it fired: 0 poor, 1 gangster, 2 hitman |
| 7 | `flags` | u8: `kShotSpread` 0x1 (a shotgun's shot: Skyrim fans it out into its pellets), `kShotFirstPerson` 0x2 (from San Andreas's first-person camera: the sniper scope, the first-person rifle) |
| 8 | `strike` | u32, melee: counts CJ's blows. A blow can come several frames running (the chainsaw's cut, a slow swing): the same count is the same blow |
| 12 | `damage` | f32: San Andreas's damage for it, its health points: `weapon.dat`'s per bullet (a shotgun's per pellet) at CJ's skill, `melee.dat`'s for the blow's move |
| 16 | `origin[3]` | where the bullet starts, or the centre of the blow, relative to `InputState.anchor`, Skyrim units |
| 28 | `direction[3]` | unit vector: the bullet's line, after San Andreas's spread; for a blow the way CJ faces |
| 40 | `range` | Skyrim units: how far the bullet reaches (`weapon.dat`'s range, or the line San Andreas worked out if longer); for a blow its radius |
| 44 | reserved | 20 bytes |

Where they come from: `CWeapon::FireInstantHit` (0x73FB10) tests each line it fires after calling `CBirds::HandleGunShot` with its start and end, hit or not, at four call sites the ASI points at itself (a shotgun's shot is one line there: San Andreas's pellets are only its bullet traces); the first-person rifles fire through `CWeapon::DoBulletImpact` (0x73B550) instead; the sniper rifle fires a moving bullet from the camera instead (`CWeapon::FireSniper`, 0x73AAC0), sent along the camera's front; a blow lands in `CTaskSimpleFight::FightStrike` (0x6240B0) at the move's hit frame, with the blow's centre, and its radius (0x30) and damage (a byte at 0x55) per move come from `melee.dat`'s combo data at 0xC170D0. The ASI hooks all three once the other plugins have patched the game, and sends only CJ's. A third-person bullet's spread is eased in: the first of a burst goes straight down San Andreas's crosshair ray, and the line keeps more of San Andreas's spread the longer the fire button is held (`CPlayerData::m_fAttackButtonCounter`, which also grows the crosshair, `aimRadius`).

Skyrim fans a shotgun's shot out into 8 pellets (the combat shotgun's 6) in a cone of 3 to 6 degrees, the same pattern every time (`Damage::PelletDirection`), and ray tests each bullet and pellet through its Havok world from `origin` along `direction` for `range` on the collision layer `[Weapons] ShotLayer` in `AhShitRim.ini` (6, the projectiles', by default), passing through the player's own body, the dead and what doesn't stop a bullet (clutter, debris, triggers, invisible walls): a static, furniture, a tree, the land or water stops it (Skyrim's impact for its material, from the Hunting Bow's impact set, stone's for a shape without one, two and a half times the size), and the first living actor takes it. Havok gives the actor's capsule, not a bone, so the line passing within a head's radius of the head bone is a headshot and a hit low on the body a leg. A blow lands on each living actor whose capsule it reaches, once per `strike`, the chainsaw every 0.1 s or more for as long as it cuts; a blow does two and a half times its `melee.dat` damage, the chainsaw 150 a second, and staggers its target by chance (the chainsaw every time). The damage (`Damage::ToSkyrim`): San Andreas's times 0.8, times 3 on the head (10 with the sniper and country rifles), 0.75 on a leg, less for targets above level 6 (down to 0.4), a half for giants and mammoths and a quarter for dragons; Skyrim's difficulty doesn't scale it. It goes through Skyrim's own hit with the player as the aggressor (`HitData`, the call the melee hit makes at its 0x4A8), so kill credit, crime and aggro are Skyrim's, and the race's blood shows where it went in. Each hit is counted for its weapon and sent back as a `Hit` command.

### Projectiles

San Andreas's projectiles would fly and blow up in an empty world, so they fly in Skyrim instead (`sa/src/cj_projectiles.cpp`, `skse/src/player/Projectiles.cpp`, the rules in `protocol/cpp/explosives.h`). When `CProjectileInfo::AddProjectile` (0x737C80) makes one of CJ's, or (v19) of the vehicle he drives (the Hunter's rockets and the Hydra's missiles, `CVehicle::FireUnguidedMissile` and `FireHeatSeakingMissile`) (types 16 grenade, 17 tear gas, 18 Molotov, 19 rocket, 20 heat seeker, and from v17 39 satchel charge), the ASI finds the slot it filled in `gaProjectileInfo` (0xC891A8) and `CProjectileInfo::ms_apProjectile` (0xC89110), reads the `CProjectile` as San Andreas made it, sends a `ProjectileEvent` and takes it out of San Andreas's world again without a blast (`CProjectileInfo::RemoveIfThisIsAProjectile`). One San Andreas had no room to throw (`CProjectileInfo::RemoveNotAdd`, 0x737C00, which would blow it up where it is) is sent with `kProjectileBurst` instead, and San Andreas's own is skipped.

| Offset | Field | Meaning |
|---|---|---|
| 0 | `handover` | u32: the handover `origin` belongs to; Skyrim drops one of an older handover |
| 4 | `weaponType` | u8: the projectile type San Andreas gave `AddProjectile`; 40 (v17) is the detonator, not a projectile: Skyrim blows every satchel CJ has out (only `handover` and `id` are set) |
| 5 | `flags` | u8: `kProjectileBurst` 0x1, it goes off at `origin` at once |
| 6 | `partCount` | u8: parts of its model in `parts`, 0 when there is nothing to draw |
| 8 | `model` | u32: the `ModelChunkHeader.model` it is drawn with, 0 for none |
| 12 | `fuseMs` | u32: milliseconds until it goes off by itself (`gaProjectileInfo`'s destroy time: a grenade's fuse, a rocket's reach), 0 for Skyrim's default |
| 16 | `origin[3]` | where it starts, relative to `InputState.anchor`, Skyrim units |
| 28 | `velocity[3]` | Skyrim units a second (`m_vecMoveSpeed` times 50 steps a second) |
| 40 | `rotation[4]` | its orientation, a unit quaternion x, y, z, w |
| 56 | `parts[2]` | `BodyBone`s (28 bytes each, `kProjectilePartMax`): each part of the model relative to the projectile, Skyrim units |
| 112 | `id` | u32: San Andreas's number for it, which an `Effect` names when it is over |
| 116 | `reserved[3]` | zero |

A projectile's model is a `kSkinModelWeapon` built the way a held weapon's is, from the `CProjectile`'s own clump or atomic (the thrown weapon's model, or the missile), sent once per projectile type under `ModelSender::kProjectileKey` plus the type less 16 (the satchel's is 30), and again whenever the link comes up. Skyrim builds a copy of the model for each further one of a type drawn at once (`ModelView::AddCopy`, even ids from 0xA5C00002).

Skyrim flies it in steps of at most 1/30 s under San Andreas's gravity (20 m/s²; a rocket flies straight, at 40 m/s at least), ray testing each step as a bullet is: a rocket and a Molotov go off where they touch a static, the land, water or a living actor, a grenade and tear gas bounce (35% of the speed into the surface back, 70% along it) and lie still once slow, until their fuse runs out. A rocket or grenade blast places a copy of Skyrim's `FireBallExp01` explosion made at startup without its sounds (no damage of its own) and hurts every actor within 7 m (a grenade's 6 m) through the same hit as a bullet (`Damage::ToSkyrim` of 300 San Andreas points in the middle quarter, down to a tenth at the edge, never cut for the target's level or size: about 240 to anyone in the middle, five rockets for a mammoth), knocks the ones in the inner two thirds over (`AIProcess::KnockExplosion`; not giants, mammoths or dragons), and unlocks every locked door and container within 3 m whose lock isn't key-only, the other side of a load door too, with `TrespassAlarm` (a crime if someone sees it) where activating it would be a crime. A Molotov bursts into the same fireball at twice the size (San Andreas's is bigger than a rocket's), does 40 points within 2.5 m and sets those it catches burning (the Flames spell, cast by the player), and its fire spreads on the ground as San Andreas's does: 13 patches of `TrapOilHazard01`'s burning oil (`Effects\FXFireOilHazard.nif`, drawn alone), the middle at once, 4 at 1.5 m from 3 s and 8 at 2.5 m from 6 to 9.5 s, all burning until 30 s after the burst (`Explosives::FirePatch`); whoever stands in one takes 20 points a second and is set burning, and anyone set burning burns on for 6 s more at 15 points a second. A satchel charge (v17) sticks where it first touches anything instead of bouncing: an actor's nearest skeleton node, another reference's 3D, or the land, keeping its place and turn on that node (`Explosives::StickTo`) until the detonation, which blows each with a grenade's blast (locks too). Every projectile is dropped when the player changes interior or worldspace. Tear gas lets out `DA13PustuleGasCloud` (harmless itself) for 15 s, which chokes everyone within 4 m for 10 points a second and staggers them every 2 s. CJ caught in any of it takes the same points through `Vitals::Hurt`, armour first.

Skyrim tells San Andreas what happened with an `EffectEvent` (32 bytes, the ring to San Andreas), so San Andreas's own sounds play: `handover` u32, `kind` u8, `weaponType` u8 (the projectile type), 2 reserved bytes, `projectile` u32 (a `ProjectileEvent.id`), `at[3]` (relative to the anchor, Skyrim units), `seconds` f32 and 4 reserved bytes.

| `kind` | San Andreas |
|---|---|
| `ProjectileGone` 1 | drops the stand-in it kept of projectile `projectile` |
| `Explosion` 2 | plays its explosion sound at `at` (`CAEExplosionAudioEntity::AddAudioEvent` on `CExplosion::m_ExplosionAudioEntity`; a Molotov's at -36 dB, as its own) and shakes its camera from there (`CCamera::CamShake`, 0.6, a Molotov's 0.2); for a Molotov, its stand-in is put at `at` and smashed (`CAudioEngine::ReportObjectDestruction`, the bottle's own sound) |
| `Fire` 3 | starts its own fire at `at` for `seconds` (`gFireManager.StartFire`): its crackle, and CJ catches fire walking into it |
| `BurnCj` 4 | sets CJ alight (`gFireManager.StartFire` on him): his scream, his burning and its damage |

A rocket, a Molotov and (v17) a satchel keep a stand-in in San Andreas's world: the `CProjectile` itself, without collision or gravity, its slot left out of `CProjectileInfo::Update` (hooked at 0x738B20) so San Andreas never blows it up. A rocket's flies along Skyrim's line, so the launch and roar San Andreas tied to it follow the rocket and stop when it goes off; a Molotov's waits to be smashed, and a satchel's keeps the detonator in CJ's hands. A stand-in no `ProjectileGone` came for goes 2 s after its fuse or with a new handover; a satchel's stays until the detonator (`CProjectileInfo::RemoveDetonatorProjectiles`, hooked at 0x738860, drops them before San Andreas's own runs and sends the detonation), its `ProjectileGone`, or a frame without `kInputOwnsPlayer` (Skyrim drops its satchels on F10). San Andreas's camera shake goes on its final matrix after the active camera is worked out, so while one lasts the body slot's camera position takes that offset too.

The heat seeker (v17) has nothing on the ring of its own. San Andreas locks only on to vehicles and fires a plain rocket (type 19) without a lock, so Skyrim locks on: while the pose's weapon is the heat seeker and `kWeaponAiming` is set (the lock is kept 0.3 s after, for a launch that comes in later), each living, loaded dragon, horse, rider, giant, mammoth and dwarven centurion within 250 m is rated from Skyrim's camera node (`PlayerCamera::cameraRoot`, where San Andreas's camera is put) as `CWeapon::EvaluateTargetForHeatSeekingMissile` rates a vehicle (`Explosives::HsRating`: the square root of its distance over 10 plus its distance off the aim line over its distance, out of the cone when that is 1/1.2 or more, the current target 4 times better), the best one tested for sight by a ray every second, and locked once it has been the one in sight for 1.5 s. The HUD draws `particle.txd`'s `target256` on it, white at alpha 100 while locking and red once locked. A rocket launched from the heat seeker while locked steers for its target (`Explosives::Steer`, 2.5 radians a second at most) and goes off within 1 m of its body; it counts as the heat seeker for damage and skill either way.

The Rhino's shell (v19) is no projectile in San Andreas: `CAutomobile::TankControl` tests a line 60 m out from the turret's muzzle and sets off `EXPLOSION_TANK_FIRE` where it hits or at its end, through `CExplosion::AddExplosion` (0x736A50, hooked). For CJ's Rhino that explosion is skipped and a `ProjectileEvent` sent instead: a rocket (19) without a model from the muzzle (`CAR_MISC_C`'s frame) towards that point at 200 m/s, its fuse the time to get there, so Skyrim blows it up on whatever is in the way or at the point. The bullets of the vehicle's guns (the Hunter's, `CVehicle::FireFixedMachineGuns`, fired with the vehicle as the shooter) go as CJ's shots, with his skill.

The flamethrower's puffs come from `CWeapon::FireAreaEffect` (0x73E800), aimed as it aims them (through the crosshair while the mouse turns the camera behind him, else ahead at his look pitch); San Andreas's own (fire creeping over its ground) is skipped for CJ. Skyrim launches the Flames spell's flames from the nozzle (every 0.05 s at most) and burns everyone in a 12 degree cone up to the weapon's range (2 to 8 m) for 80 points a second, counted per actor like the chainsaw's cuts, setting them burning.

The spray can's and the fire extinguisher's puffs come the same way, as `kShotFlame` shots with `weaponType` 41 and 42 (no layout change; the rules are in `protocol/cpp/explosives.h`, `ConeOf` to `HoseRadius`), and San Andreas's own still runs for them. The spray can is a 20 degree cone reaching 1 to 3 m: everyone in it chokes, 30 points a second through the same hit as a bullet (the player the aggressor), and coughs (a stagger) every 2 s. The extinguisher is a 15 degree cone reaching 2 to 8 m: whoever is in it coughs the same way, unhurt and not angered (as in San Andreas), and stops burning (the 6 s afterburn ends, every fire effect on them, those whose resist value is fire resistance, is dispelled, and the magic effects shown on them stop), and a Molotov's fire patch it reaches (the patch, up to knee height, touching the cone) goes out, its burning oil hidden. Both clouds are San Andreas's own (`protocol/cpp/puff_model.h`, `skse/src/player/Puffs.cpp`): the prims of its `spraycan` and `extinguisher` systems in `models/effects.fxp`, their sprites (`smoke`, `collisionsmoke`, `sphere`) read from the player's `models/effectsPC.txd` and drawn as camera-facing quads like the vehicles' smoke, the spray can's added onto what's behind (`kSkinMeshAdditive`, a model built in Skyrim only). Where something goes out, a few sprites of the extinguisher's froosh rise (San Andreas has no effect of its own for it). In San Andreas the ASI puts out CJ's own fire and every fire within the cone's width plus a patch's radius of points 1 m apart along its axis (`CFireManager::ExtinguishPoint`, 0x539450), which takes the Molotov fires Skyrim started there too.

### A weapon's model

Sent whenever CJ takes a weapon in his hand, and again whenever the link comes up (`sa/src/cj_weapons.cpp`), as a `SkinModel` of kind `kSkinModelWeapon` (2) in the same way as a vehicle's: every drawn atomic of the weapon's clump is a part and a bone (the `gunflash` atomic, San Andreas's own flash sprite, is left out), its vertices in the part's own space on that bone alone, a scale or mirror baked in, meshes that look the same merged. A pistol is one part and one mesh of about 200 vertices (28 KB), the minigun two parts (130 KB). The model is built from the weapon's clump the first time it is held and kept by the weapon's model index, under an id of its own for each hand (the left hand's copy is a second id), for good: Skyrim builds a model id once, so switching back to a weapon sends it again but builds nothing. Skyrim builds it like any other and bounds it like CJ.

The debug key (`=`, while San Andreas has the player; off with `[Debug] WeaponKey=0` in `AhShitRim.ini`, which should be set once cheats exist) gives the next of 5 steps, round and round, and says which in the help box: San Andreas's three weapon sets (the third has the chainsaw), the gear they leave out, and the katana (`Weapons::kDebugSteps`).

### A vehicle's model

Sent once per spawned vehicle, after its first `PreRender` (`sa/src/vehicles.cpp`, the game-free half in `protocol/cpp/vehicle.h`). Every atomic of the vehicle's clump is a part, and a bone, but the low-detail ones (`_vlo`): from v19 the hidden ones too (the damaged versions of parts, the standing or spinning rotor), drawn ones first so a model with more than 64 parts keeps those, and `hiddenParts` says which show (until v19 hidden atomics were left out). Each part's vertices are in its own frame's space and hang on its bone alone (weight 1). A part's frame can carry a scale or a mirror besides its rotation (wheels are scaled to the wheel size), which a bone can't, so it is baked into the part's vertices once (the frame is the bone's rotation times a constant matrix; a mirror turns the triangles round). Materials take the vehicle's own colours as San Andreas does right before drawing (the four paint placeholders from the colour table, the lights texture's materials white); one with alpha in its colour (glass) is `kSkinMeshBlend`. Parts' meshes that look the same are merged, so a vehicle comes as a few dozen meshes: the Sandking as 17 parts, 21 meshes, about 3,800 vertices and 13 textures (1.1 MB), the Sanchez as 7 parts and 8 meshes (870 KB).

Skyrim multiplies a textured mesh's texture by its colour (a vehicle's paint is a grey texture in the vehicle's colour) into a copy of the texture, since its shader has no tint. A blended mesh is drawn with alpha blending, its colour's alpha as the material's, without writing depth.

### Collision chunks

Chunks are `kCollisionChunkUnits` (2048 units, about 29 m) squares on a grid anchored at Skyrim's world (0, 0); chunk `(x, y)` starts at `(x * 2048, y * 2048)`. The payload is the 40-byte header, `vertexCount` vertices (`float[3]`, Skyrim units relative to `origin`) and `indexCount` triangle indices (u32). Triangles wind counterclockwise seen from outside in Skyrim (from above for the land); San Andreas swaps two corners per triangle, since it faces them the other way. `generation` is the `InputState.handover` the chunk belongs to: San Andreas builds chunks of the handover it has read, keeps newer ones until it reads that handover, and drops older ones. A chunk with the same x/y and generation replaces the one before. A chunk has at most 65,000 vertices (San Andreas indexes them with 16 bits); static triangles past that are left out and logged.

Skyrim side (`skse/src/collision/CollisionSampler.cpp`, `StaticMeshes.cpp`, ported from PerkeleRim; geometry in `protocol/cpp/collision.h`): while San Andreas has the player, each chunk is two parts in one payload.

- **Land**: a heightfield of 33 x 33 samples (64 units apart) from top-down Havok raycasts that keep only the landscape (the terrain or ground layer where it belongs to no reference) and pass through everything else. A ray that misses is tried again 8 units to each side; a sample still missed with at least three hit neighbours takes their mean height, three passes over, so only wide misses leave a hole. Interiors have no land and skip this.
- **Statics**: the real collision of every loaded reference with static collision (layers static, animated static, terrain, ground, trees, stair helpers and transparent, plus clutter on bodies that don't move), read from its Havok shapes: triangle meshes as they are, boxes and convex hulls as their faces, capsules as 8-sided prisms. Spheres and transform wrappers that don't read as a rotation are skipped and counted in the log. A triangle belongs to the chunk its center is in. When a door opens or closes, or a gate or trapdoor moves and settles, the chunks around it are read and sent again.

The 5 x 5 chunks around the player are sampled nearest first, then the 5 x 5 around a point 2 s of travel ahead (at most 3 chunks ahead, a car at 50 m/s), about 2 ms of raycasts per frame, 4 ms while the player moves faster than 10 m/s (a chunk takes about 6 frames, and a fast car needs a new row of 5 every half second). A chunk is done once at least 99% of its land samples hit and no static in it is still waiting for its 3D; otherwise it is sampled again every 2 s, every 0.4 s above 10 m/s (up to 5 tries), and resent when it improves. Ahead of a fast car Skyrim is often still loading the land (a chunk came with 33 of 1089 land samples at 40 m/s). Skyrim forgets chunks more than 3 away from both centers; San Andreas drops them more than 7 away from CJ, so a chunk San Andreas dropped is always one Skyrim resends.

San Andreas side (`sa/src/chunk_world.cpp`, the game-free half in `protocol/cpp/ground.h`): one invisible `CBuilding` per chunk, its model's `CColModel` holding the chunk's triangles, all of them tarmac (`SURFACE_TARMAC`: chunks carry no material yet), under the floating origin described in [DESIGN.md](DESIGN.md#ground-skyrims-collision-in-san-andreas).

Log lines for weapons, in San Andreas's log: `weapons: mouse aiming on`, `weapons: weapon set N given`, `weapons: weapon TYPE given`, `weapons: loadout set from Skyrim, N weapons, holding slot N`, `weapons: the NAME's model read in N ms: N parts (N skipped), N meshes, N vertices, N triangles (...), N textures: ...; N bytes`, `weapons: CJ holds weapon TYPE (model N)[, in both hands]`, `weapons: the NAME's model for the right|left hand ID sent to Skyrim in N chunks`, `weapons: nothing in his hand any more (...)`. In Skyrim's: `arms: the debug weapon key (=) is on|off`, `arms: debug key N: WHAT`, `arms: the loaded save holds slot N: TYPE:AMMO ...`, `arms: CJ's weapons sent to San Andreas, holding slot N: ...`, `muzzle flash: ...`, `sa hud: ... N weapon icons (N missing), scopes: ...`, `model view: NAME ID built|shown|hidden` for each weapon.

Log lines for CJ: `body: CJ's model ID read in N ms: N meshes, N vertices, N triangles (...), N bones (world|local space), N textures: ...`, `body: CJ's model ID sent to Skyrim in N chunks`, `body: texture NAME of CJ left white: why` in San Andreas's log; `model view: model ID CJ built in N ms: ...`, `model view: CJ ID attached under NODE`, `model view: CJ ID shown|hidden`, `player sync: CJ drawn from San Andreas's model ID, Skyrim's camera on San Andreas's (fov N)` in Skyrim's.

Log lines for vehicle damage: `vehicle: the NAME in slot N blew up, its model built again scorched` and `vehicle: the NAME's scorched model ID read in ...` in San Andreas's log; `vehicle fx: the vehicle in slot N smokes|burns|is a wreck|is fine[, black smoke] (health N)`, `vehicle fx: the smoke is San Andreas's bullethitsmoke (WxH) from PATH`, `vehicle fx: the WhiteSmoke|BlackSmoke model for slot N built (64 sprites)` and `explosives: 35 went off, N caught, CJ took N` for its blast in Skyrim's.

Log lines for vehicles, in San Andreas's log: `vehicle: NAME (MODEL) spawned in slot N at (x,y,z), spot N of 15 tried, colours N/N`, or why nothing was (`CJ is in a vehicle`, `no flat, clear spot for the NAME in front of CJ`, ...); `vehicle: the NAME's model ID read in N ms: N parts (N with a scale or mirror baked in; ...), N meshes, ...`, `vehicle: the NAME's model ID sent to Skyrim in N chunks`; `vehicle: driving at N m/s, N wheels on the ground, gas N, brake N, gear N, engine on|off, status N` every 5 s while CJ drives; `vehicle: the NAME in slot N removed|is gone`, `vehicle: CJ taken out of his vehicle` at a new handover; `ground: CJ's vehicle was below the ground at (...), put back at (...)`. In Skyrim's: `model view: NAME ID built|shown|hidden`.

Log lines: `collision: chunk (x,y) gen N try T: terrain hits/1089 (...), statics N triangles from N refs (...), ms over N frames + ms statics, sent|nothing new` in Skyrim's log; `ground: handover N at Skyrim (x,y,z), waiting for the ground under CJ`, `ground: the ground under CJ is in after S s, letting him go`, `ground: N chunks built (N resent, N triangles, N left out), N far ones dropped, N stale; N chunks around CJ` (every 5 s while anything happens), `ground: origin moved by (x,y,z) m with CJ`, `ground: CJ was below the ground at (...), put back at (...)` in San Andreas's.

## Handshake and liveness

Both sides run the same state machine (`Detached` → `Waiting` → `Up`, or `Disabled`), ticked on their own thread at 20 Hz, so it also runs in menus and while loading.

1. Create or open the section. A fresh section gets its header filled in and the magic written last; a joining side waits while the magic is still 0.
2. Unknown magic or header size: the link is disabled (`not an AhShitRim link`).
3. Each side publishes its `PeerInfo` (version, pid), even when the versions differ, so both sides can report a mismatch. If the creator's version, or the peer's, differs from ours, the side logs `protocol mismatch: ... speaks vN, this ... speaks vM` and stays disabled until restarted.
4. Each tick bumps our heartbeat. The link is **up** while the peer's heartbeat keeps moving. The first read only seeds the value, so a crashed peer never looks alive. It goes **down** after 2 s without a heartbeat change, or at once when the peer detaches cleanly (pid set to 0 on exit).

Log lines on both sides, prefixed `link:`:

- `started (protocol v12)`
- `created shared memory, waiting for peer` / `joined shared memory, waiting for peer`
- `link up: <peer> pid N, protocol v12`
- `link down: the <peer> detached` / `link down: no heartbeat from the <peer> for 2000 ms`
- `link disabled: ...`

The Skyrim side logs to `Documents\My Games\Skyrim Special Edition\SKSE\AhShitRim.log`, the San Andreas side to `<SaPath>\scripts\AhShitRim.log`. `scripts/logs.ps1` shows both.
