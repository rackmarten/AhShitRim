// Wasted (v11), the game-free half: the sequence that plays when the player's health runs out while
// CJ has the player, and which temple the player wakes in afterwards. Header-only and free of game
// types, so the protocol tests cover it; the SKSE plugin (skse/src/player/Vitals.cpp) feeds it.
//
// Skyrim's player is made essential while CJ has it, so at 0 health it bleeds out instead of dying.
// Dying plays San Andreas's "wasted" in slow motion with the colour draining, then the screen fades
// to black, the player is taken to the temple of the hold it fell in, and comes to there at full
// health, 100 gold lighter, as the screen fades back in.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace AhShitRim::Wasted
{
    // ---- Where the player wakes ----

    enum class Temple
    {
        Kynareth,  // Whiterun
        Mara,      // Riften
        Divines,   // Solitude
        Talos,     // Windhelm
        Dibella,   // Markarth
    };

    struct TempleInfo
    {
        Temple                     id;
        const char*                name;
        const char*                cell;    // the interior cell's editor id, for `coc`
        std::array<const char*, 6> places;  // whole words of the location names (a hold, a city) that belong to it; nullptr ends the list
    };

    // The small holds (Hjaalmarch, the Pale, Winterhold, Falkreath) go to the nearest of the five.
    inline constexpr std::array<TempleInfo, 5> kTemples = { {
        { Temple::Kynareth, "Temple of Kynareth", "WhiterunTempleofKynareth", { "Whiterun", "Falkreath", nullptr, nullptr, nullptr, nullptr } },
        { Temple::Mara, "Temple of Mara", "RiftenTempleofMara", { "Rift", "Riften", nullptr, nullptr, nullptr, nullptr } },
        { Temple::Divines, "Temple of the Divines", "SolitudeTempleoftheDivines", { "Haafingar", "Solitude", "Hjaalmarch", "Morthal", nullptr, nullptr } },
        { Temple::Talos, "Temple of Talos", "WindhelmTempleofTalos", { "Eastmarch", "Windhelm", "Pale", "Dawnstar", "Winterhold", nullptr } },
        { Temple::Dibella, "Temple of Dibella", "MarkarthTempleofDibella", { "Reach", "Markarth", nullptr, nullptr, nullptr, nullptr } },
    } };

    inline const TempleInfo& Info(Temple a_temple) { return kTemples[static_cast<std::size_t>(a_temple)]; }

    namespace detail
    {
        inline bool IsLetter(char a_c) { return std::isalpha(static_cast<unsigned char>(a_c)) != 0; }

        // a_word as a whole word of a_text, ignoring case ("Rift" is in "The Rift", not in "Riften").
        inline bool HasWord(std::string_view a_text, std::string_view a_word)
        {
            for (std::size_t at = 0; at + a_word.size() <= a_text.size(); at++) {
                if ((at > 0 && IsLetter(a_text[at - 1])) || (at + a_word.size() < a_text.size() && IsLetter(a_text[at + a_word.size()]))) {
                    continue;
                }
                bool same = true;
                for (std::size_t i = 0; i < a_word.size() && same; i++) {
                    same = std::tolower(static_cast<unsigned char>(a_text[at + i])) == std::tolower(static_cast<unsigned char>(a_word[i]));
                }
                if (same) {
                    return true;
                }
            }
            return false;
        }
    }

    // The temple a location name points to ("Whiterun Hold", "The Rift", "Solitude"), or nullptr.
    inline const TempleInfo* ForPlace(std::string_view a_name)
    {
        for (const auto& temple : kTemples) {
            for (const char* place : temple.places) {
                if (place && detail::HasWord(a_name, place)) {
                    return &temple;
                }
            }
        }
        return nullptr;
    }

    // ---- The sequence ----

    enum class Phase
    {
        Idle,
        Dying,   // slow motion, the colour drains, "wasted"
        Fading,  // to black
        Moving,  // black, waiting for the player to be at the temple
        Waking,  // full health again, fading back in
    };

    enum class Action
    {
        None,
        Begin,   // "wasted" comes up
        Move,    // take the player to the temple
        Revive,  // full health, the fee, bleedout over
        Done,
    };

    struct Timing
    {
        float dying = 3.5f;         // seconds of slow motion
        float fadeOut = 1.0f;
        float moveTimeout = 12.0f;  // the temple never came: wake where the player fell
        float wake = 2.0f;          // the fade back in
        float timeScale = 0.3f;     // game speed while dying
        float desaturate = 0.7f;    // how grey the picture gets at most
    };

    // What to show this frame; the sequence runs on real seconds, so slow motion doesn't stretch it.
    struct Frame
    {
        Action action = Action::None;
        bool   active = false;     // the player can't act, and San Andreas gets kInputWasted
        float  timeScale = 1.0f;
        float  desaturate = 0.0f;  // 0..1 grey wash over the picture
        float  fade = 0.0f;        // 0..1 black over everything
    };

    class Sequence
    {
    public:
        explicit Sequence(Timing a_timing = {}) : _t(a_timing) {}

        Phase GetPhase() const { return _phase; }
        bool  Running() const { return _phase != Phase::Idle; }

        Frame Start()
        {
            _phase = Phase::Dying;
            _time = 0.0f;
            Frame f = Shape(0.0f);
            f.action = Action::Begin;
            return f;
        }

        // a_delta real seconds; a_arrived: the player stands at the temple (or there is nowhere to go),
        // only looked at once Move has been asked for.
        Frame Tick(float a_delta, bool a_arrived)
        {
            if (_phase == Phase::Idle) {
                return {};
            }
            _time += a_delta;
            Action action = Action::None;
            switch (_phase) {
            case Phase::Dying:
                if (_time >= _t.dying) {
                    _phase = Phase::Fading;
                    _time = 0.0f;
                }
                break;
            case Phase::Fading:
                if (_time >= _t.fadeOut) {
                    _phase = Phase::Moving;
                    _time = 0.0f;
                    action = Action::Move;
                }
                break;
            case Phase::Moving:
                if (a_arrived || _time >= _t.moveTimeout) {
                    _phase = Phase::Waking;
                    _time = 0.0f;
                    action = Action::Revive;
                }
                break;
            case Phase::Waking:
                if (_time >= _t.wake) {
                    _phase = Phase::Idle;
                    Frame done;
                    done.action = Action::Done;
                    return done;
                }
                break;
            default: break;
            }
            Frame f = Shape(_time);
            f.action = action;
            return f;
        }

        void Cancel() { _phase = Phase::Idle; }

    private:
        Frame Shape(float a_time) const
        {
            Frame f;
            f.active = _phase != Phase::Idle && _phase != Phase::Waking;
            switch (_phase) {
            case Phase::Dying: {
                const float in = (std::min)(a_time / 0.5f, 1.0f);
                f.timeScale = 1.0f + (_t.timeScale - 1.0f) * in;
                f.desaturate = _t.desaturate * (std::min)(a_time / 1.5f, 1.0f);
                break;
            }
            case Phase::Fading:
                f.timeScale = _t.timeScale;
                f.desaturate = _t.desaturate;
                f.fade = (std::min)(a_time / _t.fadeOut, 1.0f);
                break;
            case Phase::Moving:
                f.fade = 1.0f;
                break;
            case Phase::Waking:
                f.fade = 1.0f - (std::min)(a_time / _t.wake, 1.0f);
                break;
            default: break;
            }
            return f;
        }

        Timing _t;
        Phase  _phase = Phase::Idle;
        float  _time = 0.0f;
    };
}
