// CJ's lines for the insult (X) and swear (middle mouse) keys, his answers in Skyrim's conversations
// and his Thu'um, picked by ear from his speech banks with scripts/voice-picker (extract.mjs builds the
// page, apply.mjs writes the picks below). Each line is a San Andreas sound bank (690 to 709,
// SPC_PA's 20 voices of CJ's moods) and a sound in it; the San Andreas plugin plays it in place of the
// one its speech code would pick. A key with no picks plays San Andreas's own choice for its context.
// Header-only and game-free, so the protocol tests cover it.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
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

    // A line picked for one of Skyrim's shouts (protocol/cpp/thuum.h's kShouts key) and word count.
    struct ShoutPick
    {
        std::string_view shout;
        std::uint8_t     words;  // 1 to 3
        Line             line;
    };

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

    // CJ's answers in Skyrim's conversations, one array per category (protocol/cpp/dialogue.h). The
    // picker's "talk" menu picks them; a category with none picked holds his calm lines (CR, CR2) of
    // the San Andreas contexts nearest it (apply.mjs --dialogue-defaults).
    // --- dialogue picks (scripts/voice-picker/apply.mjs) ---
    inline constexpr std::array kGreet{
        Line{ 700, 131 },  // CR GENERIC_HI_MALE
        Line{ 700, 132 },  // CR GENERIC_HI_MALE
        Line{ 700, 133 },  // CR GENERIC_HI_MALE
        Line{ 701, 149 },  // CR2 GENERIC_HI_MALE
        Line{ 701, 150 },  // CR2 GENERIC_HI_MALE
        Line{ 701, 151 },  // CR2 GENERIC_HI_MALE
        Line{ 701, 152 },  // CR2 GENERIC_HI_MALE
        Line{ 701, 153 },  // CR2 GENERIC_HI_MALE
    };
    inline constexpr std::array kAgree{
        Line{ 700, 12 },  // CR AGREE_TO_DO_DRIVEBY
        Line{ 700, 13 },  // CR AGREE_TO_DO_DRIVEBY
        Line{ 700, 340 },  // CR SOLICIT_GEN_YES
        Line{ 700, 341 },  // CR SOLICIT_GEN_YES
        Line{ 700, 342 },  // CR SOLICIT_GEN_YES
        Line{ 700, 343 },  // CR SOLICIT_GEN_YES
        Line{ 700, 344 },  // CR SOLICIT_GEN_YES
        Line{ 700, 345 },  // CR SOLICIT_GEN_YES
        Line{ 701, 15 },  // CR2 AGREE_TO_DO_DRIVEBY
        Line{ 701, 16 },  // CR2 AGREE_TO_DO_DRIVEBY
        Line{ 701, 337 },  // CR2 SOLICIT_GEN_YES
        Line{ 701, 338 },  // CR2 SOLICIT_GEN_YES
        Line{ 701, 339 },  // CR2 SOLICIT_GEN_YES
        Line{ 701, 340 },  // CR2 SOLICIT_GEN_YES
    };
    inline constexpr std::array kRefuse{
        Line{ 700, 117 },  // CR GANGBANG_NO
        Line{ 700, 118 },  // CR GANGBANG_NO
        Line{ 700, 119 },  // CR GANGBANG_NO
        Line{ 700, 120 },  // CR GANGBANG_NO
        Line{ 700, 121 },  // CR GANGBANG_NO
        Line{ 700, 337 },  // CR SOLICIT_GEN_NO
        Line{ 700, 338 },  // CR SOLICIT_GEN_NO
        Line{ 700, 339 },  // CR SOLICIT_GEN_NO
        Line{ 701, 133 },  // CR2 GANGBANG_NO
        Line{ 701, 134 },  // CR2 GANGBANG_NO
        Line{ 701, 135 },  // CR2 GANGBANG_NO
        Line{ 701, 136 },  // CR2 GANGBANG_NO
        Line{ 701, 137 },  // CR2 GANGBANG_NO
        Line{ 701, 333 },  // CR2 SOLICIT_GEN_NO
        Line{ 701, 334 },  // CR2 SOLICIT_GEN_NO
        Line{ 701, 335 },  // CR2 SOLICIT_GEN_NO
        Line{ 701, 336 },  // CR2 SOLICIT_GEN_NO
    };
    inline constexpr std::array kAsk{
        Line{ 700, 387 },  // CR UH_HUH
        Line{ 700, 388 },  // CR UH_HUH
        Line{ 700, 389 },  // CR UH_HUH
        Line{ 701, 386 },  // CR2 UH_HUH
        Line{ 701, 387 },  // CR2 UH_HUH
        Line{ 701, 388 },  // CR2 UH_HUH
    };
    inline constexpr std::array kThreaten{
        Line{ 700, 296 },  // CR PULL_GUN
        Line{ 700, 297 },  // CR PULL_GUN
        Line{ 700, 298 },  // CR PULL_GUN
        Line{ 700, 299 },  // CR PULL_GUN
        Line{ 700, 300 },  // CR PULL_GUN
        Line{ 700, 301 },  // CR PULL_GUN
        Line{ 700, 302 },  // CR PULL_GUN
        Line{ 700, 303 },  // CR PULL_GUN
        Line{ 700, 304 },  // CR PULL_GUN
        Line{ 700, 305 },  // CR PULL_GUN
        Line{ 700, 306 },  // CR PULL_GUN
        Line{ 700, 307 },  // CR PULL_GUN
        Line{ 700, 308 },  // CR PULL_GUN
        Line{ 701, 292 },  // CR2 PULL_GUN
        Line{ 701, 293 },  // CR2 PULL_GUN
        Line{ 701, 294 },  // CR2 PULL_GUN
        Line{ 701, 295 },  // CR2 PULL_GUN
        Line{ 701, 296 },  // CR2 PULL_GUN
        Line{ 701, 297 },  // CR2 PULL_GUN
        Line{ 701, 298 },  // CR2 PULL_GUN
        Line{ 701, 299 },  // CR2 PULL_GUN
        Line{ 701, 300 },  // CR2 PULL_GUN
        Line{ 701, 301 },  // CR2 PULL_GUN
        Line{ 701, 302 },  // CR2 PULL_GUN
        Line{ 701, 303 },  // CR2 PULL_GUN
    };
    inline constexpr std::array kJoke{
        Line{ 700, 46 },  // CR CASINO_WIN
        Line{ 700, 47 },  // CR CASINO_WIN
        Line{ 700, 48 },  // CR CASINO_WIN
        Line{ 701, 44 },  // CR2 CASINO_WIN
        Line{ 701, 45 },  // CR2 CASINO_WIN
        Line{ 701, 46 },  // CR2 CASINO_WIN
    };
    inline constexpr std::array kGoodbye{
        Line{ 700, 327 },  // CR SHOP_LEAVE
        Line{ 700, 328 },  // CR SHOP_LEAVE
        Line{ 700, 329 },  // CR SHOP_LEAVE
        Line{ 701, 322 },  // CR2 SHOP_LEAVE
        Line{ 701, 323 },  // CR2 SHOP_LEAVE
        Line{ 701, 324 },  // CR2 SHOP_LEAVE
        Line{ 701, 325 },  // CR2 SHOP_LEAVE
    };
    // --- end of dialogue picks ---

    // CJ's Thu'um (protocol/cpp/thuum.h): his lines for each shout and word count, from the picker's
    // shout columns, and the generic pool any shout without picks falls back to. With none picked the
    // generic pool holds his angry fight lines (AG, AG2, AR, AR2 FIGHT; apply.mjs --defaults).
    // --- shout picks (scripts/voice-picker/apply.mjs) ---
    inline constexpr std::array<ShoutPick, 0> kShoutPicks{};
    inline constexpr std::array kShoutGeneric{
        Line{ 690, 38 },  // AG FIGHT
        Line{ 690, 39 },  // AG FIGHT
        Line{ 690, 40 },  // AG FIGHT
        Line{ 690, 41 },  // AG FIGHT
        Line{ 690, 42 },  // AG FIGHT
        Line{ 690, 43 },  // AG FIGHT
        Line{ 690, 44 },  // AG FIGHT
        Line{ 690, 45 },  // AG FIGHT
        Line{ 691, 37 },  // AG2 FIGHT
        Line{ 691, 38 },  // AG2 FIGHT
        Line{ 691, 39 },  // AG2 FIGHT
        Line{ 691, 40 },  // AG2 FIGHT
        Line{ 691, 41 },  // AG2 FIGHT
        Line{ 692, 39 },  // AR FIGHT
        Line{ 692, 40 },  // AR FIGHT
        Line{ 692, 41 },  // AR FIGHT
        Line{ 692, 42 },  // AR FIGHT
        Line{ 692, 43 },  // AR FIGHT
        Line{ 692, 44 },  // AR FIGHT
        Line{ 692, 45 },  // AR FIGHT
        Line{ 692, 46 },  // AR FIGHT
        Line{ 692, 47 },  // AR FIGHT
        Line{ 693, 36 },  // AR2 FIGHT
        Line{ 693, 37 },  // AR2 FIGHT
        Line{ 693, 38 },  // AR2 FIGHT
        Line{ 693, 39 },  // AR2 FIGHT
        Line{ 693, 40 },  // AR2 FIGHT
        Line{ 693, 41 },  // AR2 FIGHT
        Line{ 693, 42 },  // AR2 FIGHT
        Line{ 693, 43 },  // AR2 FIGHT
    };
    // --- end of shout picks ---

    // CJ talking on his own (protocol/cpp/situations.h): his lines for each trigger, from the picker's
    // situation columns. An empty one has San Andreas pick for the trigger's context; the stealth kill
    // and shop ones, which San Andreas has no pick of CJ's for, hold his calm lines (CR, CR2) of their
    // context until some are picked (apply.mjs --defaults).
    // --- situation picks (scripts/voice-picker/apply.mjs) ---
    inline constexpr std::array<Line, 0> kFightLines{};
    inline constexpr std::array<Line, 0> kShootLines{};
    inline constexpr std::array<Line, 0> kPullGunLines{};
    inline constexpr std::array kStealthKillLines{
        Line{ 700, 365 },  // CR STEALTH_KILL
        Line{ 700, 366 },  // CR STEALTH_KILL
        Line{ 700, 367 },  // CR STEALTH_KILL
        Line{ 700, 368 },  // CR STEALTH_KILL
        Line{ 701, 359 },  // CR2 STEALTH_KILL
        Line{ 701, 360 },  // CR2 STEALTH_KILL
        Line{ 701, 361 },  // CR2 STEALTH_KILL
        Line{ 701, 362 },  // CR2 STEALTH_KILL
        Line{ 701, 363 },  // CR2 STEALTH_KILL
    };
    inline constexpr std::array<Line, 0> kSingingLines{};
    inline constexpr std::array<Line, 0> kCashLines{};
    inline constexpr std::array kShopBuyLines{
        Line{ 700, 323 },  // CR SHOP_BUY
        Line{ 700, 324 },  // CR SHOP_BUY
        Line{ 700, 325 },  // CR SHOP_BUY
        Line{ 700, 326 },  // CR SHOP_BUY
        Line{ 701, 318 },  // CR2 SHOP_BUY
        Line{ 701, 319 },  // CR2 SHOP_BUY
        Line{ 701, 320 },  // CR2 SHOP_BUY
        Line{ 701, 321 },  // CR2 SHOP_BUY
    };
    inline constexpr std::array kShopLeaveLines{
        Line{ 700, 327 },  // CR SHOP_LEAVE
        Line{ 700, 328 },  // CR SHOP_LEAVE
        Line{ 700, 329 },  // CR SHOP_LEAVE
        Line{ 701, 322 },  // CR2 SHOP_LEAVE
        Line{ 701, 323 },  // CR2 SHOP_LEAVE
        Line{ 701, 324 },  // CR2 SHOP_LEAVE
        Line{ 701, 325 },  // CR2 SHOP_LEAVE
    };
    inline constexpr std::array<Line, 0> kChasedLines{};
    inline constexpr std::array<Line, 0> kCrashLines{};
    // --- end of situation picks ---
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
