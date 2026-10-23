// AhShitRim link protocol: the layout of the shared memory both halves map.
//
// Source of truth for the layout, shared by the SKSE plugin (64-bit) and the San Andreas ASI
// plugin (32-bit). Explained in docs/PROTOCOL.md.
//
// Rules:
// - Everything is packed (no implicit padding), little-endian, fixed size, and uses only
//   fixed-width types: no pointers, size_t or long, so x86 and x64 see the same bytes. The
//   static_asserts below run in both builds.
// - LinkHeader is frozen across versions, so a peer can always read the other side's version.
// - Any other change to a struct, offset or constant bumps kProtocolVersion.

#pragma once

#include <cstddef>
#include <cstdint>

namespace AhShitRim::Protocol
{
    inline constexpr std::uint32_t kMagic = 0x4D525341;  // "ASRM" in memory
    inline constexpr std::uint32_t kProtocolVersion = 22;

    // Shared memory section, per Windows session. Whichever game starts first creates it.
    inline constexpr const wchar_t* kMappingName = L"Local\\AhShitRim.Link";

    // A peer counts as gone when its heartbeat hasn't moved for this long.
    inline constexpr std::uint32_t kPeerTimeoutMs = 2000;

    // Collision chunks are squares of this many Skyrim units (about 29 m) on a grid anchored at
    // Skyrim's world (0, 0): chunk (x, y) starts at (x * size, y * size).
    inline constexpr std::uint32_t kCollisionChunkUnits = 2048;

    // CJ's skeleton has 32 bones; room for any San Andreas ped's. A vehicle's parts (chassis, wheels,
    // doors, ...) go in the same 64 (v6).
    inline constexpr std::uint32_t kBodyBoneMax = 64;

    // Vehicles San Andreas keeps at once (v6): spawning another removes the oldest one CJ isn't in.
    inline constexpr std::uint32_t kVehicleMax = 4;

    // Weapon models San Andreas has in CJ's hands at once (v12): one per hand, a twin pistol draws the
    // same model in both.
    inline constexpr std::uint32_t kWeaponHandMax = 2;

    // San Andreas's weapon slots (CPed::m_aWeapons): the loadout in the pose and the save has one entry
    // per slot (v12).
    inline constexpr std::uint32_t kWeaponSlotCount = 13;

    // San Andreas's weapon skills (v13): its stats STAT_PISTOL_SKILL (69) to STAT_SNIPERRIFLE_SKILL (79), in that
    // order, each 0 to 1000 (poor, gangster, hitman).
    inline constexpr std::uint32_t kWeaponSkillCount = 11;

    // Section layout (byte offsets from the start of the mapping): the header, the latest-value
    // slots, the ring from Skyrim to San Andreas, then the ring back.
    inline constexpr std::uint32_t kOffsetHeader = 0;
    inline constexpr std::uint32_t kOffsetInputSlot = 128;
    inline constexpr std::uint32_t kOffsetPlayerSlot = 272;              // v9: after the larger input slot
    inline constexpr std::uint32_t kOffsetAudioSlot = 576;               // v7; v12, v13: after the larger player slot
    inline constexpr std::uint32_t kOffsetBodySlot = 640;                // v5
    inline constexpr std::uint32_t kOffsetVehicleSlots = 2560;           // v6: kVehicleMax BodySlots
    inline constexpr std::uint32_t kOffsetWeaponSlots = 10240;           // v12: kWeaponHandMax BodySlots
    inline constexpr std::uint32_t kOffsetGearSlot = 14080;              // v19: one BodySlot, the jetpack or the parachute on CJ
    inline constexpr std::uint32_t kOffsetRingToSanAndreas = 16000;      // RingHeader, data follows
    inline constexpr std::uint32_t kRingToSanAndreasCapacity = 4194304;  // 4 MiB: collision chunks
    inline constexpr std::uint32_t kOffsetRingToSkyrim = 4210368;        // v5
    inline constexpr std::uint32_t kRingToSkyrimCapacity = 4194304;      // 4 MiB: CJ's, the vehicles', the weapons' and the gear's models, the shots
    inline constexpr std::uint32_t kMappingSize = 8404736;               // 4210368 + 64 + 4 MiB

    // A model goes over in ModelChunk messages of at most this many bytes, and is at most
    // kModelSizeMax whole (protocol/cpp/skin_model.h).
    inline constexpr std::uint32_t kModelChunkMax = 262144;
    inline constexpr std::uint32_t kModelSizeMax = 16777216;
    inline constexpr std::uint32_t kModelTextureSizeMax = 2048;
    inline constexpr std::uint32_t kModelMeshVertexMax = 65535;

    enum class Side : std::uint32_t
    {
        Skyrim = 0,
        SanAndreas = 1,
    };

#pragma pack(push, 1)

    // ---- Header (frozen) ----

    // One per side, written only by that side.
    struct PeerInfo
    {
        std::uint32_t version;    // kProtocolVersion of that side, 0 while not attached
        std::uint32_t pid;        // process id, 0 after a clean detach
        std::uint64_t heartbeat;  // bumped every tick while attached
        std::uint64_t reserved[2];
    };

    struct LinkHeader
    {
        std::uint32_t magic;           // written last by the creator: 0 means "still initializing"
        std::uint32_t headerSize;      // sizeof(LinkHeader)
        std::uint32_t mappingSize;     // kMappingSize of the creator
        std::uint32_t creatorVersion;  // kProtocolVersion of the creator
        PeerInfo      peers[2];        // indexed by Side
        std::uint8_t  reserved[48];
    };

