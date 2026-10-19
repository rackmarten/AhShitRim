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
    inline constexpr std::uint32_t kProtocolVersion = 3;

    // Shared memory section, per Windows session. Whichever game starts first creates it.
    inline constexpr const wchar_t* kMappingName = L"Local\\AhShitRim.Link";

    // A peer counts as gone when its heartbeat hasn't moved for this long.
    inline constexpr std::uint32_t kPeerTimeoutMs = 2000;

    // Section layout (byte offsets from the start of the mapping): the header, then the latest-value
    // slots. Rings follow in later versions.
    inline constexpr std::uint32_t kOffsetHeader = 0;
    inline constexpr std::uint32_t kOffsetInputSlot = 128;
    inline constexpr std::uint32_t kOffsetPlayerSlot = 192;
    inline constexpr std::uint32_t kMappingSize = 4096;

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

    // Skyrim -> San Andreas: what the player does with CJ, as actions rather than keys.
    struct InputState
    {
        std::uint32_t frame;       // Skyrim frame counter
        std::uint32_t flags;       // kInput*
        double        mouseX;      // accumulated raw mouse counts since Skyrim started; San Andreas applies the delta
        double        mouseY;      // likewise, positive down (DirectInput's direction)
        float         moveX;       // -1..1, positive to the right
        float         moveY;       // -1..1, positive forwards
        std::uint32_t buttons;     // kButton*
        std::uint32_t handover;    // counts handovers; a new value puts CJ back at the test plane's spawn
        float         heading;     // the Skyrim player's heading at that handover, Skyrim radians; CJ spawns facing it
        std::uint32_t reserved;
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
    };

    struct PlayerSlot
    {
        std::uint32_t seq;
        std::uint32_t reserved;
        PlayerState   value;
        std::uint8_t  pad[8];
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
    static_assert(sizeof(InputState) == 48);
    static_assert(offsetof(InputState, mouseX) == 8);
    static_assert(offsetof(InputState, mouseY) == 16);
    static_assert(offsetof(InputState, moveX) == 24);
    static_assert(offsetof(InputState, buttons) == 32);
    static_assert(offsetof(InputState, handover) == 36);
    static_assert(offsetof(InputState, heading) == 40);
    static_assert(sizeof(InputSlot) == 64);
    static_assert(sizeof(PlayerState) == 48);
    static_assert(offsetof(PlayerState, offset) == 8);
    static_assert(offsetof(PlayerState, velocity) == 20);
    static_assert(offsetof(PlayerState, heading) == 32);
    static_assert(offsetof(PlayerState, viewHeading) == 36);
    static_assert(offsetof(PlayerState, viewPitch) == 40);
    static_assert(offsetof(PlayerState, handover) == 44);
    static_assert(sizeof(PlayerSlot) == 64);

    // Section layout follows from the struct sizes.
    static_assert(kOffsetInputSlot == kOffsetHeader + sizeof(LinkHeader));
    static_assert(kOffsetPlayerSlot == kOffsetInputSlot + sizeof(InputSlot));
    static_assert((kOffsetInputSlot + offsetof(InputSlot, value) + offsetof(InputState, mouseX)) % 8 == 0);
    static_assert(kMappingSize >= kOffsetPlayerSlot + sizeof(PlayerSlot));
}
