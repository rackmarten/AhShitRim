// San Andreas's IMG archives (version 2: gta3.img and the like), read in part: the directory, then the
// entries asked for. Skyrim reads the weapons' HUD icons and scope textures out of models/gta3.img
// (each weapon has a <name>.txd there) without loading the gigabyte. Header-only and game-free, so the
// protocol tests cover it.
//
// Layout: "VER2", a uint32 entry count, then per entry 32 bytes: offset and size in 2048-byte sectors
// (uint32 and uint16 streaming size, uint16 archive size) and a 24-byte name.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::Img
{
    inline constexpr std::uint32_t kSector = 2048;
    inline constexpr std::uint32_t kEntryCountMax = 1u << 20;

    struct Entry
    {
        std::string   name;    // as stored, e.g. "colt45.txd"
        std::uint64_t offset;  // bytes from the start of the archive
        std::uint64_t size;    // bytes (whole sectors)
    };

    // Reads a_size bytes at a_offset into a_out; false when the archive is shorter.
    using Reader = std::function<bool(std::uint64_t a_offset, void* a_out, std::size_t a_size)>;

    inline bool EqualsNoCase(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                   return lower(x) == lower(y);
               });
    }

    // The archive's directory; nothing when it isn't a version 2 archive or is cut short.
    inline std::optional<std::vector<Entry>> ReadDirectory(const Reader& a_read)
    {
        std::uint8_t header[8];
        if (!a_read(0, header, sizeof(header)) || std::memcmp(header, "VER2", 4) != 0) {
            return std::nullopt;
        }
        std::uint32_t count = 0;
        std::memcpy(&count, header + 4, sizeof(count));
        if (count > kEntryCountMax) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(count) * 32);
        if (!raw.empty() && !a_read(8, raw.data(), raw.size())) {
            return std::nullopt;
        }
        std::vector<Entry> entries;
        entries.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint8_t* e = raw.data() + static_cast<std::size_t>(i) * 32;
            std::uint32_t offset = 0;
            std::uint16_t streaming = 0, archive = 0;
            std::memcpy(&offset, e, 4);
            std::memcpy(&streaming, e + 4, 2);
            std::memcpy(&archive, e + 6, 2);
            const char* name = reinterpret_cast<const char*>(e + 8);
            entries.push_back({ std::string(name, strnlen(name, 24)), static_cast<std::uint64_t>(offset) * kSector,
                static_cast<std::uint64_t>(streaming ? streaming : archive) * kSector });
        }
        return entries;
    }

    inline const Entry* Find(const std::vector<Entry>& a_entries, std::string_view a_name)
    {
        const auto found = std::find_if(a_entries.begin(), a_entries.end(), [&](const Entry& a_entry) { return EqualsNoCase(a_entry.name, a_name); });
        return found == a_entries.end() ? nullptr : &*found;
    }

    // The entry's bytes; empty when the archive is cut short.
    inline std::vector<std::uint8_t> ReadEntry(const Reader& a_read, const Entry& a_entry)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(a_entry.size));
        if (!bytes.empty() && !a_read(a_entry.offset, bytes.data(), bytes.size())) {
            bytes.clear();
        }
        return bytes;
    }
}