    // ---- Latest-value slots (seqlock) ----
    //
    // Continuous state: the reader only wants the newest value, so there is no queue.
    // Writer: seq += 1 (odd = writing), write value, seq += 1. Reader: read seq, copy value,
    // read seq again; retry if the two differ or seq is odd. seq == 0 means never written.

    inline constexpr std::uint32_t kInputOwnsPlayer = 0x1;  // InputState.flags: San Andreas should apply this input
    inline constexpr std::uint32_t kInputPaused = 0x2;      // InputState.flags: a Skyrim menu pauses the game, San Andreas freezes its time
    inline constexpr std::uint32_t kInputFace = 0x4;        // InputState.flags (v9): CJ stands still and turns to sceneHeading (the one he talks to)
    inline constexpr std::uint32_t kInputScripted = 0x8;    // InputState.flags (v9): Skyrim moves the player in a scene; CJ follows sceneOffset and sceneHeading
    inline constexpr std::uint32_t kInputSeated = 0x10;     // InputState.flags (v9), with kInputScripted: the player sits
    inline constexpr std::uint32_t kInputWasted = 0x20;     // InputState.flags (v11): the player is dying (Wasted), San Andreas ignores damage; (v14) CJ falls dead on his front
    inline constexpr std::uint32_t kInputWastedCamera = 0x40;  // InputState.flags (v14), with kInputWasted: San Andreas's death camera rises over him
    inline constexpr std::uint32_t kInputBusted = 0x80;     // InputState.flags (v22): Busted, CJ is arrested: dragged out of the car he is in (the driver's door), knocked off a bike, or on foot his hands up

    inline constexpr std::uint32_t kButtonSprint = 0x1;  // InputState.buttons, held
    inline constexpr std::uint32_t kButtonJump = 0x2;
    inline constexpr std::uint32_t kButtonCrouch = 0x4;  // San Andreas toggles crouch on the press
    inline constexpr std::uint32_t kButtonWalk = 0x8;
    inline constexpr std::uint32_t kButtonEnterExit = 0x10;  // v6: get in or out of a vehicle
    inline constexpr std::uint32_t kButtonHorn = 0x20;       // v6
    inline constexpr std::uint32_t kButtonLeanForward = 0x40;  // v6: up arrow; on foot it walks forwards, on a bike it leans forward
    inline constexpr std::uint32_t kButtonLeanBack = 0x80;     // v6: down arrow; on foot back, on a bike it leans back (wheelies)
    inline constexpr std::uint32_t kButtonHandbrake = 0x100;   // v8: in a vehicle, the handbrake (Space in both key layouts, also Right Ctrl in the classic one)
    inline constexpr std::uint32_t kButtonFire = 0x200;        // v12: left mouse; on foot it fires the weapon in CJ's hands (a melee weapon attacks)
    inline constexpr std::uint32_t kButtonAim = 0x400;         // v12: right mouse; on foot it aims the weapon (San Andreas's lock-on, free aim and scopes); (v19) in a vehicle its second weapon
    inline constexpr std::uint32_t kButtonLookLeft = 0x800;    // v19: Q, in a vehicle: look left; an aircraft's rudder left
    inline constexpr std::uint32_t kButtonLookRight = 0x1000;  // v19: E, in a vehicle: look right; the rudder right
    inline constexpr std::uint32_t kButtonTurretLeft = 0x2000;   // v19: numpad 4, in a vehicle: San Andreas's turret keys (its right stick)
    inline constexpr std::uint32_t kButtonTurretRight = 0x4000;  // v19: numpad 6
    inline constexpr std::uint32_t kButtonTurretUp = 0x8000;     // v19: numpad 8 or Page Up: the Hydra's nozzles back towards forward flight
    inline constexpr std::uint32_t kButtonTurretDown = 0x10000;  // v19: numpad 2 or Page Down: the Hydra's nozzles down towards hovering
    inline constexpr std::uint32_t kButtonHydraulics = 0x20000;  // v20: numpad 5, in a vehicle: San Andreas's R3, the hydraulics' jump with the turret keys

