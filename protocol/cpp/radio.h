// The radio (v10): Skyrim's audio sliders to San Andreas's volumes, and the two stations Skyrim plays
// after San Andreas's own: Radio Sovngarde, on Skyrim's own music, and Sweetroll FM, on CC0 music that
// ships with AhShitRim. Header-only and free of game types, so the protocol tests cover it; the SKSE
// plugin plays what it picks (skse/src/audio/).
//
// Radio Sovngarde follows Skyrim's mood: tuning in starts with the main theme, then combat tracks
// while the player is in combat and the exploration tracks of the time of day otherwise, with one of
// the bards' instrumentals after every two exploration tracks. The tracks are read from the player's
// own Skyrim archives by path; nothing is copied.
//
// Sweetroll FM shuffles its tracks (music/CREDITS.md), installed as loose files under Skyrim's
// Data\Music\AhShitRim\SweetrollFM\: every one once before any repeats, never one twice in a row.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace AhShitRim::Radio
{
    // San Andreas's volumes from Skyrim's sliders (each 0..1): effects for CJ, his vehicles and
    // weapons, music for the radio. Silent while Skyrim has the player.
    struct Volumes
    {
        float effects = 0.0f;
        float music = 0.0f;
    };

    constexpr Volumes FromSliders(float a_master, float a_effects, float a_music, bool a_owned)
    {
        if (!a_owned) {
            return {};
        }
        const float master = std::clamp(a_master, 0.0f, 1.0f);
        return { master * std::clamp(a_effects, 0.0f, 1.0f), master * std::clamp(a_music, 0.0f, 1.0f) };
    }

    // ---- Stations ----

    // The dial's stations (the pose's radioStation): 0 is the emergency services' (Emergency AA, never
    // in the dial), 1..11 San Andreas's stations (its eRadioID), 12 Radio Sovngarde, 13 Sweetroll FM
    // and 14 the radio off. San Andreas's own radio is off (its eRadioID 13) on 12 to 14.
    inline constexpr std::int8_t kEmergency = 0;
    inline constexpr std::int8_t kSovngarde = 12;
    inline constexpr std::int8_t kSweetroll = 13;
    inline constexpr std::int8_t kRadioOff = 14;
    inline constexpr std::int8_t kSaUserTracks = 12;  // San Andreas's eRadioID for its User Track Player
    inline constexpr std::int8_t kSaRadioOff = 13;    // and for off

    // The names San Andreas's text gives its own (FEA_R0..FEA_R10, FEA_NON), empty for none.
    constexpr std::string_view StationName(std::int8_t a_station)
    {
        constexpr std::string_view kNames[] = { "", "Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR", "Radio Los Santos", "Radio X",
            "CSR 103.9", "K-Jah West", "Master Sounds 98.3", "WCTR", "Radio Sovngarde", "Sweetroll FM", "Radio Off" };
        return a_station >= 0 && a_station < static_cast<std::int8_t>(std::size(kNames)) ? kNames[a_station] : std::string_view();
    }

    // A station plays (Skyrim's own music goes quiet): any but none and off.
    constexpr bool Plays(std::int8_t a_station) { return a_station >= kEmergency && a_station != kRadioOff; }

    // Skyrim plays the station itself (San Andreas's radio is off).
    constexpr bool SkyrimPlays(std::int8_t a_station) { return a_station == kSovngarde || a_station == kSweetroll; }

    // The eRadioID San Andreas tunes to for a dial station.
    constexpr std::int8_t SaStation(std::int8_t a_station) { return SkyrimPlays(a_station) || a_station == kRadioOff ? kSaRadioOff : a_station; }

    // The mouse wheel's dial: a_steps up (negative down) from a_station through 1..14, round and round.
    // From the emergency station (or none) the dial starts at its first station.
    constexpr std::int8_t Dial(std::int8_t a_station, int a_steps)
    {
        int at = a_station >= 1 && a_station <= kRadioOff ? a_station - 1 : (a_steps > 0 ? -1 : 0);
        at = ((at + a_steps) % kRadioOff + kRadioOff) % kRadioOff;
        return static_cast<std::int8_t>(at + 1);
    }

    // ---- Radio Sovngarde ----

    enum class Kind : std::uint8_t
    {
        None,
        MainTheme,
        Explore,
        Combat,
        Bard,
    };

    inline constexpr std::string_view kMainTheme = "music\\special\\mus_maintheme.xwm";

    inline constexpr std::array<std::string_view, 9> kExploreDay = {
        "music\\explore\\mus_explore_day_01.xwm", "music\\explore\\mus_explore_day_02.xwm",
        "music\\explore\\mus_explore_day_03.xwm", "music\\explore\\mus_explore_day_04.xwm",
        "music\\explore\\mus_explore_day_05.xwm", "music\\explore\\mus_explore_day_06.xwm",
        "music\\explore\\mus_explore_day_07.xwm", "music\\explore\\mus_explore_day_08.xwm",
        "music\\explore\\mus_explore_day_09.xwm",
    };
    inline constexpr std::array<std::string_view, 2> kExploreMorning = {
        "music\\explore\\mus_explore_morning_01.xwm", "music\\explore\\mus_explore_morning_02.xwm",
    };
    inline constexpr std::array<std::string_view, 3> kExploreDusk = {
        "music\\explore\\mus_explore_dusk_01.xwm", "music\\explore\\mus_explore_dusk_02.xwm",
        "music\\explore\\mus_explore_dusk_03.xwm",
    };
    inline constexpr std::array<std::string_view, 6> kExploreNight = {
        "music\\explore\\mus_explore_night_01.xwm", "music\\explore\\mus_explore_night_02.xwm",
        "music\\explore\\mus_explore_night_03.xwm", "music\\explore\\mus_explore_night_04.xwm",
        "music\\explore\\mus_explore_night_05.xwm", "music\\explore\\mus_explore_night_06.xwm",
    };
    // The combat tracks without their finales (short endings Skyrim plays after a fight).
    inline constexpr std::array<std::string_view, 8> kCombat = {
        "music\\combat\\mus_combat_01.xwm", "music\\combat\\mus_combat_02.xwm",
        "music\\combat\\mus_combat_03.xwm", "music\\combat\\mus_combat_04.xwm",
        "music\\combat\\mus_combat_06.xwm", "music\\combat\\mus_combat_boss.xwm",
        "music\\combat\\mus_combat_boss_02.xwm", "music\\combat\\mus_combat_boss_03.xwm",
    };
    // What Skyrim's bards play on lute, flute and drum. Their sung songs are dialogue lines split
    // by verse in voice files, which Skyrim can't play from a path.
    inline constexpr std::array<std::string_view, 13> kBard = {
        "sound\\fx\\mus\\bard\\lute\\mus_bardlute_01.wav", "sound\\fx\\mus\\bard\\lute\\mus_bardlute_02.wav",
        "sound\\fx\\mus\\bard\\lute\\mus_bardlute_03.wav", "sound\\fx\\mus\\bard\\lute\\mus_bardlute_04.wav",
        "sound\\fx\\mus\\bard\\flute\\mus_bardflute_01_sarba.wav", "sound\\fx\\mus\\bard\\flute\\mus_bardflute_02_psalm.wav",
        "sound\\fx\\mus\\bard\\flute\\mus_bardflute_03_bookoflife.wav", "sound\\fx\\mus\\bard\\flute\\mus_bardflute_04_ascents.wav",
        "sound\\fx\\mus\\bard\\flute\\mus_bardflute_05_trip.wav", "sound\\fx\\mus\\bard\\flute\\mus_bardflute_06_springhouse.wav",
        "sound\\fx\\mus\\bard\\drum\\mus_barddrum_01.wav", "sound\\fx\\mus\\bard\\drum\\mus_barddrum_02.wav",
        "sound\\fx\\mus\\bard\\drum\\mus_barddrum_03.wav",
    };

    // The exploration tracks for a Skyrim game hour (0..24): morning 5-9, day 9-17, dusk 17-21,
    // night otherwise, as Skyrim's own music types split them.
    constexpr std::span<const std::string_view> ExploreFor(float a_hour)
    {
        if (a_hour >= 5.0f && a_hour < 9.0f) {
            return kExploreMorning;
        }
        if (a_hour >= 9.0f && a_hour < 17.0f) {
            return kExploreDay;
        }
        if (a_hour >= 17.0f && a_hour < 21.0f) {
            return kExploreDusk;
        }
        return kExploreNight;
    }

    struct Track
    {
        Kind             kind = Kind::None;
        std::string_view path;
    };

    // Picks Radio Sovngarde's tracks. Tune() on tuning in, Next() whenever the last track ended,
    // and MoodChanged() every frame: true means the track playing is the wrong mood (a fight started
    // during exploration, or ended during a combat track), and Next() should cut it short.
    class Sovngarde
    {
    public:
        explicit Sovngarde(std::uint32_t a_seed = 1) : _rng(a_seed ? a_seed : 1) {}

        void Tune()
        {
            _tuned = true;
            _current = {};
            _sinceBard = 0;
        }

        Track Current() const { return _current; }

        bool MoodChanged(bool a_combat) const
        {
            switch (_current.kind) {
            case Kind::Combat: return !a_combat;
            case Kind::Explore:
            case Kind::Bard:
            case Kind::MainTheme: return a_combat;
            default: return false;
            }
        }

        Track Next(bool a_combat, float a_hour)
        {
            if (_tuned) {
                _tuned = false;
                _current = { Kind::MainTheme, kMainTheme };
            } else if (a_combat) {
                _current = { Kind::Combat, Pick(kCombat) };
            } else if (_current.kind == Kind::Explore && _sinceBard >= 2) {
                _sinceBard = 0;
                _current = { Kind::Bard, Pick(kBard) };
            } else {
                _sinceBard++;
                _current = { Kind::Explore, Pick(ExploreFor(a_hour)) };
            }
            return _current;
        }

    private:
        // A random track of the pool, never the one that just played.
        std::string_view Pick(std::span<const std::string_view> a_pool)
        {
            _rng = _rng * 1664525u + 1013904223u;
            std::size_t i = (_rng >> 8) % a_pool.size();
            if (a_pool.size() > 1 && a_pool[i] == _current.path) {
                i = (i + 1) % a_pool.size();
            }
            return a_pool[i];
        }

        std::uint32_t _rng;
        bool          _tuned = false;
        int           _sinceBard = 0;
        Track         _current;
    };

    // ---- Sweetroll FM ----

    // The tracks, by path under Skyrim's Data folder, as named in music/CREDITS.md.
    inline constexpr std::array<std::string_view, 11> kSweetrollTracks = {
        "music\\ahshitrim\\sweetrollfm\\01-the-bards-tale.xwm",
        "music\\ahshitrim\\sweetrollfm\\02-kings-feast.xwm",
        "music\\ahshitrim\\sweetrollfm\\03-the-old-tower-inn.xwm",
        "music\\ahshitrim\\sweetrollfm\\04-minstrel-dance.xwm",
        "music\\ahshitrim\\sweetrollfm\\05-harvest-season.xwm",
        "music\\ahshitrim\\sweetrollfm\\06-exploration.xwm",
        "music\\ahshitrim\\sweetrollfm\\07-grasslands-theme.xwm",
        "music\\ahshitrim\\sweetrollfm\\08-morleys-pavan.xwm",
        "music\\ahshitrim\\sweetrollfm\\09-interweave.xwm",
        "music\\ahshitrim\\sweetrollfm\\10-the-highland-road.xwm",
        "music\\ahshitrim\\sweetrollfm\\11-sunset-plains.xwm",
    };

    // A playlist shuffled like a deck: every track once, then a new order, whose first track is never
    // the one that just played. Tune() on tuning in starts a new order (a random track first), Next()
    // whenever the last track ended. An empty playlist plays nothing (Next() gives an empty path).
    class Shuffle
    {
    public:
        explicit Shuffle(std::span<const std::string_view> a_tracks = {}, std::uint32_t a_seed = 1) :
            _tracks(a_tracks.begin(), a_tracks.end()), _rng(a_seed ? a_seed : 1)
        {}

        void SetTracks(std::span<const std::string_view> a_tracks)
        {
            _tracks.assign(a_tracks.begin(), a_tracks.end());
            _order.clear();
            _current = {};
        }

        std::size_t Size() const { return _tracks.size(); }

        void Tune() { _order.clear(); }

        std::string_view Current() const { return _current; }

        std::string_view Next()
        {
            if (_tracks.empty()) {
                return _current = {};
            }
            if (_order.empty()) {
                Deal();
            }
            _current = _tracks[_order.back()];
            _order.pop_back();
            return _current;
        }

    private:
        // A new order, played from the back.
        void Deal()
        {
            _order.resize(_tracks.size());
            for (std::size_t i = 0; i < _order.size(); ++i) {
                _order[i] = i;
            }
            for (std::size_t i = _order.size() - 1; i > 0; --i) {
                std::swap(_order[i], _order[Random(i + 1)]);
            }
            if (_order.size() > 1 && _tracks[_order.back()] == _current) {
                std::swap(_order.back(), _order.front());
            }
        }

        std::size_t Random(std::size_t a_below)
        {
            _rng = _rng * 1664525u + 1013904223u;
            return (_rng >> 8) % a_below;
        }

        std::vector<std::string_view> _tracks;
        std::vector<std::size_t>      _order;
        std::uint32_t                 _rng;
        std::string_view              _current;
    };
}
