#include "player/Missions.h"

#include "Ini.h"
#include "hud/SaHud.h"
#include "../../../protocol/cpp/missions.h"  // by path: "missions.h" would find player/Missions.h first on Windows

#include <atomic>
#include <mutex>

namespace AhShitRim::Missions
{
    namespace
    {
        // The HUD hook and the event sink may run off the main thread; Update takes what they queued.
        struct State
        {
            std::mutex        lock;
            std::vector<Step> notices;           // from the HUD hook, for the sequence
            std::vector<std::pair<RE::FormID, std::string>> miscDone;  // misc quests whose objective was completed, for the watch
            std::atomic<bool> miscPassed = true;  // [Missions] MiscPassed
            MiscWatch         misc;      // main thread
            bool              ralofDone = false;  // Ralof's line ended
            Sequence          sequence;  // main thread
            Intro             intro;     // main thread
            CueTimer          cue;       // main thread
            std::atomic<bool> cuePlaying = false;
        } state;

        class TopicSink : public RE::BSTEventSink<RE::TESTopicInfoEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
            {
                if (a_event && a_event->topicInfoFormID == kRalofFinallyAwake && a_event->type == RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd) {
                    SKSE::log::info("missions: Ralof's first line in the cart ended, CJ answers");
                    std::scoped_lock guard(state.lock);
                    state.ralofDone = true;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void OnDataLoaded()
    {
        state.miscPassed = Ini::Value("Missions", "MiscPassed") != "0";
        SKSE::log::info("missions: misc quests done are odd jobs passed: {} ([Missions] MiscPassed)", state.miscPassed ? "on" : "off");
        static TopicSink topics;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESTopicInfoEvent>(&topics);
        }
    }

    bool OnHudMessage(const RE::HUDData& a_data)
    {
        const auto type = static_cast<int>(a_data.type.get());
        const auto* quest = a_data.quest;
        if (type >= kHudQuestStarted && type <= kHudQuestStarted + 5) {  // quest and objective messages: what Skyrim announces, for the log
            SKSE::log::info("missions: Skyrim's HUD message {} '{}' for {:08X} (type {})", type, a_data.text.c_str() ? a_data.text.c_str() : "",
                quest ? quest->GetFormID() : 0, quest ? static_cast<int>(quest->GetType()) : -1);
        }
        if (quest && MiscCandidate(type, static_cast<int>(quest->GetType()), state.miscPassed)) {
            std::scoped_lock guard(state.lock);  // Skyrim's own message stays; the watch sees whether the quest ended
            state.miscDone.emplace_back(quest->GetFormID(), quest->GetName() ? quest->GetName() : "");
            return false;
        }
        if (type != kHudQuestStarted && type != kHudQuestComplete) {
            return false;
        }
        std::string name = a_data.text.c_str() ? a_data.text.c_str() : "";
        if (name.empty() && quest && quest->GetName()) {
            name = quest->GetName();
        }
        const auto notice = NoticeOf(type, quest ? static_cast<int>(quest->GetType()) : -1, name);
        if (notice == Notice::None) {
            return false;
        }
        SKSE::log::info("missions: {} {} ({:08X}), shown as San Andreas's {}", notice == Notice::Title ? "started" : "completed", name,
            quest ? quest->GetFormID() : 0, notice == Notice::Title ? "mission title" : "mission passed");
        std::scoped_lock guard(state.lock);
        state.notices.push_back({ notice, std::move(name) });
        return true;
    }

    std::uint8_t Update(float a_delta)
    {
        std::vector<Step> notices;
        bool              ralofDone = false;
        std::vector<std::pair<RE::FormID, std::string>> miscDone;
        {
            std::scoped_lock guard(state.lock);
            notices.swap(state.notices);
            miscDone.swap(state.miscDone);
            ralofDone = std::exchange(state.ralofDone, false);
        }
        for (auto& notice : notices) {
            state.sequence.Push(notice.notice, std::move(notice.name));
        }
        if (ralofDone) {
            state.intro.OnTopicEnd(kRalofFinallyAwake);
        }
        for (auto& [quest, name] : miscDone) {
            state.misc.Add(quest, std::move(name));
        }
        // A misc quest ended after its objective: its script completed or stopped it.
        for (auto& step : state.misc.Tick(a_delta, [](std::uint32_t a_quest) {
                 const auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_quest);
                 return quest && (quest->IsCompleted() || quest->IsStopping() || quest->IsStopped());
             })) {
            SKSE::log::info("missions: the misc quest {} is done, shown as San Andreas's odd job passed", step.name);
            state.sequence.Push(step.notice, std::move(step.name));
        }

        std::uint8_t cue = 0;
        if (const auto step = state.sequence.Tick(a_delta); step.notice == Notice::Title) {
            SaHud::ShowMissionTitle(step.name);
        } else if (IsPassed(step.notice)) {
            SaHud::ShowBigMessage(step.notice == Notice::OddJob ? SaHud::BigMessage::OddJobPassed : SaHud::BigMessage::MissionPassed, kPassedSeconds);
            cue = Protocol::kCueMissionPassed;
        }
        // One cue a frame: CJ's line waits a frame for the tune.
        if (!cue && state.intro.Tick(a_delta)) {
            SaHud::ShowSubtitle(std::string(kIntroLine), kIntroLineSeconds);
            cue = Protocol::kCueIntroLine;
        }
        state.cue.Tick(a_delta);
        if (cue) {
            state.cue.Start(cue);
        }
        state.cuePlaying = state.cue.Playing();
        return cue;
    }

    bool CuePlaying() { return state.cuePlaying; }

    void TestIntro()
    {
        SKSE::log::info("missions: test: as if Ralof's line had ended");
        std::scoped_lock guard(state.lock);
        state.ralofDone = true;
    }

    void OnGameLoad()
    {
        std::scoped_lock guard(state.lock);
        state.notices.clear();
        state.miscDone.clear();
        state.misc.Clear();
        state.ralofDone = false;
        state.sequence.Clear();
        state.intro.Clear();
        state.cue.Clear();
        state.cuePlaying = false;
    }
}
