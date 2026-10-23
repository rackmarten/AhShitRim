#include "player/GunRewards.h"

#include "hud/SaHud.h"
#include "player/Arms.h"
#include "rewards.h"

#include <mutex>
#include <set>

namespace AhShitRim::GunRewards
{
    namespace
    {
        constexpr int kLocationDepth = 8;  // locations held by locations, at most this deep

        // The sinks may run off the main thread; Update hands out what they queued.
        struct State
        {
            std::mutex                          lock;
            std::vector<const Rewards::Reward*> waiting;
            std::set<std::uint32_t>             bossesPaid;  // locations whose boss gave its reward this session
        } state;

        void Queue(const Rewards::Reward* a_reward)
        {
            std::scoped_lock guard(state.lock);
            state.waiting.push_back(a_reward);
        }

        // Skyrim marks a dungeon's boss with the Boss location ref type.
        bool IsBoss(RE::TESObjectREFR* a_ref)
        {
            const auto* type = a_ref->extraList.GetByType<RE::ExtraLocationRefType>();
            return type && type->locRefType && type->locRefType->GetFormID() == Rewards::kBossRefType;
        }

        // The locations a_ref is in, innermost first: its own, else its cell's, and those holding it.
        std::vector<std::uint32_t> LocationsOf(RE::TESObjectREFR* a_ref)
        {
            std::vector<std::uint32_t> ids;
            auto* location = a_ref->GetCurrentLocation();
            if (!location) {
                auto* cell = a_ref->GetParentCell();
                location = cell ? cell->GetLocation() : nullptr;
            }
            for (int i = 0; location && i < kLocationDepth; i++, location = location->parentLoc) {
                ids.push_back(location->GetFormID());
            }
            return ids;
        }

        class StageSink : public RE::BSTEventSink<RE::TESQuestStageEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event, RE::BSTEventSource<RE::TESQuestStageEvent>*) override
            {
                if (const auto* step = a_event ? Rewards::ForStage(a_event->formID, a_event->stage) : nullptr) {
                    SKSE::log::info("gun rewards: {} reached stage {} ({:08X}), CJ gets a {}", step->name, step->stage, step->quest, step->reward.weapon);
                    Queue(&step->reward);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                auto* dying = a_event ? a_event->actorDying.get() : nullptr;
                if (!dying || !IsBoss(dying)) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const auto  locations = LocationsOf(dying);
                const auto* boss = Rewards::ForBoss(locations.begin(), locations.end());
                if (!boss) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                {
                    std::scoped_lock guard(state.lock);
                    if (!state.bossesPaid.insert(boss->location).second) {
                        return RE::BSEventNotifyControl::kContinue;  // another boss of the same place
                    }
                }
                SKSE::log::info("gun rewards: the boss of {} ({:08X}) died, CJ gets a {}", boss->name, dying->GetFormID(), boss->reward.weapon);
                Queue(&boss->reward);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void OnDataLoaded()
    {
        static StageSink stages;
        static DeathSink deaths;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESQuestStageEvent>(&stages);
            events->AddEventSink<RE::TESDeathEvent>(&deaths);
            SKSE::log::info("gun rewards: {} quest steps and {} bosses give guns", std::size(Rewards::kQuestSteps), std::size(Rewards::kBosses));
        }
    }

    void Update()
    {
        std::vector<const Rewards::Reward*> rewards;
        {
            std::scoped_lock guard(state.lock);
            rewards.swap(state.waiting);
        }
        for (const auto* reward : rewards) {
            Arms::Give(reward->type, reward->rounds);
            SaHud::ShowHelp(Rewards::Text(*reward), 6.0f);
        }
    }

    void OnGameLoad()
    {
        std::scoped_lock guard(state.lock);
        state.waiting.clear();
        state.bossesPaid.clear();
    }
}
