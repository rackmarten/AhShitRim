#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ostream>  // before doctest, so it can print string_view
#include <string_view>

#include <doctest/doctest.h>

#include "../../cpp/img.h"
#include "../../cpp/txd.h"
#include "../../cpp/weapons.h"

using namespace AhShitRim;

namespace
{
    // An archive in memory: a directory of (name, sectors) and each entry's first byte set to its index + 1.
    std::vector<std::uint8_t> Archive(const std::vector<std::pair<std::string, std::uint16_t>>& a_entries)
    {
        std::vector<std::uint8_t> bytes;
        const auto                put = [&](const void* a_data, std::size_t a_size) {
            const auto* p = static_cast<const std::uint8_t*>(a_data);
            bytes.insert(bytes.end(), p, p + a_size);
        };
        put("VER2", 4);
        const std::uint32_t count = static_cast<std::uint32_t>(a_entries.size());
        put(&count, 4);
        std::uint32_t sector = 1 + (8 + count * 32 + Img::kSector - 1) / Img::kSector;  // data after the directory
        for (const auto& [name, sectors] : a_entries) {
            const std::uint16_t zero = 0;
            put(&sector, 4);
            put(&sectors, 2);
            put(&zero, 2);
            char field[24] = {};
            std::memcpy(field, name.data(), name.size());
            put(field, sizeof(field));
            sector += sectors;
        }
        bytes.resize(static_cast<std::size_t>(sector) * Img::kSector);
        std::uint32_t at = 1 + (8 + count * 32 + Img::kSector - 1) / Img::kSector;
        for (std::size_t i = 0; i < a_entries.size(); ++i) {
            bytes[static_cast<std::size_t>(at) * Img::kSector] = static_cast<std::uint8_t>(i + 1);
            at += a_entries[i].second;
        }
        return bytes;
    }

    Img::Reader InMemory(const std::vector<std::uint8_t>& a_bytes)
    {
        return [&a_bytes](std::uint64_t a_offset, void* a_out, std::size_t a_size) {
            if (a_offset + a_size > a_bytes.size()) {
                return false;
            }
            std::memcpy(a_out, a_bytes.data() + a_offset, a_size);
            return true;
        };
    }
}

TEST_CASE("an archive's directory and entries read back")
{
    const auto bytes = Archive({ { "colt45.txd", 4 }, { "colt45.dff", 7 }, { "Sniper.TXD", 12 } });
    const auto read = InMemory(bytes);
    const auto dir = Img::ReadDirectory(read);
    REQUIRE(dir);
    REQUIRE(dir->size() == 3);
    CHECK((*dir)[0].name == "colt45.txd");
    CHECK((*dir)[0].size == 4u * Img::kSector);

    const auto* sniper = Img::Find(*dir, "sniper.txd");  // names compare without case
    REQUIRE(sniper);
    CHECK(sniper->size == 12u * Img::kSector);
    CHECK_FALSE(Img::Find(*dir, "m4.txd"));

    const auto data = Img::ReadEntry(read, *sniper);
    REQUIRE(data.size() == 12u * Img::kSector);
    CHECK(data[0] == 3);
    CHECK(Img::ReadEntry(read, *Img::Find(*dir, "colt45.dff"))[0] == 2);
}

TEST_CASE("something that is not a version 2 archive, or is cut short, has no directory")
{
    std::vector<std::uint8_t> bad(64, 0);
    CHECK_FALSE(Img::ReadDirectory(InMemory(bad)));
    auto bytes = Archive({ { "a.txd", 1 } });
    bytes.resize(20);  // the entry is cut off
    CHECK_FALSE(Img::ReadDirectory(InMemory(bytes)));
    auto huge = Archive({ { "a.txd", 1 } });
    const std::uint32_t many = Img::kEntryCountMax + 1;
    std::memcpy(huge.data() + 4, &many, 4);
    CHECK_FALSE(Img::ReadDirectory(InMemory(huge)));
}

TEST_CASE("an entry past the end of the archive reads as nothing")
{
    auto       bytes = Archive({ { "a.txd", 2 } });
    const auto read = InMemory(bytes);
    const auto dir = Img::ReadDirectory(read);
    REQUIRE(dir);
    bytes.resize(bytes.size() - 100);
    CHECK(Img::ReadEntry(read, (*dir)[0]).empty());
}

// The player's own files, never in the repository: every weapon CJ can carry has its icon in its model's
// dictionary in gta3.img. Without AHSR_SA_PATH this checks nothing.
TEST_CASE("The San Andreas install's gta3.img has every weapon's icon")
{
    const char* sa = std::getenv("AHSR_SA_PATH");
    if (!sa || !*sa) {
        MESSAGE("AHSR_SA_PATH not set, skipped");
        return;
    }
    std::ifstream file(std::filesystem::path(sa) / "models/gta3.img", std::ios::binary);
    REQUIRE(file);
    const Img::Reader read = [&](std::uint64_t a_offset, void* a_out, std::size_t a_size) {
        file.clear();
        file.seekg(static_cast<std::streamoff>(a_offset));
        return static_cast<bool>(file.read(static_cast<char*>(a_out), static_cast<std::streamsize>(a_size)));
    };
    const auto dir = Img::ReadDirectory(read);
    REQUIRE(dir);

    int found = 0;
    for (int type = 1; type <= Weapons::kLastCarried; ++type) {
        const auto t = static_cast<std::uint8_t>(type);
        if (!Weapons::Carried(t)) {
            continue;
        }
        const auto  model = std::string(Weapons::ModelName(t));
        const auto* entry = Img::Find(*dir, model + ".txd");
        INFO("weapon ", type, " model ", model);
        REQUIRE(entry);
        const auto data = Img::ReadEntry(read, *entry);
        std::uint32_t skipped = 0;
        const auto    textures = Txd::Read(data.data(), data.size(), &skipped);
        const auto*   icon = Txd::Find(textures, Weapons::IconName(t));
        REQUIRE(icon);
        CHECK(icon->width > 0);
        CHECK(icon->rgba.size() == static_cast<std::size_t>(icon->width) * icon->height * 4);
        found++;
    }
    CHECK(found == 43);

    // And the scoped crosshairs.
    for (const char* weapon : { "sniper", "camera" }) {
        const auto* entry = Img::Find(*dir, std::string(weapon) + ".txd");
        REQUIRE(entry);
        const auto data = Img::ReadEntry(read, *entry);
        const auto textures = Txd::Read(data.data(), data.size(), nullptr);
        CHECK(Txd::Find(textures, std::string(weapon) == "sniper" ? "SNIPERcrosshair" : "cameraCrosshair"));
    }
}