    // Skyrim -> San Andreas: what the player does with CJ, as actions rather than keys.
    struct InputState
    {
        std::uint32_t frame;       // Skyrim frame counter
        std::uint32_t flags;       // kInput*
        double        mouseX;      // accumulated raw mouse counts since Skyrim started; San Andreas applies the delta
        double        mouseY;      // likewise, positive down (DirectInput's direction)
        float         moveX;       // -1..1, positive to the right
        float         moveY;       // -1..1, positive forwards (W and S; the up and down arrows are kButtonLean*)
        std::uint32_t buttons;     // kButton*
        std::uint32_t handover;    // counts handovers and re-anchors (v8: a cell change, fast travel); a new value puts CJ at anchor and drops older collision chunks and every vehicle
        float         heading;     // the Skyrim player's heading at that handover, Skyrim radians; CJ spawns facing it
        float         anchor[3];   // the Skyrim player's feet at that handover, Skyrim world units (v4)
        std::uint16_t spawnModel;  // v6: San Andreas vehicle model id to spawn in front of CJ; (v19) 370 puts the jetpack on him, 371 gives him the parachute
        std::uint16_t spawnCount;  // v6: counts spawn requests; a new value spawns spawnModel
        std::uint16_t radioNext;   // v7: counts mouse wheel steps up; in a vehicle each one tunes the next station, on foot (v12) it switches to the next weapon
        std::uint16_t radioPrev;   // v7: counts wheel steps down, the previous station or (v12) the previous weapon
        float         sceneOffset[3];  // v9, kInputScripted: the Skyrim player's feet relative to anchor
        float         sceneHeading;    // v9, kInputScripted: the player's heading; kInputFace: the heading CJ turns to (Skyrim radians)
        std::uint16_t shoutCount;      // v9: counts shouts the player let out; a new value plays CJ's gesture
        std::uint16_t carModel;        // v11: San Andreas vehicle model id to put CJ in once the ground is in (a loaded save's car), 0 = none; only read with a new handover
        std::uint16_t insultCount;     // v16: counts presses of the insult key; a new value has CJ insult whoever's about, with the finger on foot
        std::uint8_t  carColours[4];   // v11: the car's four colour table indices
        std::uint16_t swearCount;      // v16: counts presses of the swear key; a new value has CJ swear to himself
        float         carOffset[3];    // v11: the car relative to the anchor, Skyrim units
        float         carHeading;      // v11: the car's heading, Skyrim radians
        std::uint16_t lineCount;       // v21: counts lines Skyrim asks CJ to say; a new value has him say one of lineKind
        std::uint8_t  lineKind;        // v21: Dialogue::Category (protocol/cpp/dialogue.h): greet, agree, refuse, ask, threaten, joke, goodbye
        std::uint8_t  spawnFlags;      // v22 (a reserved byte before): kSpawn*, how the spawn request with spawnCount wants spawnModel
    };

    inline constexpr std::uint8_t kSpawnAirborne = 0x1;  // InputState.spawnFlags: a plane comes flying, CJ at its controls (AIR + its name)

