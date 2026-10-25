// CJ's Thu'um (v25), the game-free half: which of Skyrim's shouts the player let out (the shout table,
// by the master file and form ID it has there), how InputState.lineKind carries it with the words
// charged, and which of CJ's lines answers it: the picks for that shout and word count (voice_lines.h's
// shout picks), else the shout's picks for the nearest word count, else the generic pool of his angry
// lines, so no shout goes unanswered. Each pool plays like a deck, never the same line twice in a row.
// Header-only and free of game types, so the protocol tests cover it; Skyrim's half is
// skse/src/player/Shouting.cpp, San Andreas's sa/src/scenes.cpp.
//
// The picker (scripts/voice-picker) gets its shout columns from kShouts: keep each entry on one line.

#pragma once

#include "voice_lines.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace AhShitRim::Thuum
{
    struct Shout
    {
        std::string_view file;    // the master it comes from
        std::uint32_t    formId;  // its form ID in that file, without the load order byte
        std::string_view key;     // the picker's and voice_lines.h's name for it
        std::string_view name;
    };

    // Every shout a player can learn in Skyrim and its two story DLCs, roughly in the order they come.
    // Appending is fine; the order is the wire's (Encode), which both plugins build from this header.
    // clang-format off
    inline constexpr std::array kShouts{
        Shout{ "Skyrim.esm", 0x013E07, "unrelenting-force", "Unrelenting Force" },
        Shout{ "Skyrim.esm", 0x02F7BA, "whirlwind-sprint", "Whirlwind Sprint" },
        Shout{ "Skyrim.esm", 0x032920, "become-ethereal", "Become Ethereal" },
        Shout{ "Skyrim.esm", 0x03F9EA, "fire-breath", "Fire Breath" },
        Shout{ "Skyrim.esm", 0x05D16B, "frost-breath", "Frost Breath" },
        Shout{ "Skyrim.esm", 0x070981, "disarm", "Disarm" },
        Shout{ "Skyrim.esm", 0x07097F, "throw-voice", "Throw Voice" },
        Shout{ "Skyrim.esm", 0x048AC9, "slow-time", "Slow Time" },
        Shout{ "Skyrim.esm", 0x07097C, "marked-for-death", "Marked for Death" },
        Shout{ "Skyrim.esm", 0x044250, "dragonrend", "Dragonrend" },
        Shout{ "Skyrim.esm", 0x03CD34, "clear-skies", "Clear Skies" },
        Shout{ "Skyrim.esm", 0x046B8C, "call-dragon", "Call Dragon" },
        Shout{ "Skyrim.esm", 0x07097D, "storm-call", "Storm Call" },
        Shout{ "Skyrim.esm", 0x05FC77, "animal-allegiance", "Animal Allegiance" },
        Shout{ "Skyrim.esm", 0x07097B, "aura-whisper", "Aura Whisper" },
        Shout{ "Skyrim.esm", 0x05196A, "call-of-valor", "Call of Valor" },
        Shout{ "Skyrim.esm", 0x02395A, "dismay", "Dismay" },
        Shout{ "Skyrim.esm", 0x032921, "elemental-fury", "Elemental Fury" },
        Shout{ "Skyrim.esm", 0x070980, "ice-form", "Ice Form" },
        Shout{ "Skyrim.esm", 0x07097E, "kynes-peace", "Kyne's Peace" },
        Shout{ "Dawnguard.esm", 0x008A62, "drain-vitality", "Drain Vitality" },
        Shout{ "Dawnguard.esm", 0x007CB6, "soul-tear", "Soul Tear" },
        Shout{ "Dawnguard.esm", 0x0030D2, "summon-durnehviir", "Summon Durnehviir" },
        Shout{ "Dragonborn.esm", 0x0179D8, "bend-will", "Bend Will" },
        Shout{ "Dragonborn.esm", 0x01DF92, "dragon-aspect", "Dragon Aspect" },
        Shout{ "Dragonborn.esm", 0x0200C0, "cyclone", "Cyclone" },
        Shout{ "Dragonborn.esm", 0x02AD09, "battle-fury", "Battle Fury" },
    };
    // clang-format on

    inline constexpr int kMaxWords = 3;

    // The Dragonborn's Thu'um voice: Skyrim.esm's VoicePowers quest says the shout's words in the
    // shouter's voice through these topics (Shout01a, Shout01b, Shout02, Shout03), whose lines are
    // picked by the shout and the voice type. Dragons, draugr and the Greybeards use them too.
    inline constexpr std::array<std::uint32_t, 4> kVoiceTopics{ 0x013DFE, 0x013DFF, 0x013E00, 0x013E01 };

    namespace detail
    {
        constexpr bool SameFile(std::string_view a_a, std::string_view a_b)
        {
            if (a_a.size() != a_b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a_a.size(); ++i) {
                const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                if (lower(a_a[i]) != lower(a_b[i])) {
                    return false;
                }
            }
            return true;
        }
    }

    // A shout's index in kShouts by its file (any case) and form ID there, or by its key; -1 when it
    // isn't one (a mod's shout, a racial power).
    constexpr int Find(std::string_view a_file, std::uint32_t a_formId)
    {
        for (std::size_t i = 0; i < kShouts.size(); ++i) {
            if (kShouts[i].formId == (a_formId & 0xFFFFFF) && detail::SameFile(kShouts[i].file, a_file)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    constexpr int Find(std::string_view a_key)
    {
        for (std::size_t i = 0; i < kShouts.size(); ++i) {
            if (kShouts[i].key == a_key) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    constexpr int ClampWords(int a_words) { return std::clamp(a_words, 1, kMaxWords); }

    // A shout as one of the lines Skyrim asks CJ to say (InputState.lineCount and lineKind): kLineShout
    // plus the shout's index in kShouts plus one (0 for a shout not in it) times four, plus the words
    // charged (1 to 3). Lower kinds are the conversation's categories and the wanted level's lines.
    inline constexpr std::uint8_t kLineShout = 0x80;
    constexpr bool                IsShout(std::uint8_t a_kind) { return a_kind >= kLineShout; }

    constexpr std::uint8_t Encode(int a_shout, int a_words)
    {
        const int slot = a_shout >= 0 && a_shout < static_cast<int>(kShouts.size()) ? a_shout + 1 : 0;
        return static_cast<std::uint8_t>(kLineShout + slot * 4 + ClampWords(a_words));
    }
    constexpr int ShoutOf(std::uint8_t a_kind)
    {
        const int slot = IsShout(a_kind) ? (a_kind - kLineShout) / 4 : 0;
        return slot >= 1 && slot <= static_cast<int>(kShouts.size()) ? slot - 1 : -1;
    }
    constexpr int WordsOf(std::uint8_t a_kind) { return IsShout(a_kind) ? ClampWords((a_kind - kLineShout) % 4) : 1; }
    static_assert(kLineShout + kShouts.size() * 4 + kMaxWords <= 0xFF, "lineKind is one byte");

    // CJ's lines for each shout and word count, dealt like decks.
    class Voice
    {
    public:
        Voice(std::span<const VoiceLines::ShoutPick> a_picks, std::span<const VoiceLines::Line> a_generic, std::uint32_t a_seed = 1)
        {
            for (const auto& pick : a_picks) {
                const int shout = Find(pick.shout);
                if (shout >= 0 && pick.words >= 1 && pick.words <= kMaxWords && VoiceLines::Valid(pick.line)) {
                    _lines[shout][pick.words - 1].push_back(pick.line);
                }
            }
            for (std::size_t s = 0; s < kShouts.size(); ++s) {
                for (int w = 0; w < kMaxWords; ++w) {
                    _decks[s][w] = VoiceLines::Deck(_lines[s][w], a_seed * static_cast<std::uint32_t>(3 * s + w + 7));
                }
            }
            _generic = VoiceLines::Deck(a_generic, a_seed * 5u + 1u);
        }
        Voice(const Voice&) = delete;  // the decks point into _lines
        Voice& operator=(const Voice&) = delete;

        // Whose lines answer a_shout (an index in kShouts, -1 for none) let out with a_words: the word
        // count whose picks play (1 to 3), the nearest one with picks when that one has none (fewer
        // words first on a tie), or 0 for the generic pool.
        int PoolWords(int a_shout, int a_words) const
        {
            if (a_shout < 0 || a_shout >= static_cast<int>(kShouts.size())) {
                return 0;
            }
            constexpr int kNearest[kMaxWords][kMaxWords] = { { 0, 1, 2 }, { 1, 0, 2 }, { 2, 1, 0 } };
            for (const int w : kNearest[ClampWords(a_words) - 1]) {
                if (!_decks[a_shout][w].Empty()) {
                    return w + 1;
                }
            }
            return 0;
        }

        // The deck those lines come from; empty only when the generic pool is.
        VoiceLines::Deck& For(int a_shout, int a_words)
        {
            const int words = PoolWords(a_shout, a_words);
            return words ? _decks[a_shout][words - 1] : _generic;
        }

    private:
        std::array<std::array<std::vector<VoiceLines::Line>, kMaxWords>, kShouts.size()> _lines;
        std::array<std::array<VoiceLines::Deck, kMaxWords>, kShouts.size()>               _decks;
        VoiceLines::Deck                                                                  _generic;
    };
}
