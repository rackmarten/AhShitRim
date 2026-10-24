// Missions (v24), the game-free half: Skyrim's quests start and end like San Andreas's missions, and a
// new game opens with the line AhShitRim is named after. Header-only and free of game types, so the
// protocol tests cover it; the SKSE plugin (skse/src/player/Missions.cpp) feeds it and San Andreas
// (sa/src/audio.cpp) plays the sounds, by its own ids from the player's own install.
//
// - A quest starting with a journal entry shows its name as San Andreas's mission title instead of
//   Skyrim's "quest started". Skyrim already only announces a quest once it has a journal entry; misc
//   objectives (the Miscellaneous quest type) stay Skyrim's.
// - A quest completed shows "mission passed!" and "respect +" (San Andreas's M_PASSR) instead of
//   Skyrim's "quest completed", with San Andreas's mission passed tune.
// - The Helgen intro: right after Ralof's "Hey, you. You're finally awake." in the cart, CJ says "Ah
//   shit, here we go again.", the first line of San Andreas's intro voice-over, with its subtitle.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Missions
{
    // ---- Which quests count ----

    // Skyrim's HUD messages for quests (RE::HUD_MESSAGE_TYPE) and its Miscellaneous quest type
    // (QUEST_DATA::Type), the ones the rules read.
    inline constexpr int kHudQuestStarted = 14;
    inline constexpr int kHudQuestComplete = 15;
    inline constexpr int kQuestMiscellaneous = 6;

    enum class Notice
    {
        None,
        Title,   // the quest's name, bottom right
        Passed,  // "mission passed!", "respect +" and the tune
    };

    // What San Andreas shows for one of Skyrim's HUD messages about a quest of a_questType named a_name.
    // None leaves the message to Skyrim (a failed quest, an objective, a misc quest, a quest without a name).
    constexpr Notice NoticeOf(int a_hudType, int a_questType, std::string_view a_name)
    {
        if (a_questType == kQuestMiscellaneous || a_name.empty()) {
            return Notice::None;
        }
        return a_hudType == kHudQuestStarted ? Notice::Title : a_hudType == kHudQuestComplete ? Notice::Passed : Notice::None;
    }

    // ---- The order and timing ----

    inline constexpr float kTitleSeconds = 4.0f;    // a title at full (SaHud::ShowMissionTitle), with 0.25 s in and 1 s out
    inline constexpr float kTitleShown = kTitleSeconds + 1.25f;
    inline constexpr float kPassedSeconds = 5.0f;   // "mission passed!" at full (M_PASSR's 5000 ms)
    inline constexpr float kTuneSeconds = 8.3f;     // the mission passed tune (stream track 183)
    inline constexpr float kGapSeconds = 0.5f;      // between one notice going and the next coming

    // How long a notice keeps the screen: a passed mission until its tune is over.
    constexpr float SecondsOf(Notice a_notice)
    {
        return a_notice == Notice::Title ? kTitleShown : a_notice == Notice::Passed ? (std::max)(kPassedSeconds + 1.15f, kTuneSeconds) : 0.0f;
    }

    struct Step
    {
        Notice      notice = Notice::None;
        std::string name;
    };

    // The notices one after another, never two at once. A quest passing goes before the titles still
    // waiting (San Andreas ends a mission before the next one starts: Before the Storm's end and Bleak
    // Falls Barrow's start come together), and the same notice of the same quest twice is shown once.
    class Sequence
    {
    public:
        void Push(Notice a_notice, std::string a_name)
        {
            if (a_notice == Notice::None) {
                return;
            }
            for (const auto& step : _waiting) {
                if (step.notice == a_notice && step.name == a_name) {
                    return;
                }
            }
            Step step{ a_notice, std::move(a_name) };
            if (a_notice == Notice::Passed) {
                const auto at = std::find_if(_waiting.begin(), _waiting.end(), [](const Step& a_step) { return a_step.notice == Notice::Title; });
                _waiting.insert(at, std::move(step));
            } else {
                _waiting.push_back(std::move(step));
            }
        }

        // a_delta seconds on: the notice to show now, or None.
        Step Tick(float a_delta)
        {
            _busy = (std::max)(_busy - a_delta, 0.0f);
            if (_busy > 0.0f || _waiting.empty()) {
                return {};
            }
            Step step = std::move(_waiting.front());
            _waiting.pop_front();
            _busy = SecondsOf(step.notice) + kGapSeconds;
            return step;
        }

        bool Idle() const { return _busy <= 0.0f && _waiting.empty(); }
        std::size_t Waiting() const { return _waiting.size(); }
        void Clear() { *this = {}; }

    private:
        std::deque<Step> _waiting;
        float            _busy = 0.0f;  // seconds the notice up keeps the screen
    };

    // ---- The Helgen intro ----

    inline constexpr std::uint32_t kRalofFinallyAwake = 0x00101AEA;  // Skyrim.esm: MQ101's INFO for Ralof's first line in the cart (one response)
    inline constexpr float         kIntroDelay = 0.3f;               // CJ answers this long after Ralof's line ends
    inline constexpr float         kIntroLineSeconds = 2.9f;         // the voice-over's first line (SCRIPT bank 353, sound 0)
    inline constexpr std::string_view kIntroLine = "Ah shit, here we go again.";

    // When CJ says it: once Ralof's line has ended (Skyrim's TESTopicInfoEvent), once until a game loads.
    class Intro
    {
    public:
        void OnTopicEnd(std::uint32_t a_info)
        {
            if (a_info == kRalofFinallyAwake && !_said && _wait < 0.0f) {
                _wait = kIntroDelay;
            }
        }

        // True on the frame CJ says it.
        bool Tick(float a_delta)
        {
            if (_wait < 0.0f) {
                return false;
            }
            _wait -= a_delta;
            if (_wait > 0.0f) {
                return false;
            }
            _wait = -1.0f;
            _said = true;
            return true;
        }

        bool Waiting() const { return _wait >= 0.0f; }
        void Clear() { *this = {}; }  // a game loads: a save from before the cart hears it again

    private:
        float _wait = -1.0f;  // seconds to go, -1 while nothing waits
        bool  _said = false;
    };

    // ---- The sounds (InputState.cue, kCue*) ----

    inline constexpr std::int16_t kMissionPassedBeat = 11;  // CAudioEngine::PreloadBeatTrack's: PLAY_MISSION_PASSED_TUNE 1 (stream track 183)
    inline constexpr std::int32_t kIntroLineAudio = 43200;  // CAudioEngine::PreloadMissionAudio's: the intro's voice-over line VO_AA (SCRIPT bank 353, sound 0)
    inline constexpr float        kCueLoadSeconds = 1.5f;   // San Andreas loading the sound from its streams or banks, at most

    // How long a cue plays, loading included: Skyrim keeps San Andreas's volumes up that long while it has the
    // player itself (San Andreas is muted then), and San Andreas lets it go after that whatever happened.
    constexpr float CueSeconds(std::uint8_t a_cue)
    {
        return a_cue == Protocol::kCueMissionPassed ? kTuneSeconds + kCueLoadSeconds
             : a_cue == Protocol::kCueIntroLine     ? kIntroLineSeconds + kCueLoadSeconds
                                                    : 0.0f;
    }

    // A cue playing, by time: the longest of those started.
    class CueTimer
    {
    public:
        void Start(std::uint8_t a_cue) { _left = (std::max)(_left, CueSeconds(a_cue)); }
        void Tick(float a_delta) { _left = (std::max)(_left - a_delta, 0.0f); }
        bool Playing() const { return _left > 0.0f; }
        void Clear() { _left = 0.0f; }

    private:
        float _left = 0.0f;
    };
}
