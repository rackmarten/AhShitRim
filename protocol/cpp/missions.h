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
// - A Miscellaneous quest done (a favour, a delivery) is an odd job, as San Andreas's trucking is:
//   "mission passed!" alone (M_PASSD) with the tune, and no title when it starts, so fetch quests don't
//   spam. [Missions] MiscPassed=0 in AhShitRim.ini turns that off.
// - The Helgen intro: right after Ralof's "Hey, you. You're finally awake." in the cart, CJ says "Ah
//   shit, here we go again.", the first line of San Andreas's intro voice-over, with its subtitle.

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::Missions
{
    // ---- Which quests count ----

    // Skyrim's HUD messages for quests (RE::HUD_MESSAGE_TYPE) and its Miscellaneous quest type
    // (QUEST_DATA::Type), the ones the rules read.
    inline constexpr int kHudQuestStarted = 14;
    inline constexpr int kHudQuestComplete = 15;
    inline constexpr int kHudObjectiveComplete = 18;
    inline constexpr int kQuestMiscellaneous = 6;

    enum class Notice
    {
        None,
        Title,   // the quest's name, bottom right
        Passed,  // "mission passed!", "respect +" and the tune
        OddJob,  // "mission passed!" alone and the tune: a misc quest done
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

    // Whether one of Skyrim's HUD messages may mean a Miscellaneous quest was just done. Skyrim sends no
    // "quest completed" for those, only their objectives (A Lovely Letter's last is "Talk to Faendal"
    // completed, and its script then stops the quest), so an objective completed makes the quest a
    // candidate, and MiscWatch says whether it ended. a_miscPassed is [Missions] MiscPassed.
    constexpr bool MiscCandidate(int a_hudType, int a_questType, bool a_miscPassed)
    {
        return a_miscPassed && a_questType == kQuestMiscellaneous && (a_hudType == kHudObjectiveComplete || a_hudType == kHudQuestComplete);
    }

    // ---- The order and timing ----

    inline constexpr float kTitleSeconds = 4.0f;    // a title at full (SaHud::ShowMissionTitle), with 0.25 s in and 1 s out
    inline constexpr float kTitleShown = kTitleSeconds + 1.25f;
    inline constexpr float kPassedSeconds = 5.0f;   // "mission passed!" at full (M_PASSR's 5000 ms)
    inline constexpr float kTuneSeconds = 8.3f;     // the mission passed tune (stream track 183)
    inline constexpr float kGapSeconds = 0.5f;      // between one notice going and the next coming

    constexpr bool IsPassed(Notice a_notice) { return a_notice == Notice::Passed || a_notice == Notice::OddJob; }

    // How long a notice keeps the screen: a passed mission until its tune is over.
    constexpr float SecondsOf(Notice a_notice)
    {
        return a_notice == Notice::Title ? kTitleShown : IsPassed(a_notice) ? (std::max)(kPassedSeconds + 1.15f, kTuneSeconds) : 0.0f;
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
            if (IsPassed(a_notice)) {
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

    // ---- Odd jobs ----

    inline constexpr float kMiscWatchSeconds = 3.0f;  // a candidate's quest has this long to end

    // Misc quests whose objective was just completed (MiscCandidate), until their quest has ended
    // (completed, stopping or stopped) or kMiscWatchSeconds pass. Each quest passes once until a game loads.
    class MiscWatch
    {
    public:
        void Add(std::uint32_t a_quest, std::string a_name)
        {
            if (_passed.contains(a_quest)) {
                return;
            }
            for (auto& watch : _watching) {
                if (watch.quest == a_quest) {
                    watch.left = kMiscWatchSeconds;
                    return;
                }
            }
            _watching.push_back({ a_quest, std::move(a_name), kMiscWatchSeconds });
        }

        // a_ended(quest id) says whether the quest has ended now; the odd jobs passed this frame.
        template <class Ended>
        std::vector<Step> Tick(float a_delta, Ended&& a_ended)
        {
            std::vector<Step> out;
            for (auto it = _watching.begin(); it != _watching.end();) {
                if (a_ended(it->quest)) {
                    _passed.insert(it->quest);
                    out.push_back({ Notice::OddJob, std::move(it->name) });
                    it = _watching.erase(it);
                } else if ((it->left -= a_delta) <= 0.0f) {
                    it = _watching.erase(it);
                } else {
                    ++it;
                }
            }
            return out;
        }

        std::size_t Watching() const { return _watching.size(); }
        void Clear() { *this = {}; }

    private:
        struct Watch
        {
            std::uint32_t quest;
            std::string   name;
            float         left;
        };
        std::vector<Watch>      _watching;
        std::set<std::uint32_t> _passed;
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
