#include <doctest/doctest.h>

#include "../../cpp/handshake.h"

using namespace AhShitRim;
using namespace AhShitRim::Handshake;
using namespace AhShitRim::Protocol;

TEST_CASE("a section's header says whether it's ours")
{
    constexpr std::uint32_t kHeader = sizeof(LinkHeader);
    CHECK(CheckSection(0, 0, 0, 0) == Section::Initializing);
    CHECK(CheckSection(kMagic, kHeader, kMappingSize, kProtocolVersion) == Section::Usable);
    CHECK(CheckSection(kMagic, kHeader, kMappingSize, 8) == Section::OtherVersion);
    CHECK(CheckSection(kMagic, kHeader, kMappingSize - 4096, kProtocolVersion) == Section::OtherVersion);
    CHECK(CheckSection(0xDEADBEEF, kHeader, kMappingSize, kProtocolVersion) == Section::Foreign);
    CHECK(CheckSection(kMagic, 64, kMappingSize, kProtocolVersion) == Section::Foreign);
}

TEST_CASE("another version's section reports the other half's version, else its creator's")
{
    CHECK(OtherVersion(8, 8) == 8);
    CHECK(OtherVersion(12, 8) == 12);  // the other half replaced the creator
    CHECK(OtherVersion(0, 8) == 8);    // nobody there now
    CHECK(OtherVersion(kProtocolVersion, 8) == 8);  // the other half is ours too: the creator is the odd one
}

TEST_CASE("a mismatch is told at once, then again only after the repeat")
{
    Notice notice(60000);
    CHECK(notice.Due(8, 1000));
    CHECK_FALSE(notice.Due(8, 1050));
    CHECK_FALSE(notice.Due(8, 60999));
    CHECK(notice.Due(8, 61000));
    CHECK_FALSE(notice.Due(8, 61050));
    CHECK(notice.Due(9, 61100));  // another version: told at once
}

TEST_CASE("a mismatch that ends and starts again is told again at once")
{
    Notice notice(60000);
    CHECK(notice.Due(8, 1000));
    CHECK_FALSE(notice.Due(0, 2000));  // over
    CHECK(notice.Due(8, 3000));
}

TEST_CASE("the mismatch message names both versions and the half to update")
{
    const std::string older = std::to_string(kProtocolVersion - 1);
    const std::string ours = std::to_string(kProtocolVersion);

    // Skyrim sees an older San Andreas.
    std::string text = MismatchMessage(Side::Skyrim, kProtocolVersion - 1);
    CHECK(text.find("San Andreas has version " + older + ", Skyrim has version " + ours) != std::string::npos);
    CHECK(text.find("Install the same version in both") != std::string::npos);
    CHECK(text.find("AhShitRim.asi") != std::string::npos);

    // San Andreas sees an older Skyrim plugin.
    text = MismatchMessage(Side::SanAndreas, kProtocolVersion - 1);
    CHECK(text.find("San Andreas has version " + ours + ", Skyrim has version " + older) != std::string::npos);
    CHECK(text.find("AhShitRim.dll") != std::string::npos);
}