    struct InputSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        InputState    value;
        std::uint8_t  pad[24];  // v9: keeps the later slots on a 64-byte boundary
    };

    inline constexpr std::uint32_t kPlayerValid = 0x1;     // PlayerState.flags: San Andreas is simulating CJ for this handover
    inline constexpr std::uint32_t kPlayerGrounded = 0x2;   // PlayerState.flags: CJ stands on something
    inline constexpr std::uint32_t kPlayerCrouching = 0x4;  // PlayerState.flags: CJ crouches (v3); Skyrim sneaks
    inline constexpr std::uint32_t kPlayerInVehicle = 0x8;  // PlayerState.flags: CJ drives or rides a vehicle (v6)
    inline constexpr std::uint32_t kPlayerOnFire = 0x20;  // PlayerState.flags (v15): CJ burns in San Andreas; Skyrim shows flames on him
    inline constexpr std::uint32_t kPlayerRadioRetuning = 0x10;  // PlayerState.flags (v7): the radio is between stations

    inline constexpr std::int8_t kRadioNone = -1;  // PlayerState.radioStation (v7): no station plays

    inline constexpr std::uint16_t kWeaponInHand = 0x1;      // PlayerState.weaponFlags (v12): a weapon model is in CJ's hand (weapon slot 0 draws it)
    inline constexpr std::uint16_t kWeaponTwin = 0x2;        // a twin pistol: the same model in the other hand too (weapon slot 1)
    inline constexpr std::uint16_t kWeaponAiming = 0x4;      // CJ aims it: right mouse held, San Andreas's aim camera is up
    inline constexpr std::uint16_t kWeaponFiring = 0x8;      // it fires right now (San Andreas's weapon state)
    inline constexpr std::uint16_t kWeaponReloading = 0x10;  // it reloads
    inline constexpr std::uint16_t kWeaponMelee = 0x20;      // a melee weapon or fists: there is no ammo to show

    inline constexpr std::uint8_t kAmmoTotal = 0;  // PlayerState.ammoStyle (v12): the rounds in all ("200"), for a weapon with no clip to speak of
    inline constexpr std::uint8_t kAmmoClip = 1;   // the rounds left, a dash, the clip ("83-17")
    inline constexpr std::uint8_t kAmmoFlame = 2;  // as a clip, in tenths: the flamethrower's fuel

    // One of San Andreas's weapon slots (v12). type 0 = the slot is empty.
    struct WeaponEntry
    {
        std::uint8_t  type;      // San Andreas's eWeaponType
        std::uint8_t  reserved;
        std::uint16_t clip;      // rounds in the clip (m_nAmmoInClip)
        std::uint32_t ammo;      // rounds in all, the clip included (m_nAmmoTotal), at most 99999
    };

    // San Andreas -> Skyrim, every San Andreas frame. Skyrim axes and units: game units, Z up,
    // heading 0 = north, clockwise; pitch positive looks down.
    struct PlayerState
    {
        std::uint32_t frame;        // San Andreas frame counter
        std::uint32_t flags;        // kPlayer*
        float         offset[3];    // CJ's feet relative to where the handover spawned him
        float         velocity[3];  // units per second
        float         heading;      // CJ's body, radians
        float         viewHeading;  // San Andreas's camera, radians; Skyrim's first-person camera follows it
        float         viewPitch;    // radians
        std::uint32_t handover;     // InputState.handover this pose belongs to
        std::int8_t   radioStation;  // v10: the radio dial's station in CJ's vehicle (Radio::StationName: 1..11 San Andreas's, 12 Radio Sovngarde, 13 Sweetroll FM, 14 off), kRadioNone if none
        std::uint8_t  reserved0;
        std::uint16_t vehicleModel;  // v11: San Andreas model id of the vehicle CJ is in, 0 on foot
        std::uint8_t  vehicleColours[4];  // v11: its colour table indices
        float         vehicleOffset[3];   // v11: the vehicle relative to CJ's feet, Skyrim units
        float         vehicleHeading;     // v11: its heading, Skyrim radians
        float         damage;             // v11: damage San Andreas has worked out for CJ since it started, in its health points (100 = a full health bar); only counted while Skyrim has the player
        std::uint32_t reserved1;
        std::uint8_t  weaponType;   // v12: the weapon in CJ's hands (San Andreas's eWeaponType), 0 = fists
        std::uint8_t  weaponSlot;   // v12: its slot, an index into loadout
        std::uint16_t weaponFlags;  // v12: kWeapon*
        std::uint16_t shots;        // v12: counts the shots fired (Skyrim flashes for each); wraps
        std::uint8_t  cameraMode;   // v12: San Andreas's camera mode (eCamMode), for the sniper and rocket scopes
        std::uint8_t  ammoStyle;    // v12: how the HUD writes the held weapon's rounds, kAmmo*
        float         muzzle[2][3];  // v12: where the shots leave, relative to CJ's feet, Skyrim units; [1] is the left hand's (kWeaponTwin)
        float         crosshair[2];  // v12: where San Andreas puts its crosshair on the screen, as fractions of its width and height
        WeaponEntry   loadout[kWeaponSlotCount];  // v12: San Andreas's weapons, one per slot
        std::uint16_t skills[kWeaponSkillCount];  // v13: San Andreas's weapon skill stats, 0..1000
        std::uint16_t aimRadius;  // v13: the free-aim crosshair's size in thousandths (CPlayerPed::GetWeaponRadiusOnScreen: 200 steady, growing as he fires)
    };

    struct PlayerSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        PlayerState   value;
        std::uint8_t  pad[48];  // v13: the audio slot follows on a 64-byte boundary
    };

    // Skyrim -> San Andreas, every Skyrim frame (v7): how loud San Andreas plays, from Skyrim's audio
    // sliders. 0 while Skyrim has the player; a pausing menu pauses San Andreas's sounds instead
    // (kInputPaused).
    struct AudioState
    {
        float         effects;  // 0..1: master x effects, for CJ, his vehicles and weapons
        float         music;    // 0..1: master x music, for the radio
        std::uint8_t  reserved[24];
    };

    struct AudioSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        AudioState    value;
        std::uint8_t  pad[24];  // v11: keeps the body slot on a 64-byte boundary
    };

    inline constexpr std::uint32_t kBodyValid = 0x1;    // BodyState.flags: the bones and camera are set
    inline constexpr std::uint32_t kBodyDriven = 0x2;   // BodyState.flags, vehicle slots (v6): CJ is in this vehicle
    inline constexpr std::uint32_t kBodyWrecked = 0x4;  // BodyState.flags, vehicle slots (v18): it blew up (San Andreas's STATUS_WRECKED)
    inline constexpr std::uint32_t kBodyBurning = 0x8;  // BodyState.flags, vehicle slots (v18): it burns: its engine (health under 250, about to blow up) or the wreck
    inline constexpr std::uint32_t kBodyJetpack = 0x10;    // BodyState.flags, the gear slot (v19): the jetpack on CJ's back
    inline constexpr std::uint32_t kBodyParachute = 0x20;  // BodyState.flags, the gear slot (v19): the parachute, open
    inline constexpr std::uint32_t kBodyHydraulics = 0x40;  // BodyState.flags, vehicle slots (v20): it has San Andreas's hydraulics (LOWRIDER)

    // One bone's skinning transform (v5): takes a vertex of the model (bind pose, Skyrim units) to
    // its place relative to CJ's feet (BodyState.feet), Skyrim axes and units:
    // v' = rotate(rotation, v) + translation.
    struct BodyBone
    {
        float rotation[4];  // unit quaternion x, y, z, w
        float translation[3];
    };

    // San Andreas -> Skyrim, every San Andreas frame (v5): CJ's skeleton and San Andreas's camera,
    // for drawing CJ and putting Skyrim's camera where San Andreas's is. feet and the camera are
    // relative to where the handover put CJ's feet (InputState.anchor in Skyrim), the bones to
    // feet; Skyrim axes and units. A vehicle slot (v6) holds one too: feet is the vehicle's
    // position, the bones its parts (one per drawn part, each part's vertices on its own bone) and
    // the camera is unused (zero). The gear slot (v19) holds the jetpack (its parts as a vehicle's) or the
    // open parachute (skinned like CJ), relative to CJ's feet like his bones.
    struct BodyState
    {
        std::uint32_t frame;             // San Andreas frame counter, as PlayerState.frame
        std::uint32_t flags;             // kBody*
        std::uint32_t handover;          // InputState.handover this belongs to
        std::uint32_t model;             // ModelChunkHeader.model the bones belong to
        std::uint32_t boneCount;         // bones in use, the model's SkinModelHeader.boneCount
        std::uint32_t timeMs;            // San Andreas's real-time clock, for Skyrim's interpolation
        float         feet[3];           // CJ's feet
        float         cameraPosition[3];
        float         cameraForward[3];  // unit vector
        float         cameraFov;         // degrees, horizontal for a 4:3 screen: Skyrim's own fov convention
        BodyBone      bones[kBodyBoneMax];
        float         cameraUp[3];       // v14: the camera's up, a unit vector (its roll: the death camera's swing); zero for none
        float         health;            // v18, vehicle slots: San Andreas's vehicle health (1000 new, it smokes under 650 and burns under 250); 0 elsewhere
        float         engine[3];         // v18, vehicle slots: where its engine is (the engine dummy), relative to feet; zero elsewhere
        std::uint64_t hiddenParts;       // v19: bit i set: bone i isn't drawn (a vehicle's damaged or whole version of a part, a helicopter's rotor
                                         // standing or spinning); bones past 63 are always drawn
    };

    struct BodySlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        BodyState     value;
        std::uint8_t  pad[20];
    };

    // ---- Rings (single producer, single consumer) ----
    //
    // One each way: Skyrim to San Andreas (collision chunks), San Andreas to Skyrim (CJ's model,
    // v5). head and tail are monotonic byte counts; position = count % capacity. A record is a
    // MessageHeader plus payload and may wrap around the end. The consumer drops whatever is left in
    // its ring when it attaches, so stale messages from an earlier session never arrive.

    struct RingHeader
    {
        std::uint32_t capacity;
        std::uint32_t reserved0;
        std::uint64_t head;
        std::uint8_t  reserved1[16];
        std::uint64_t tail;  // own 32-byte half, away from head
        std::uint8_t  reserved2[24];
    };

    enum class MessageType : std::uint16_t
    {
        CollisionChunk = 1,  // Skyrim -> San Andreas
        ModelChunk = 2,      // San Andreas -> Skyrim (v5): CJ's model, each vehicle's (v6) and CJ's weapons' (v12)
        WeaponCommand = 3,   // Skyrim -> San Andreas (v12): set CJ's weapons, give a set of them, (v13) count his hits
        Shot = 4,            // San Andreas -> Skyrim (v13): a bullet, a shotgun's shot or a blow CJ dealt, for Skyrim to hit test
        Projectile = 5,      // San Andreas -> Skyrim (v15): a rocket, grenade, tear gas or Molotov CJ launched, for Skyrim to fly
        Effect = 6,          // Skyrim -> San Andreas (v15): what CJ's explosives did, for San Andreas's own sound and fire
        Cheat = 7,           // Skyrim -> San Andreas (v20): a cheat typed in Skyrim that San Andreas has a part in
    };

    struct MessageHeader
    {
        std::uint16_t type;  // MessageType
        std::uint16_t reserved;
        std::uint32_t size;  // payload bytes after this header
    };

    // Payload: this header, then vertexCount * float[3] vertices (Skyrim units, relative to origin),
    // then indexCount uint32 triangle indices. Triangles wind counterclockwise seen from outside in
    // Skyrim space (from above for the land: cross(b - a, c - a) points up). A newer chunk with the
    // same x/y and generation replaces the older one.
    struct CollisionChunkHeader
    {
        std::int32_t  chunkX;
        std::int32_t  chunkY;
        std::uint32_t worldspace;  // form id
        std::uint32_t generation;  // InputState.handover this chunk belongs to
        float         origin[3];   // Skyrim world units
        std::uint32_t vertexCount;
        std::uint32_t indexCount;
        std::uint32_t reserved;
    };

    enum class WeaponCommandKind : std::uint32_t
    {
        SetLoadout = 1,  // replace CJ's weapons with entries (a loaded save, a new game: all empty); argument is the slot he holds
        GiveSet = 2,     // give one of San Andreas's weapon sets: 1 to 3 are its three cheats', 4 the gear they leave out; argument is the set
        GiveWeapon = 3,  // give one weapon; argument is its type, entries[0].ammo its ammo (at least 1)
        Hit = 4,         // v13: CJ hit someone in Skyrim; argument is the weapon's type, entries[0].ammo how many hits. Raises his skill
    };

    // Payload of a WeaponCommand message (v12).
    struct WeaponCommand
    {
        std::uint32_t kind;      // WeaponCommandKind
        std::uint32_t argument;
        WeaponEntry   entries[kWeaponSlotCount];  // SetLoadout only
        std::uint16_t skills[kWeaponSkillCount];  // v13, SetLoadout only: the weapon skill stats to set
        std::uint16_t reserved;
    };

    inline constexpr std::uint8_t kShotBullet = 0;  // ShotEvent.kind (v13): a bullet, or a shotgun's shot
    inline constexpr std::uint8_t kShotMelee = 1;   // a blow at a melee weapon's (or the fists') hit frame
    inline constexpr std::uint8_t kShotFlame = 2;   // v15: one puff of the flamethrower; range is how far it reaches
    inline constexpr std::uint8_t kShotSpread = 0x1;  // ShotEvent.flags: a shotgun's shot, which Skyrim fans out into its pellets (Damage::Pellets)
    inline constexpr std::uint8_t kShotFirstPerson = 0x2;  // fired from San Andreas's first-person camera (the scopes)

    // Payload of a Shot message (v13): San Andreas fired, Skyrim decides what it hit.
    struct ShotEvent
    {
        std::uint32_t handover;      // InputState.handover the origin belongs to
        std::uint8_t  weaponType;    // San Andreas's eWeaponType
        std::uint8_t  kind;          // kShot*
        std::uint8_t  skill;         // the skill level it was fired at: 0 poor, 1 gangster, 2 hitman
        std::uint8_t  flags;         // kShotSpread, kShotFirstPerson
        std::uint32_t strike;        // melee: counts CJ's blows; a blow hits each actor once, the chainsaw keeps cutting
        float         damage;        // San Andreas's damage for it (a shotgun's: per pellet), in its health points (weapon.dat, melee.dat)
        float         origin[3];     // where it starts (melee: the blow's centre), relative to InputState.anchor, Skyrim units
        float         direction[3];  // unit vector; melee: the way CJ faces
        float         range;         // Skyrim units: how far a bullet reaches; melee: the blow's radius
        std::uint32_t reserved[5];
    };

    inline constexpr std::uint8_t  kProjectileBurst = 0x1;  // ProjectileEvent.flags: San Andreas had no room to throw it, it goes off where it is
    inline constexpr std::uint32_t kProjectilePartMax = 2;  // parts of a projectile's model sent with it

    // Payload of a Projectile message (v15): San Andreas launched it (CProjectileInfo::AddProjectile) and
    // dropped its own; Skyrim flies it, draws it and blows it up. From v17 a satchel charge (39) goes the same
    // way and sticks where it lands, and weaponType 40 (the detonator) is no projectile but CJ pressing it:
    // Skyrim blows every satchel he has out (only handover and id count).
    struct ProjectileEvent
    {
        std::uint32_t handover;     // InputState.handover the origin belongs to
        std::uint8_t  weaponType;   // San Andreas's projectile type: 16 grenade, 17 tear gas, 18 Molotov, 19 rocket, 20 heat seeker, 39 satchel; 40 detonation
        std::uint8_t  flags;        // kProjectileBurst
        std::uint8_t  partCount;    // parts of its model in parts, 0 when it has none to draw
        std::uint8_t  reserved0;
        std::uint32_t model;        // ModelChunkHeader.model to draw it with (a kSkinModelWeapon), 0 for none
        std::uint32_t fuseMs;       // milliseconds until it goes off by itself (a grenade's fuse, a rocket's reach), 0 for never
        float         origin[3];    // where it starts, relative to InputState.anchor, Skyrim units
        float         velocity[3];  // Skyrim units a second
        float         rotation[4];  // its orientation, a unit quaternion x, y, z, w
        BodyBone      parts[kProjectilePartMax];  // each part of the model relative to the projectile
        std::uint32_t id;           // San Andreas's number for it, which an Effect names when it is over
        std::uint32_t reserved[3];
    };

    enum class EffectKind : std::uint8_t
    {
        ProjectileGone = 1,  // projectile `projectile` went off or is gone: San Andreas drops what it kept of it (a rocket's sound)
        Explosion = 2,       // a blast at `at`: San Andreas's explosion sound
        Fire = 3,            // fire on the ground at `at` for `seconds`: San Andreas's own (its crackle; CJ catches fire walking into it)
        BurnCj = 4,          // CJ was caught in fire: San Andreas sets him alight (his scream, the burn)
        KnockCj = 5,         // v22: a blow knocked the player down in Skyrim (a giant's club): San Andreas throws CJ away from `at` at `seconds` metres a second, with its own fall and get-up (protocol/cpp/knock.h)
    };

    // Payload of an Effect message (v15).
    struct EffectEvent
    {
        std::uint32_t handover;    // InputState.handover `at` belongs to
        std::uint8_t  kind;        // EffectKind
        std::uint8_t  weaponType;  // the projectile type it came from (16 grenade .. 20 heat seeker, 39 satchel), 0 for none
        std::uint16_t reserved0;
        std::uint32_t projectile;  // ProjectileEvent.id, ProjectileGone only
        float         at[3];       // relative to InputState.anchor, Skyrim units
        float         seconds;     // Fire only
        std::uint32_t reserved1;
    };

    inline constexpr std::uint8_t kCheatOn = 0x1;  // CheatCommand.flags (v20): a toggle is on (a one-shot cheat always has it)

    // Payload of a Cheat message (v20): San Andreas's own cheat, found in its tables by its key. One with a function
    // (CCheat::m_aCheatFunctions) runs it when kCheatOn is set; one without is a toggle, set to kCheatOn.
    struct CheatCommand
    {
        std::uint32_t key;       // San Andreas's key of the code (CCheat::m_aCheatHashKeys, protocol/cpp/cheats.h)
        std::uint8_t  cheat;     // San Andreas's eCheats index it should be at; the key decides
        std::uint8_t  flags;     // kCheatOn
        std::uint16_t reserved;
    };

    // Payload: this header, then size bytes of a model blob starting at offset. Chunks of one model
    // arrive in order; offset 0 starts a model.
    struct ModelChunkHeader
    {
        std::uint32_t model;      // id, new for every model San Andreas sends
        std::uint32_t totalSize;  // the whole blob
        std::uint32_t offset;
        std::uint32_t size;
    };

    // The model blob (v5, protocol/cpp/skin_model.h): SkinModelHeader, boneCount uint32 bone ids
    // (San Andreas's node ids, for the logs), the textures (SkinTexture + width * height RGBA8
    // bytes, top row first), then the meshes (SkinMesh + vertexCount SkinVertex + indexCount uint16
    // triangle indices).
    inline constexpr std::uint32_t kSkinTextureRgba8 = 1;
    inline constexpr std::uint32_t kSkinMeshCutout = 0x1;  // alpha tested
    inline constexpr std::uint32_t kSkinMeshBlend = 0x2;   // alpha blended
    inline constexpr std::uint32_t kSkinMeshAdditive = 0x4;  // with kSkinMeshBlend: added onto what's behind (Skyrim's own sprites only)
    inline constexpr std::uint32_t kSkinModelBody = 0;     // SkinModelHeader.kind: a ped (CJ)
    inline constexpr std::uint32_t kSkinModelVehicle = 1;  // v6: a vehicle, its parts as bones
    inline constexpr std::uint32_t kSkinModelWeapon = 2;   // v12: a weapon in CJ's hand, its parts as bones, bounded like CJ

    struct SkinModelHeader
    {
        char          name[32];
        std::uint32_t textureCount;
        std::uint32_t meshCount;
        std::uint32_t boneCount;
        std::uint32_t kind;  // kSkinModel* (v6)
    };

    struct SkinTexture
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t format;  // kSkinTextureRgba8
        std::uint32_t reserved;
    };

    struct SkinMesh
    {
        std::int32_t  texture;  // index into the textures, -1 = none
        std::uint32_t vertexCount;
        std::uint32_t indexCount;
        std::uint32_t flags;     // kSkinMesh*
        std::uint8_t  color[4];  // RGBA, multiplies the texture
        std::uint32_t reserved;
    };

    // Bind pose, Skyrim axes and units; triangles wind counterclockwise seen from outside.
    struct SkinVertex
    {
        float        position[3];
        float        normal[3];
        float        uv[2];
        std::uint8_t bones[4];  // BodyState.bones indices
        float        weights[4];
    };

