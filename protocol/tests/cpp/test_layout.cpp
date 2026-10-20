#include <cstring>
#include <ostream>  // before doctest, so it can print string_view
#include <string_view>

#include <doctest/doctest.h>

#include "../../ahshitrim_protocol.h"

using namespace AhShitRim::Protocol;

// The static_asserts in the header pin the layout at compile time (in the 64-bit tests and the
// 32-bit ASI build alike). These spell out the values a peer of any version relies on.

TEST_CASE("the magic reads ASRM in memory")
{
    char bytes[4];
    std::memcpy(bytes, &kMagic, sizeof(bytes));
    CHECK(std::string_view(bytes, 4) == "ASRM");
}

TEST_CASE("the frozen header layout")
{
    CHECK(sizeof(PeerInfo) == 32);
    CHECK(sizeof(LinkHeader) == 128);
    CHECK(offsetof(LinkHeader, magic) == 0);
    CHECK(offsetof(LinkHeader, creatorVersion) == 12);
    CHECK(offsetof(LinkHeader, peers) == 16);
    CHECK(offsetof(LinkHeader, peers) + static_cast<std::size_t>(Side::SanAndreas) * sizeof(PeerInfo) == 48);
}

TEST_CASE("protocol v5 constants")
{
    CHECK(kProtocolVersion == 5);
    CHECK(kPlayerCrouching == 0x4);
    CHECK(kMappingSize == 8390976);
    CHECK(kCollisionChunkUnits == 2048);
    CHECK(static_cast<std::uint16_t>(MessageType::CollisionChunk) == 1);
    CHECK(static_cast<std::uint16_t>(MessageType::ModelChunk) == 2);
    CHECK(kBodyBoneMax == 64);
    CHECK(kModelChunkMax == 256u * 1024);
    CHECK(kPeerTimeoutMs == 2000);
    CHECK((std::wstring_view(kMappingName) == L"Local\\AhShitRim.Link"));  // doctest cannot print wide strings
    CHECK(static_cast<std::uint32_t>(Side::Skyrim) == 0);
    CHECK(static_cast<std::uint32_t>(Side::SanAndreas) == 1);
}

TEST_CASE("the v5 slot and ring layout")
{
    CHECK(kOffsetInputSlot == 128);
    CHECK(kOffsetPlayerSlot == 208);
    CHECK(sizeof(InputState) == 64);
    CHECK(offsetof(InputState, anchor) == 44);
    CHECK(sizeof(PlayerState) == 48);
    CHECK(offsetof(InputSlot, value) == 8);
    CHECK(offsetof(PlayerSlot, value) == 8);
    CHECK(kOffsetPlayerSlot + sizeof(PlayerSlot) <= kOffsetBodySlot);
    CHECK(kOffsetBodySlot == 320);
    CHECK(sizeof(BodySlot) == 1920);
    CHECK(offsetof(BodySlot, value) == 8);
    CHECK(sizeof(BodyState) == 1856);
    CHECK(kOffsetRingToSanAndreas == 2240);
    CHECK(kRingToSanAndreasCapacity == 4u * 1024 * 1024);
    CHECK(kOffsetRingToSkyrim == 4196608);
    CHECK(kRingToSkyrimCapacity == 4u * 1024 * 1024);
    CHECK(sizeof(ModelChunkHeader) == 16);
    CHECK(sizeof(SkinVertex) == 52);
    CHECK(sizeof(CollisionChunkHeader) == 40);
}
