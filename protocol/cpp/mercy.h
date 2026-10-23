// NOMERCY (v20), our own cheat, the game-free half: which essential flags it cleared, so that typing it again
// puts back exactly those, and its record in Skyrim's co-save. Header-only and free of game types, so the
// protocol tests cover it; the SKSE plugin clears the flags (skse/src/player/CheatCodes.cpp).
//
// Skyrim keeps "essential" in two places: an actor base's flag (TESNPC, the plugins' data, never saved) and
// each loaded actor's own (Actor::boolFlags, which its base and the quest aliases that hold it set). While
// NOMERCY is on, every base's flag is cleared once and every loaded actor's whenever it shows up again (a
// cell loads, an alias takes someone), and each one cleared is noted here. Off, the noted ones get their
// flag back. The player is never touched: it is made essential for Wasted (skse/src/player/Vitals.cpp).
//
// The record ("ASCH", its own beside ASRM so it can grow apart from it): version 1 is on (u8), then the
// bases (u32 count, form ids) and the actors (u32 count, then each actor's form id and its base's).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace AhShitRim::Mercy
{
    inline constexpr std::uint32_t kRecordType = 0x48435341;  // "ASCH"
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::uint32_t kListMax = 1u << 20;       // far more than any load order has: a bigger count is a broken record

    inline constexpr std::uint32_t kPlayerBase = 0x00000007;  // the player's actor base (Prisoner)
    inline constexpr std::uint32_t kPlayerRef = 0x00000014;   // the player

    // An actor whose flag was cleared, with its base: a temporary reference's form id can belong to someone
    // else after a load, and only the same one gets its flag back.
    struct Actor
    {
        std::uint32_t id = 0;
        std::uint32_t base = 0;
        bool operator==(const Actor&) const = default;
    };

    // Whether NOMERCY clears a_id's flag: an essential one that isn't the player.
    constexpr bool Clears(bool a_on, bool a_essential, std::uint32_t a_id, std::uint32_t a_base)
    {
        return a_on && a_essential && a_id != kPlayerRef && a_base != kPlayerBase;
    }

    class Ledger
    {
    public:
        bool On() const { return _on; }
        void SetOn(bool a_on) { _on = a_on; }

        // A base's flag was cleared. Returns false if it was noted already.
        bool NoteBase(std::uint32_t a_base) { return Insert(_bases, a_base); }

        // An actor's own flag was cleared. Returns false if it was noted already.
        bool NoteActor(std::uint32_t a_id, std::uint32_t a_base)
        {
            const auto at = std::lower_bound(_actors.begin(), _actors.end(), a_id, [](const Actor& a_a, std::uint32_t a_v) { return a_a.id < a_v; });
            if (at != _actors.end() && at->id == a_id) {
                return false;
            }
            _actors.insert(at, Actor{ a_id, a_base });
            return true;
        }

        const std::vector<std::uint32_t>& Bases() const { return _bases; }
        const std::vector<Actor>&         Actors() const { return _actors; }
        bool                              Empty() const { return _bases.empty() && _actors.empty(); }

        // Everything to give its flag back, the ledger emptied (and off).
        struct Cleared
        {
            std::vector<std::uint32_t> bases;
            std::vector<Actor>         actors;
        };
        Cleared TakeAll()
        {
            Cleared out{ std::move(_bases), std::move(_actors) };
            _bases.clear();
            _actors.clear();
            _on = false;
            return out;
        }

        // Only the bases, the actors kept (a load: the bases are this session's memory, the actors the
        // save's to say).
        std::vector<std::uint32_t> TakeBases()
        {
            auto out = std::move(_bases);
            _bases.clear();
            return out;
        }

    private:
        static bool Insert(std::vector<std::uint32_t>& a_list, std::uint32_t a_value)
        {
            const auto at = std::lower_bound(a_list.begin(), a_list.end(), a_value);
            if (at != a_list.end() && *at == a_value) {
                return false;
            }
            a_list.insert(at, a_value);
            return true;
        }

        bool                       _on = false;
        std::vector<std::uint32_t> _bases;   // sorted
        std::vector<Actor>         _actors;  // sorted by id
    };

    namespace detail
    {
        inline void Put(std::vector<std::uint8_t>& a_out, std::uint32_t a_value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_value);
            a_out.insert(a_out.end(), bytes, bytes + sizeof(a_value));
        }

        inline bool Get(const std::uint8_t*& a_at, const std::uint8_t* a_end, std::uint32_t& a_value)
        {
            if (a_end - a_at < static_cast<std::ptrdiff_t>(sizeof(a_value))) {
                return false;
            }
            std::memcpy(&a_value, a_at, sizeof(a_value));
            a_at += sizeof(a_value);
            return true;
        }
    }

    inline std::vector<std::uint8_t> Write(const Ledger& a_ledger)
    {
        std::vector<std::uint8_t> out;
        out.push_back(a_ledger.On() ? 1 : 0);
        detail::Put(out, static_cast<std::uint32_t>(a_ledger.Bases().size()));
        for (auto base : a_ledger.Bases()) {
            detail::Put(out, base);
        }
        detail::Put(out, static_cast<std::uint32_t>(a_ledger.Actors().size()));
        for (const auto& actor : a_ledger.Actors()) {
            detail::Put(out, actor.id);
            detail::Put(out, actor.base);
        }
        return out;
    }

    // Reads a record into a_out; false (a_out empty and off) for a newer version or one that doesn't read.
    inline bool Read(std::uint32_t a_version, const std::uint8_t* a_data, std::size_t a_size, Ledger& a_out)
    {
        a_out = {};
        if (a_version < 1 || a_version > kVersion || a_size < 1) {
            return false;
        }
        const std::uint8_t* at = a_data;
        const std::uint8_t* end = a_data + a_size;
        Ledger              ledger;
        ledger.SetOn((*at++ & 1) != 0);
        std::uint32_t count = 0;
        if (!detail::Get(at, end, count) || count > kListMax) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t base = 0;
            if (!detail::Get(at, end, base)) {
                return false;
            }
            ledger.NoteBase(base);
        }
        if (!detail::Get(at, end, count) || count > kListMax) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t id = 0, base = 0;
            if (!detail::Get(at, end, id) || !detail::Get(at, end, base)) {
                return false;
            }
            ledger.NoteActor(id, base);
        }
        a_out = std::move(ledger);
        return true;
    }
}
