// CJ's lines for the insult (X) and swear (middle mouse) keys, picked by ear from his speech banks
// with scripts/voice-picker (extract.mjs builds the page, apply.mjs writes the picks below). Each line
// is a San Andreas sound bank (690 to 709, SPC_PA's 20 voices of CJ's moods) and a sound in it; the
// San Andreas plugin plays it in place of the one its speech code would pick. A key with no picks
// plays San Andreas's own choice for its context. Header-only and game-free, so the protocol tests
// cover it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace AhShitRim::VoiceLines
{
    struct Line
    {
        std::uint16_t bank;
        std::uint16_t sound;
    };

    inline constexpr std::uint16_t kFirstBank = 690;  // SND_BANK_SPC_PA_AG
    inline constexpr std::uint16_t kLastBank = 709;   // SND_BANK_SPC_PA_WR2

    // clang-format off
    // --- picks (scripts/voice-picker/apply.mjs) ---
    inline constexpr std::array kInsult{
        Line{ 690, 7 },  // AG ARRESTED
        Line{ 690, 15 },  // AG CRASH_CAR
        Line{ 690, 18 },  // AG CRASH_CAR
        Line{ 690, 28 },  // AG CRASH_GENERIC
        Line{ 690, 29 },  // AG CRASH_GENERIC
        Line{ 690, 38 },  // AG FIGHT
        Line{ 691, 16 },  // AG2 CRASH_CAR
        Line{ 691, 17 },  // AG2 CRASH_CAR
        Line{ 692, 16 },  // AR CRASH_CAR
        Line{ 692, 25 },  // AR CRASH_CAR
        Line{ 692, 27 },  // AR CRASH_GENERIC
        Line{ 692, 32 },  // AR CRASH_GENERIC
        Line{ 693, 29 },  // AR2 CRASH_GENERIC
        Line{ 693, 131 },  // AR2 SOLICIT_PRO_NO
        Line{ 694, 78 },  // CD CRASH_CAR
        Line{ 694, 84 },  // CD CRASH_GENERIC
        Line{ 694, 86 },  // CD CRASH_GENERIC
        Line{ 695, 94 },  // CD2 FIGHT
        Line{ 698, 78 },  // CG CRASH_CAR
        Line{ 699, 68 },  // CG2 CRASH_CAR
        Line{ 699, 72 },  // CG2 CRASH_CAR
        Line{ 699, 73 },  // CG2 CRASH_CAR
        Line{ 699, 80 },  // CG2 CRASH_GENERIC
        Line{ 700, 24 },  // CR ARRESTED
        Line{ 700, 84 },  // CR CRASH_GENERIC
        Line{ 704, 28 },  // PR CRASH_CAR
        Line{ 704, 36 },  // PR CRASH_GENERIC
        Line{ 705, 6 },  // PR2 ARRESTED
        Line{ 705, 24 },  // PR2 CRASH_CAR
        Line{ 706, 30 },  // WG CRASH_GENERIC
        Line{ 707, 24 },  // WG2 CRASH_CAR
        Line{ 708, 23 },  // WR ARRESTED
        Line{ 708, 25 },  // WR ARRESTED
        Line{ 708, 42 },  // WR CRASH_CAR
    };
    inline constexpr std::array kSwear{
        Line{ 692, 19 },  // AR CRASH_CAR
        Line{ 692, 21 },  // AR CRASH_CAR
        Line{ 693, 15 },  // AR2 CRASH_CAR
        Line{ 693, 20 },  // AR2 CRASH_CAR
        Line{ 693, 21 },  // AR2 CRASH_CAR
        Line{ 695, 77 },  // CD2 CRASH_GENERIC
        Line{ 700, 77 },  // CR CRASH_CAR
        Line{ 700, 81 },  // CR CRASH_GENERIC
        Line{ 700, 82 },  // CR CRASH_GENERIC
        Line{ 701, 76 },  // CR2 CRASH_CAR
        Line{ 701, 84 },  // CR2 CRASH_CAR
        Line{ 701, 89 },  // CR2 CRASH_GENERIC
        Line{ 704, 25 },  // PR CRASH_CAR
        Line{ 704, 34 },  // PR CRASH_GENERIC
        Line{ 705, 23 },  // PR2 CRASH_CAR
        Line{ 705, 26 },  // PR2 CRASH_CAR
        Line{ 705, 27 },  // PR2 CRASH_CAR
        Line{ 705, 29 },  // PR2 CRASH_GENERIC
        Line{ 705, 30 },  // PR2 CRASH_GENERIC
        Line{ 705, 31 },  // PR2 CRASH_GENERIC
    };
    // --- end of picks ---
    // clang-format on

    constexpr bool Valid(const Line& a_line) { return a_line.bank >= kFirstBank && a_line.bank <= kLastBank && a_line.sound < 400; }

    // The picks dealt like a deck: every line once in a shuffled order, then a new order whose first
    // line is never the one that just played.
    class Deck
    {
    public:
        explicit Deck(std::span<const Line> a_lines = {}, std::uint32_t a_seed = 1) :
            _lines(a_lines), _rng(a_seed ? a_seed : 1)
        {}

        bool Empty() const { return _lines.empty(); }

        // The next line; only call it when the deck isn't empty.
        Line Next()
        {
            if (_order.empty()) {
                Deal();
            }
            _last = _order.back();
            _order.pop_back();
            return _lines[_last];
        }

    private:
        void Deal()
        {
            _order.resize(_lines.size());
            for (std::size_t i = 0; i < _order.size(); ++i) {
                _order[i] = i;
            }
            for (std::size_t i = _order.size() - 1; i > 0; --i) {
                _rng = _rng * 1664525u + 1013904223u;
                std::swap(_order[i], _order[(_rng >> 8) % (i + 1)]);
            }
            if (_order.size() > 1 && _order.back() == _last) {
                std::swap(_order.back(), _order.front());
            }
        }

        std::span<const Line>    _lines;
        std::vector<std::size_t> _order;
        std::size_t              _last = static_cast<std::size_t>(-1);
        std::uint32_t            _rng;
    };
}
