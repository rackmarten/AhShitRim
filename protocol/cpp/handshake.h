// The link's handshake and retry rules, apart from Windows' shared memory so they're tested everywhere: what a
// section's header says (ours to use, still being set up, another version's, or not ours at all), when to say a
// version mismatch again, and what to tell the player about it. link.cpp acts on them.
//
// A mismatch never switches the link off for good: a side keeps ticking, lets go of a section it can't use (it
// dies with its last holder, so the next try creates one of ours), and handshakes again once the other half
// restarts with the same version.

#pragma once

#include <cstdint>
#include <format>
#include <string>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Handshake
{
    inline constexpr std::uint64_t kRemapMs = 1000;           // a section we can't use is let go and opened again this often
    inline constexpr std::uint64_t kInitTimeoutMs = 2000;     // magic still 0 this long: its creator quit halfway through
    inline constexpr std::uint64_t kLogRepeatMs = 60000;      // a lasting mismatch is logged again this often
    inline constexpr std::uint64_t kMessageRepeatMs = 300000; // and shown to the player in Skyrim this often

    enum class Section
    {
        Initializing,  // magic 0: the creator is still filling in the header
        Usable,        // ours: same version and size
        OtherVersion,  // an AhShitRim link of another version (or size)
        Foreign,       // not an AhShitRim link
    };

    inline Section CheckSection(std::uint32_t a_magic, std::uint32_t a_headerSize, std::uint32_t a_mappingSize, std::uint32_t a_creatorVersion)
    {
        if (a_magic == 0) {
            return Section::Initializing;
        }
        if (a_magic != Protocol::kMagic || a_headerSize != sizeof(Protocol::LinkHeader)) {
            return Section::Foreign;
        }
        if (a_creatorVersion != Protocol::kProtocolVersion || a_mappingSize != Protocol::kMappingSize) {
            return Section::OtherVersion;
        }
        return Section::Usable;
    }

    // The version to report for a section of another version: the other half's, when it has published one,
    // else its creator's.
    inline std::uint32_t OtherVersion(std::uint32_t a_peerVersion, std::uint32_t a_creatorVersion)
    {
        return a_peerVersion != 0 && a_peerVersion != Protocol::kProtocolVersion ? a_peerVersion : a_creatorVersion;
    }

    // Says when to tell about a mismatch: at once when it starts or the other version changes, then again every
    // a_repeatMs while it lasts. Due(0, ...) says the mismatch is over and arms it for the next one.
    class Notice
    {
    public:
        explicit Notice(std::uint64_t a_repeatMs) :
            _repeatMs(a_repeatMs)
        {}

        bool Due(std::uint32_t a_otherVersion, std::uint64_t a_nowMs)
        {
            if (a_otherVersion == 0) {
                _version = 0;
                return false;
            }
            if (a_otherVersion != _version || a_nowMs - _lastMs >= _repeatMs) {
                _version = a_otherVersion;
                _lastMs = a_nowMs;
                return true;
            }
            return false;
        }

    private:
        std::uint64_t _repeatMs;
        std::uint32_t _version = 0;
        std::uint64_t _lastMs = 0;
    };

    // The player-facing line, the same in Skyrim's message box and San Andreas's log. Versions are the link's
    // protocol versions; the older half is the one to update.
    inline std::string MismatchMessage(Protocol::Side a_self, std::uint32_t a_otherVersion)
    {
        const bool          skyrim = a_self == Protocol::Side::Skyrim;
        const std::uint32_t sa = skyrim ? a_otherVersion : Protocol::kProtocolVersion;
        const std::uint32_t sk = skyrim ? Protocol::kProtocolVersion : a_otherVersion;
        const char*         fix = sa < sk ?
                                      "Update AhShitRim.asi in San Andreas's scripts folder and restart San Andreas" :
                                      "Update AhShitRim.dll in Skyrim's Data\\SKSE\\Plugins (or its Mod Organizer mod) and restart Skyrim";
        return std::format("AhShitRim: San Andreas has version {}, Skyrim has version {}. Install the same version in both. {}; "
                           "the link comes up by itself once both match.",
            sa, sk, fix);
    }
}
