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
    inline constexpr std::uint32_t kProtocolVersion = 1;

    // Shared memory section, per Windows session. Whichever game starts first creates it.
    inline constexpr const wchar_t* kMappingName = L"Local\\AhShitRim.Link";

    // A peer counts as gone when its heartbeat hasn't moved for this long.
    inline constexpr std::uint32_t kPeerTimeoutMs = 2000;

    // Section layout (byte offsets from the start of the mapping). v1 is the header alone; slots
    // and rings follow it in later versions.
    inline constexpr std::uint32_t kOffsetHeader = 0;
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

    // Section layout follows from the struct sizes.
    static_assert(kMappingSize >= kOffsetHeader + sizeof(LinkHeader));
}
