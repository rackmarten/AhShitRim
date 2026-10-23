#include "player/Missions.h"

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
        static TopicSink topics;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESTopicInfoEvent>(&topics);
        }
    }

    bool OnHudMessage(const RE::HUDData& a_data)
    {
        const auto type = static_cast<int>(a_data.type.get());
        if (type != kHudQuestStarted && type != kHudQuestComplete) {
            return false;
        }
        const auto* quest = a_data.quest;
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
        {
            std::scoped_lock guard(state.lock);
            notices.swap(state.notices);
            ralofDone = std::exchange(state.ralofDone, false);
        }
        for (auto& notice : notices) {
            state.sequence.Push(notice.notice, std::move(notice.name));
        }
        if (ralofDone) {
            state.intro.OnTopicEnd(kRalofFinallyAwake);
        }

        std::uint8_t cue = 0;
        if (const auto step = state.sequence.Tick(a_delta); step.notice == Notice::Title) {
            SaHud::ShowMissionTitle(step.name);
        } else if (step.notice == Notice::Passed) {
            SaHud::ShowBigMessage(SaHud::BigMessage::MissionPassed, kPassedSeconds);
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

    void OnGameLoad()
    {
        std::scoped_lock guard(state.lock);
        state.notices.clear();
        state.ralofDone = false;
        state.sequence.Clear();
        state.intro.Clear();
        state.cue.Clear();
        state.cuePlaying = false;
    }
}
