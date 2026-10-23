// CJ's state in the save (v11), the game-free half: the record the SKSE plugin writes into Skyrim's
// co-save and reads back on load. Header-only and free of game types, so the protocol tests cover it.
//
// A record is a version and a run of bytes (SKSE's serialization keeps both). Fields are only ever
// added at the end, each version's fields behind the version that brought them, and an older record
// reads with the newer fields at their defaults. The wanted level has a record of its own (protocol/cpp/wanted.h).
//
// Version history: 1 the handover and the armour; 2 the car; 3 the weapons; 4 the weapon skills; 5 the one-off
// pickups taken (v23).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../ahshitrim_protocol.h"
#include "weapons.h"

namespace AhShitRim::Cosave
{
    inline constexpr std::uint32_t kRecordType = 0x4D525341;  // "ASRM"
    inline constexpr std::uint32_t kVersion = 5;

    // The car CJ was in, relative to him; model 0 = on foot.
    struct Car
    {
        std::uint16_t model = 0;  // San Andreas's vehicle model id
        std::uint8_t  colours[4] = {};
        float         offset[3] = {};  // relative to CJ's feet, Skyrim units
        float         heading = 0.0f;  // Skyrim radians
    };

    // CJ's weapons: one entry per San Andreas weapon slot, the slot he held and (version 4) his weapon skills.
    struct Arms
    {
        std::uint8_t                slot = 0;
        AhShitRim::Protocol::WeaponEntry entries[AhShitRim::Protocol::kWeaponSlotCount] = {};
        std::uint16_t               skills[AhShitRim::Protocol::kWeaponSkillCount] = {};  // San Andreas's stats, 0..1000
    };

    struct Record
    {
        bool    handover = false;  // San Andreas had the player
        float   armor = 0.0f;      // San Andreas points, 0..100
        Car     car;
        Arms    weapons;           // version 3
        std::vector<std::uint16_t> pickupsTaken;  // version 5: the one-off pickups CJ took (Pickups::Spot ids)
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
    inline constexpr std::size_t kWeaponBytes = 1 + 2 + 4;  // type, clip, ammo
    inline constexpr std::size_t kSizeV3 = kSizeV2 + 1 + AhShitRim::Protocol::kWeaponSlotCount * kWeaponBytes;
    inline constexpr std::size_t kSizeV4 = kSizeV3 + AhShitRim::Protocol::kWeaponSkillCount * 2;
    inline constexpr std::size_t kSizeV5 = kSizeV4 + 2;  // and 2 bytes per pickup taken
    inline constexpr std::uint16_t kSkillMax = 1000;
    inline constexpr std::size_t kPickupsMax = 4096;

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
        detail::Put(out, a_record.weapons.slot);
        for (const auto& entry : a_record.weapons.entries) {
            detail::Put(out, entry.type);
            detail::Put(out, entry.clip);
            detail::Put(out, entry.ammo);
        }
        for (auto skill : a_record.weapons.skills) {
            detail::Put(out, skill);
        }
        const auto taken = static_cast<std::uint16_t>((std::min)(a_record.pickupsTaken.size(), kPickupsMax));
        detail::Put(out, taken);
        for (std::size_t i = 0; i < taken; ++i) {
            detail::Put(out, a_record.pickupsTaken[i]);
        }
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
        if (a_version < 1 || a_size < kSizeV1 || (a_version >= 2 && a_size < kSizeV2) || (a_version >= 3 && a_size < kSizeV3) ||
            (a_version >= 4 && a_size < kSizeV4) || (a_version >= 5 && a_size < kSizeV5)) {
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
        if (a_version >= 3) {
            record.weapons.slot = detail::Get<std::uint8_t>(at);
            for (auto& entry : record.weapons.entries) {
                entry.type = detail::Get<std::uint8_t>(at);
                entry.clip = detail::Get<std::uint16_t>(at);
                entry.ammo = detail::Get<std::uint32_t>(at);
            }
            if (record.weapons.slot >= AhShitRim::Protocol::kWeaponSlotCount) {
                return Result::Bad;
            }
            // A weapon this version doesn't know (a later one's) is left out rather than losing the rest of the record.
            AhShitRim::Weapons::Sanitise(record.weapons.entries);
        }
        if (a_version >= 4) {
            for (auto& skill : record.weapons.skills) {
                skill = (std::min)(detail::Get<std::uint16_t>(at), kSkillMax);
            }
        }
        if (a_version >= 5) {
            const auto taken = detail::Get<std::uint16_t>(at);
            if (taken > kPickupsMax || a_size < kSizeV5 + 2u * taken) {
                return Result::Short;
            }
            record.pickupsTaken.resize(taken);
            for (auto& id : record.pickupsTaken) {
                id = detail::Get<std::uint16_t>(at);
            }
        }
        record.armor = record.armor < 0.0f ? 0.0f : record.armor > 100.0f ? 100.0f : record.armor;
        a_out = record;
        return Result::Ok;
    }
}
