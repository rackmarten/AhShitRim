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

TEST_CASE("protocol v1 constants")
{
    CHECK(kProtocolVersion == 1);
    CHECK(kMappingSize == 4096);
    CHECK(kPeerTimeoutMs == 2000);
    CHECK((std::wstring_view(kMappingName) == L"Local\\AhShitRim.Link"));  // doctest cannot print wide strings
    CHECK(static_cast<std::uint32_t>(Side::Skyrim) == 0);
    CHECK(static_cast<std::uint32_t>(Side::SanAndreas) == 1);
}