#pragma pack(pop)

    // Frozen header layout. Never change these.
    static_assert(sizeof(PeerInfo) == 32);
    static_assert(offsetof(PeerInfo, pid) == 4);
    static_assert(offsetof(PeerInfo, heartbeat) == 8);
    static_assert(sizeof(LinkHeader) == 128);
    static_assert(offsetof(LinkHeader, headerSize) == 4);
    static_assert(offsetof(LinkHeader, mappingSize) == 8);
    static_assert(offsetof(LinkHeader, creatorVersion) == 12);
    static_assert(offsetof(LinkHeader, peers) == 16);
    // Heartbeats are 64-bit atomics on both sides, so they must sit on 8-byte boundaries.
    static_assert((offsetof(LinkHeader, peers) + offsetof(PeerInfo, heartbeat)) % 8 == 0);
    static_assert((offsetof(LinkHeader, peers) + sizeof(PeerInfo) + offsetof(PeerInfo, heartbeat)) % 8 == 0);

    // Slots. Doubles sit on 8-byte boundaries in the mapping.
    static_assert(sizeof(InputState) == 112);
    static_assert(offsetof(InputState, mouseX) == 8);
    static_assert(offsetof(InputState, mouseY) == 16);
    static_assert(offsetof(InputState, moveX) == 24);
    static_assert(offsetof(InputState, buttons) == 32);
    static_assert(offsetof(InputState, handover) == 36);
    static_assert(offsetof(InputState, heading) == 40);
    static_assert(offsetof(InputState, anchor) == 44);
    static_assert(offsetof(InputState, spawnModel) == 56);
    static_assert(offsetof(InputState, spawnCount) == 58);
    static_assert(offsetof(InputState, radioNext) == 60);
    static_assert(offsetof(InputState, radioPrev) == 62);
    static_assert(offsetof(InputState, sceneOffset) == 64);
    static_assert(offsetof(InputState, sceneHeading) == 76);
    static_assert(offsetof(InputState, shoutCount) == 80);
    static_assert(offsetof(InputState, carModel) == 82);
    static_assert(offsetof(InputState, insultCount) == 84);
    static_assert(offsetof(InputState, swearCount) == 90);
    static_assert(offsetof(InputState, carColours) == 86);
    static_assert(offsetof(InputState, carOffset) == 92);
    static_assert(offsetof(InputState, carHeading) == 104);
    static_assert(offsetof(InputState, lineCount) == 108);
    static_assert(offsetof(InputState, lineKind) == 110);
    static_assert(offsetof(InputState, spawnFlags) == 111);
    static_assert(sizeof(InputSlot) == 144);
    static_assert(sizeof(WeaponEntry) == 8);
    static_assert(sizeof(WeaponCommand) == 8 + kWeaponSlotCount * sizeof(WeaponEntry) + 24);
    static_assert(offsetof(WeaponCommand, skills) == 112);
    static_assert(sizeof(ShotEvent) == 64);
    static_assert(offsetof(ShotEvent, damage) == 12);
    static_assert(offsetof(ShotEvent, origin) == 16);
    static_assert(offsetof(ShotEvent, direction) == 28);
    static_assert(offsetof(ShotEvent, range) == 40);
    static_assert(sizeof(ProjectileEvent) == 128);
    static_assert(offsetof(ProjectileEvent, model) == 8);
    static_assert(offsetof(ProjectileEvent, origin) == 16);
    static_assert(offsetof(ProjectileEvent, velocity) == 28);
    static_assert(offsetof(ProjectileEvent, rotation) == 40);
    static_assert(offsetof(ProjectileEvent, parts) == 56);
    static_assert(offsetof(ProjectileEvent, id) == 112);
    static_assert(sizeof(EffectEvent) == 32);
    static_assert(sizeof(CheatCommand) == 8);
    static_assert(offsetof(CheatCommand, cheat) == 4);
    static_assert(offsetof(EffectEvent, projectile) == 8);
    static_assert(offsetof(EffectEvent, at) == 12);
    static_assert(offsetof(EffectEvent, seconds) == 24);
    static_assert(sizeof(PlayerState) == 248);
    static_assert(offsetof(PlayerState, offset) == 8);
    static_assert(offsetof(PlayerState, velocity) == 20);
    static_assert(offsetof(PlayerState, heading) == 32);
    static_assert(offsetof(PlayerState, viewHeading) == 36);
    static_assert(offsetof(PlayerState, viewPitch) == 40);
    static_assert(offsetof(PlayerState, handover) == 44);
    static_assert(offsetof(PlayerState, radioStation) == 48);
    static_assert(offsetof(PlayerState, vehicleModel) == 50);
    static_assert(offsetof(PlayerState, vehicleColours) == 52);
    static_assert(offsetof(PlayerState, vehicleOffset) == 56);
    static_assert(offsetof(PlayerState, vehicleHeading) == 68);
    static_assert(offsetof(PlayerState, damage) == 72);
    static_assert(offsetof(PlayerState, weaponType) == 80);
    static_assert(offsetof(PlayerState, weaponSlot) == 81);
    static_assert(offsetof(PlayerState, weaponFlags) == 82);
    static_assert(offsetof(PlayerState, shots) == 84);
    static_assert(offsetof(PlayerState, cameraMode) == 86);
    static_assert(offsetof(PlayerState, ammoStyle) == 87);
    static_assert(offsetof(PlayerState, muzzle) == 88);
    static_assert(offsetof(PlayerState, crosshair) == 112);
    static_assert(offsetof(PlayerState, loadout) == 120);
    static_assert(offsetof(PlayerState, skills) == 224);
    static_assert(offsetof(PlayerState, aimRadius) == 246);
    static_assert(sizeof(PlayerSlot) == 304);
    static_assert(sizeof(AudioState) == 32);
    static_assert(sizeof(AudioSlot) == 64);
    static_assert(sizeof(BodyBone) == 28);
    static_assert(sizeof(BodyState) == 64 + kBodyBoneMax * 28 + 36);
    static_assert(offsetof(BodyState, cameraUp) == 64 + kBodyBoneMax * 28);
    static_assert(offsetof(BodyState, health) == 64 + kBodyBoneMax * 28 + 12);
    static_assert(offsetof(BodyState, engine) == 64 + kBodyBoneMax * 28 + 16);
    static_assert(offsetof(BodyState, hiddenParts) == 64 + kBodyBoneMax * 28 + 28);
    static_assert(offsetof(BodyState, feet) == 24);
    static_assert(offsetof(BodyState, cameraFov) == 60);
    static_assert(offsetof(BodyState, bones) == 64);
    static_assert(sizeof(BodySlot) == 1920);
    static_assert(sizeof(RingHeader) == 64);
    static_assert(offsetof(RingHeader, head) == 8);
    static_assert(offsetof(RingHeader, tail) == 32);
    static_assert(sizeof(MessageHeader) == 8);
    static_assert(offsetof(MessageHeader, size) == 4);
    static_assert(sizeof(CollisionChunkHeader) == 40);
    static_assert(offsetof(CollisionChunkHeader, origin) == 16);
    static_assert(offsetof(CollisionChunkHeader, vertexCount) == 28);
    static_assert(offsetof(CollisionChunkHeader, indexCount) == 32);
    static_assert(sizeof(ModelChunkHeader) == 16);
    static_assert(sizeof(SkinModelHeader) == 48);
    static_assert(sizeof(SkinTexture) == 16);
    static_assert(sizeof(SkinMesh) == 24);
    static_assert(sizeof(SkinVertex) == 52);
    static_assert(offsetof(SkinVertex, bones) == 32);

    // Section layout follows from the struct sizes.
    static_assert(kOffsetInputSlot == kOffsetHeader + sizeof(LinkHeader));
    static_assert(kOffsetPlayerSlot == kOffsetInputSlot + sizeof(InputSlot));
    static_assert((kOffsetInputSlot + offsetof(InputSlot, value) + offsetof(InputState, mouseX)) % 8 == 0);
    static_assert(kOffsetAudioSlot == kOffsetPlayerSlot + sizeof(PlayerSlot));
    static_assert(kOffsetAudioSlot % 64 == 0);
    static_assert(kOffsetBodySlot == kOffsetAudioSlot + sizeof(AudioSlot));
    static_assert(kOffsetBodySlot % 64 == 0);
    static_assert(kOffsetVehicleSlots == kOffsetBodySlot + sizeof(BodySlot));
    static_assert(kOffsetWeaponSlots == kOffsetVehicleSlots + kVehicleMax * sizeof(BodySlot));
    static_assert(kOffsetWeaponSlots % 64 == 0);
    static_assert(kOffsetGearSlot == kOffsetWeaponSlots + kWeaponHandMax * sizeof(BodySlot));
    static_assert(kOffsetRingToSanAndreas == kOffsetGearSlot + sizeof(BodySlot));
    static_assert(kOffsetRingToSanAndreas % 64 == 0);
    static_assert(kOffsetRingToSkyrim == kOffsetRingToSanAndreas + sizeof(RingHeader) + kRingToSanAndreasCapacity);
    static_assert(kMappingSize == kOffsetRingToSkyrim + sizeof(RingHeader) + kRingToSkyrimCapacity);
    static_assert((kRingToSanAndreasCapacity & (kRingToSanAndreasCapacity - 1)) == 0);
    static_assert((kRingToSkyrimCapacity & (kRingToSkyrimCapacity - 1)) == 0);
}
