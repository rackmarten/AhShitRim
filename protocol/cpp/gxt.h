// San Andreas's vehicle names as its HUD shows them, from the player's own install: data/vehicles.ide gives
// each vehicle model its game name (a text key, "LANDSTK"), text/american.gxt (or another language's) the
// text for it ("Landstalker"). Header-only and game-free, so the protocol tests cover it.
//
// A San Andreas GXT file: a uint16 version (4) and a uint16 bits per character (8), then "TABL" and a uint32
// size of 12-byte entries (an 8-byte table name, a uint32 file offset). The first table (MAIN) starts with
// "TKEY" at its offset, every other one with its 8-byte name first. "TKEY" and a uint32 size of 8-byte
// entries (a uint32 offset into the table's text, a uint32 hash of the key), then "TDAT" and a uint32 size
// of nul-terminated 8-bit text. A key's hash is CKeyGen::GetUppercaseKey: CRC-32's table over the key in
// upper case, from 0xFFFFFFFF and without the final inversion.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace AhShitRim::Gxt
{
    inline constexpr std::array<std::uint32_t, 256> kCrcTable = []() {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; i++) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return table;
    }();

    constexpr std::uint32_t KeyHash(std::string_view a_key)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (const char raw : a_key) {
            const auto c = static_cast<unsigned char>(raw >= 'a' && raw <= 'z' ? raw - 'a' + 'A' : raw);
            crc = (crc >> 8) ^ kCrcTable[(crc ^ c) & 0xFF];
        }
        return crc;
    }

    // The MAIN table's texts by key hash; empty for a file that isn't a San Andreas GXT.
    inline std::unordered_map<std::uint32_t, std::string> ReadMain(const std::uint8_t* a_data, std::size_t a_size)
    {
        std::unordered_map<std::uint32_t, std::string> texts;
        const auto u32 = [&](std::size_t a_at) {
            std::uint32_t v = 0;
            std::memcpy(&v, a_data + a_at, 4);
            return v;
        };
        const auto tag = [&](std::size_t a_at, const char* a_tag) { return a_at + 8 <= a_size && std::memcmp(a_data + a_at, a_tag, 4) == 0; };
        if (!a_data || !tag(4, "TABL")) {
            return texts;
        }
        const std::uint32_t tables = u32(8);
        if (tables < 12 || 12 + static_cast<std::size_t>(tables) > a_size || std::memcmp(a_data + 12, "MAIN", 4) != 0) {
            return texts;
        }
        const std::size_t keys = u32(12 + 8);  // MAIN, the first table, has no name before its keys
        if (!tag(keys, "TKEY")) {
            return texts;
        }
        const std::size_t keyCount = u32(keys + 4) / 8;
        const std::size_t keysAt = keys + 8;
        const std::size_t dat = keysAt + keyCount * 8;
        if (!tag(dat, "TDAT")) {
            return texts;
        }
        const std::size_t textAt = dat + 8;
        const std::size_t textEnd = (std::min)(a_size, textAt + static_cast<std::size_t>(u32(dat + 4)));
        texts.reserve(keyCount);
        for (std::size_t i = 0; i < keyCount; i++) {
            const std::size_t at = textAt + u32(keysAt + i * 8);
            if (at >= textEnd) {
                continue;
            }
            std::size_t end = at;
            while (end < textEnd && a_data[end] != 0) {
                end++;
            }
            texts.emplace(u32(keysAt + i * 8 + 4), std::string(reinterpret_cast<const char*>(a_data + at), end - at));
        }
        return texts;
    }

    // vehicles.ide's "cars" section: model id to game name (the sixth column).
    inline std::unordered_map<std::uint16_t, std::string> ReadVehicleGameNames(std::string_view a_ide)
    {
        std::unordered_map<std::uint16_t, std::string> names;
        const auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
                s.remove_suffix(1);
            }
            return s;
        };
        bool cars = false;
        while (!a_ide.empty()) {
            const auto        eol = a_ide.find('\n');
            std::string_view line = a_ide.substr(0, eol);
            a_ide.remove_prefix(eol == std::string_view::npos ? a_ide.size() : eol + 1);
            if (const auto hash = line.find('#'); hash != std::string_view::npos) {
                line = line.substr(0, hash);
            }
            line = trim(line);
            if (line.empty()) {
                continue;
            }
            if (line == "cars") {
                cars = true;
                continue;
            }
            if (line == "end") {
                cars = false;
                continue;
            }
            if (!cars) {
                continue;
            }
            std::string_view columns[6];
            std::size_t      count = 0;
            for (std::string_view rest = line; count < 6;) {
                const auto comma = rest.find(',');
                columns[count++] = trim(rest.substr(0, comma));
                if (comma == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(comma + 1);
            }
            if (count < 6 || columns[0].empty() || columns[5].empty()) {
                continue;
            }
            unsigned id = 0;
            bool     number = true;
            for (const char c : columns[0]) {
                number = number && c >= '0' && c <= '9';
                id = id * 10 + static_cast<unsigned>(c - '0');
            }
            if (number && id > 0 && id < 65536) {
                names[static_cast<std::uint16_t>(id)] = std::string(columns[5]);
            }
        }
        return names;
    }

    // What San Andreas's HUD calls each vehicle model, for the models whose game name the GXT has.
    inline std::unordered_map<std::uint16_t, std::string> VehicleNames(std::string_view a_ide, const std::uint8_t* a_gxt, std::size_t a_gxtSize)
    {
        std::unordered_map<std::uint16_t, std::string> names;
        const auto texts = ReadMain(a_gxt, a_gxtSize);
        for (const auto& [model, key] : ReadVehicleGameNames(a_ide)) {
            if (const auto it = texts.find(KeyHash(key)); it != texts.end() && !it->second.empty()) {
                names.emplace(model, it->second);
            }
        }
        return names;
    }
}
