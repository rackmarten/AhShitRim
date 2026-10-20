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
    inline constexpr std::uint32_t kProtocolVersion = 8;

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

    // Section layout (byte offsets from the start of the mapping): the header, the latest-value
    // slots, the ring from Skyrim to San Andreas, then the ring back.
    inline constexpr std::uint32_t kOffsetHeader = 0;
    inline constexpr std::uint32_t kOffsetInputSlot = 128;
    inline constexpr std::uint32_t kOffsetPlayerSlot = 208;
    inline constexpr std::uint32_t kOffsetAudioSlot = 272;               // v7
    inline constexpr std::uint32_t kOffsetBodySlot = 320;                // v5
    inline constexpr std::uint32_t kOffsetVehicleSlots = 2240;           // v6: kVehicleMax BodySlots
    inline constexpr std::uint32_t kOffsetRingToSanAndreas = 9920;       // RingHeader, data follows
    inline constexpr std::uint32_t kRingToSanAndreasCapacity = 4194304;  // 4 MiB: collision chunks
    inline constexpr std::uint32_t kOffsetRingToSkyrim = 4204288;        // v5
    inline constexpr std::uint32_t kRingToSkyrimCapacity = 4194304;      // 4 MiB: CJ's and the vehicles' models
    inline constexpr std::uint32_t kMappingSize = 8398656;               // 4204288 + 64 + 4 MiB

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

    inline constexpr std::uint32_t kButtonSprint = 0x1;  // InputState.buttons, held
    inline constexpr std::uint32_t kButtonJump = 0x2;
    inline constexpr std::uint32_t kButtonCrouch = 0x4;  // San Andreas toggles crouch on the press
    inline constexpr std::uint32_t kButtonWalk = 0x8;
    inline constexpr std::uint32_t kButtonEnterExit = 0x10;  // v6: get in or out of a vehicle
    inline constexpr std::uint32_t kButtonHorn = 0x20;       // v6
    inline constexpr std::uint32_t kButtonLeanForward = 0x40;  // v6: up arrow; on foot it walks forwards, on a bike it leans forward
    inline constexpr std::uint32_t kButtonLeanBack = 0x80;     // v6: down arrow; on foot back, on a bike it leans back (wheelies)
    inline constexpr std::uint32_t kButtonHandbrake = 0x100;   // v8: in a vehicle, the handbrake (Space in both key layouts, also Right Ctrl in the classic one)

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
        std::uint16_t spawnModel;  // v6: San Andreas vehicle model id to spawn in front of CJ
        std::uint16_t spawnCount;  // v6: counts spawn requests; a new value spawns spawnModel
        std::uint16_t radioNext;   // v7: counts mouse wheel steps up; in a vehicle each one tunes the next station
        std::uint16_t radioPrev;   // v7: counts wheel steps down, the previous station
    };

    struct InputSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        InputState    value;
        std::uint8_t  pad[8];
    };

    inline constexpr std::uint32_t kPlayerValid = 0x1;     // PlayerState.flags: San Andreas is simulating CJ for this handover
    inline constexpr std::uint32_t kPlayerGrounded = 0x2;   // PlayerState.flags: CJ stands on something
    inline constexpr std::uint32_t kPlayerCrouching = 0x4;  // PlayerState.flags: CJ crouches (v3); Skyrim sneaks
    inline constexpr std::uint32_t kPlayerInVehicle = 0x8;  // PlayerState.flags: CJ drives or rides a vehicle (v6)
    inline constexpr std::uint32_t kPlayerRadioRetuning = 0x10;  // PlayerState.flags (v7): the radio is between stations

    inline constexpr std::int8_t kRadioNone = -1;  // PlayerState.radioStation (v7): no station plays

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
        std::int8_t   radioStation;  // v7: San Andreas's radio station playing in CJ's vehicle (eRadioID), kRadioNone if none
        std::uint8_t  reserved0;
        std::uint16_t reserved1;
        std::uint32_t reserved2;
    };

    struct PlayerSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        PlayerState   value;
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
        std::uint8_t  pad[8];
    };

    inline constexpr std::uint32_t kBodyValid = 0x1;    // BodyState.flags: the bones and camera are set
    inline constexpr std::uint32_t kBodyDriven = 0x2;   // BodyState.flags, vehicle slots (v6): CJ is in this vehicle

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
    // the camera is unused (zero).
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
    };

    struct BodySlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        BodyState     value;
        std::uint8_t  pad[56];
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
        ModelChunk = 2,      // San Andreas -> Skyrim (v5): CJ's model, and each vehicle's (v6)
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
    inline constexpr std::uint32_t kSkinModelBody = 0;     // SkinModelHeader.kind: a ped (CJ)
    inline constexpr std::uint32_t kSkinModelVehicle = 1;  // v6: a vehicle, its parts as bones

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
    static_assert(sizeof(InputState) == 64);
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
    static_assert(sizeof(InputSlot) == 80);
    static_assert(sizeof(PlayerState) == 56);
    static_assert(offsetof(PlayerState, offset) == 8);
    static_assert(offsetof(PlayerState, velocity) == 20);
    static_assert(offsetof(PlayerState, heading) == 32);
    static_assert(offsetof(PlayerState, viewHeading) == 36);
    static_assert(offsetof(PlayerState, viewPitch) == 40);
    static_assert(offsetof(PlayerState, handover) == 44);
    static_assert(offsetof(PlayerState, radioStation) == 48);
    static_assert(sizeof(PlayerSlot) == 64);
    static_assert(sizeof(AudioState) == 32);
    static_assert(sizeof(AudioSlot) == 48);
    static_assert(sizeof(BodyBone) == 28);
    static_assert(sizeof(BodyState) == 64 + kBodyBoneMax * 28);
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
    static_assert(kOffsetBodySlot == kOffsetAudioSlot + sizeof(AudioSlot));
    static_assert(kOffsetBodySlot % 64 == 0);
    static_assert(kOffsetVehicleSlots == kOffsetBodySlot + sizeof(BodySlot));
    static_assert(kOffsetRingToSanAndreas == kOffsetVehicleSlots + kVehicleMax * sizeof(BodySlot));
    static_assert(kOffsetRingToSanAndreas % 64 == 0);
    static_assert(kOffsetRingToSkyrim == kOffsetRingToSanAndreas + sizeof(RingHeader) + kRingToSanAndreasCapacity);
    static_assert(kMappingSize == kOffsetRingToSkyrim + sizeof(RingHeader) + kRingToSkyrimCapacity);
    static_assert((kRingToSanAndreasCapacity & (kRingToSanAndreasCapacity - 1)) == 0);
    static_assert((kRingToSkyrimCapacity & (kRingToSkyrimCapacity - 1)) == 0);
}
