// Coming back (v22), the game-free half: the unique people CJ kills (Ammu-Nation's blacksmiths, the main
// quest's people, every named NPC) get up again an in-game hour later, at their editor spot, once neither
// their body nor that spot is anywhere CJ could see. NOMERCY or not: it lets him kill the essential ones,
// this brings them back. Only CJ's kills: the player killed them while San Andreas had the player. Skyrim's
// own player killing someone, or a dragon, stays as Skyrim has it. Header-only and free of game types, so
// the protocol tests cover it; the SKSE plugin listens for deaths and brings them back
// (skse/src/player/Respawns.cpp).
//
// The dead waiting are in Skyrim's co-save, a record of their own ("ASRV"): version 1 is a u32 count, then
// each one's form id, its base's and when it died (f32, Skyrim's game time in days).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AhShitRim::Revive
{
    inline constexpr std::uint32_t kRecordType = 0x56525341;  // "ASRV"
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kListMax = 1u << 16;       // more dead than any game has: a bigger count is a broken record

    inline constexpr std::uint32_t kPlayerRef = 0x00000014;   // the player
    inline constexpr std::uint32_t kTemporary = 0xFF000000;   // form ids from here on are made at runtime: no editor spot to go back to

    inline constexpr float kDelayDays = 1.0f / 24.0f;  // an in-game hour (3 real minutes at Skyrim's timescale)
    inline constexpr float kHomeClear = 12288.0f;      // Skyrim units: an outdoor spot nearer the player than 3 cells may be loaded, in view

    // Whether a death brings a_id back later: a unique NPC placed in the editor (not the player, not one made
    // at runtime) that the player killed while San Andreas had it.
    constexpr bool Counts(bool a_cjHasPlayer, bool a_byPlayer, bool a_unique, std::uint32_t a_id)
    {
        return a_cjHasPlayer && a_byPlayer && a_unique && a_id != kPlayerRef && a_id < kTemporary;
    }

    // Whether the editor spot may be in view: an interior while its cell is loaded, an outdoor one while the
    // player is in the same worldspace and near.
    constexpr bool HomeInView(bool a_interior, bool a_cellLoaded, bool a_playerInWorld, float a_distance)
    {
        return a_interior ? a_cellLoaded : a_playerInWorld && a_distance < kHomeClear;
    }

    // Whether someone who died at a_diedAt gets up at a_now: an hour gone, the body and the spot out of
    // sight. A death later than now (a clock that went back) can't be waited out, so it counts as gone.
    constexpr bool Ready(float a_diedAt, float a_now, bool a_bodyLoaded, bool a_homeInView)
    {
        const bool hourGone = a_now < a_diedAt || a_now - a_diedAt >= kDelayDays;
        return hourGone && !a_bodyLoaded && !a_homeInView;
    }

    // One dead and waiting, with its base: the same id with another base after a load is someone else.
    struct Death
    {
        std::uint32_t id = 0;
        std::uint32_t base = 0;
        float         at = 0.0f;  // game days
        bool operator==(const Death&) const = default;
    };

    class Roster
    {
    public:
        // a_id died at a_at. Dying again (someone else brought them back first) starts the hour again.
        void Note(std::uint32_t a_id, std::uint32_t a_base, float a_at)
        {
            const auto at = Find(a_id);
            if (at != _dead.end() && at->id == a_id) {
                *at = Death{ a_id, a_base, a_at };
                return;
            }
            _dead.insert(at, Death{ a_id, a_base, a_at });
        }

        // a_id is back (or gone): no longer waiting. Returns false if it wasn't.
        bool Remove(std::uint32_t a_id)
        {
            const auto at = Find(a_id);
            if (at == _dead.end() || at->id != a_id) {
                return false;
            }
            _dead.erase(at);
            return true;
        }

        const std::vector<Death>& Dead() const { return _dead; }
        bool                      Empty() const { return _dead.empty(); }
        void                      Clear() { _dead.clear(); }

    private:
        std::vector<Death>::iterator Find(std::uint32_t a_id)
        {
            return std::lower_bound(_dead.begin(), _dead.end(), a_id, [](const Death& a_d, std::uint32_t a_v) { return a_d.id < a_v; });
        }

        std::vector<Death> _dead;  // sorted by id
    };

    namespace detail
    {
        template <class T>
        void Put(std::vector<std::uint8_t>& a_out, T a_value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_value);
            a_out.insert(a_out.end(), bytes, bytes + sizeof(a_value));
        }

        template <class T>
        bool Get(const std::uint8_t*& a_at, const std::uint8_t* a_end, T& a_value)
        {
            if (a_end - a_at < static_cast<std::ptrdiff_t>(sizeof(a_value))) {
                return false;
            }
            std::memcpy(&a_value, a_at, sizeof(a_value));
            a_at += sizeof(a_value);
            return true;
        }
    }

    inline std::vector<std::uint8_t> Write(const Roster& a_roster)
    {
        std::vector<std::uint8_t> out;
        detail::Put(out, static_cast<std::uint32_t>(a_roster.Dead().size()));
        for (const auto& death : a_roster.Dead()) {
            detail::Put(out, death.id);
            detail::Put(out, death.base);
            detail::Put(out, death.at);
        }
        return out;
    }

    // Reads a record into a_out; false (a_out empty) for a newer version or one that doesn't read.
    inline bool Read(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size, Roster& a_out)
    {
        a_out = {};
        if (a_version < 1 || a_version > kVersion) {
            return false;
        }
        const std::uint8_t* at = a_data;
        const std::uint8_t* end = a_data + a_size;
        std::uint32_t       count = 0;
        if (!detail::Get(at, end, count) || count > kListMax) {
            return false;
        }
        Roster roster;
        for (std::uint32_t i = 0; i < count; ++i) {
            Death death;
            if (!detail::Get(at, end, death.id) || !detail::Get(at, end, death.base) || !detail::Get(at, end, death.at) || death.at != death.at) {
                return false;
            }
            roster.Note(death.id, death.base, death.at);
        }
        a_out = std::move(roster);
        return true;
    }
}
