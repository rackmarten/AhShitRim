// CJ's state in the save (v11), the game-free half: the record the SKSE plugin writes into Skyrim's
// co-save and reads back on load. Header-only and free of game types, so the protocol tests cover it.
//
// A record is a version and a run of bytes (SKSE's serialization keeps both). Fields are only ever
// added at the end, each version's fields behind the version that brought them, and an older record
// reads with the newer fields at their defaults. Later increments add weapons, skills and the wanted
// level the same way.
//
// Version history: 1 the handover and the armour; 2 the car.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AhShitRim::Cosave
{
    inline constexpr std::uint32_t kRecordType = 0x4D525341;  // "ASRM"
    inline constexpr std::uint32_t kVersion = 2;

    // The car CJ was in, relative to him; model 0 = on foot.
    struct Car
    {
        std::uint16_t model = 0;  // San Andreas's vehicle model id
        std::uint8_t  colours[4] = {};
        float         offset[3] = {};  // relative to CJ's feet, Skyrim units
        float         heading = 0.0f;  // Skyrim radians
    };

    struct Record
    {
        bool  handover = false;  // San Andreas had the player
        float armor = 0.0f;      // San Andreas points, 0..100
        Car   car;
    };

    enum class Result
    {
        Ok,
        TooNew,  // written by a later version of AhShitRim: left at its defaults
        Short,   // fewer bytes than its version holds
        Bad,     // numbers that aren't
    };

    inline constexpr std::size_t kSizeV1 = 5;
    inline constexpr std::size_t kSizeV2 = kSizeV1 + 2 + 4 + 12 + 4;

    namespace detail
    {
        template <class T>
        void Put(std::vector<std::uint8_t>& a_out, const T& a_value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_value);
            a_out.insert(a_out.end(), bytes, bytes + sizeof(T));
        }

        template <class T>
        T Get(const std::uint8_t*& a_at)
        {
            T value;
            std::memcpy(&value, a_at, sizeof(T));
            a_at += sizeof(T);
            return value;
        }
    }

    // The record in the current version.
    inline std::vector<std::uint8_t> Write(const Record& a_record)
    {
        std::vector<std::uint8_t> out;
        detail::Put<std::uint8_t>(out, a_record.handover ? 1 : 0);
        detail::Put(out, a_record.armor);
        detail::Put(out, a_record.car.model);
        for (auto colour : a_record.car.colours) {
            detail::Put(out, colour);
        }
        for (auto offset : a_record.car.offset) {
            detail::Put(out, offset);
        }
        detail::Put(out, a_record.car.heading);
        return out;
    }

    // Reads a record of any version up to the current one into a_out, which keeps its defaults for what
    // the version doesn't hold.
    inline Result Read(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size, Record& a_out)
    {
        a_out = {};
        if (a_version > kVersion) {
            return Result::TooNew;
        }
        if (a_version < 1 || a_size < kSizeV1 || (a_version >= 2 && a_size < kSizeV2)) {
            return Result::Short;
        }
        const std::uint8_t* at = a_data;
        Record              record;
        record.handover = (detail::Get<std::uint8_t>(at) & 1) != 0;
        record.armor = detail::Get<float>(at);
        if (!std::isfinite(record.armor)) {
            return Result::Bad;
        }
        if (a_version >= 2) {
            record.car.model = detail::Get<std::uint16_t>(at);
            for (auto& colour : record.car.colours) {
                colour = detail::Get<std::uint8_t>(at);
            }
            for (auto& offset : record.car.offset) {
                offset = detail::Get<float>(at);
            }
            record.car.heading = detail::Get<float>(at);
            for (float v : { record.car.offset[0], record.car.offset[1], record.car.offset[2], record.car.heading }) {
                if (!std::isfinite(v)) {
                    return Result::Bad;
                }
            }
        }
        record.armor = record.armor < 0.0f ? 0.0f : record.armor > 100.0f ? 100.0f : record.armor;
        a_out = record;
        return Result::Ok;
    }
}
