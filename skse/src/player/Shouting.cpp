#include "player/Shouting.h"

#include "thuum.h"

#include <cmath>
#include <vector>

namespace AhShitRim::Shouting
{
    using namespace Thuum;

    namespace
    {
        // A condition put first on one of the voice topics' lines: "the speaker is not the player". The
        // lines' own conditions follow it unchanged (AND binds looser than their ORs), so everyone else
        // still says theirs. Allocated once on Skyrim's heap and never freed: the line may still hold it.
        struct Muted
        {
            RE::TESTopicInfo*     info = nullptr;
            RE::TESConditionItem* notPlayer = nullptr;
        };

        // Main thread only.
        struct State
        {
            std::vector<Muted>        muted;
            bool                      built = false;
            bool                      silent = false;
            RE::TESShout*             shout = nullptr;  // the shout charged lately, and how far
            RE::TESShout::VariationID variation = RE::TESShout::VariationID::kNone;
        } state;

        void Build()
        {
            state.built = true;
            auto* player = RE::TESForm::LookupByID(0x7);  // the player's base actor
            for (const auto id : kVoiceTopics) {
                auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(id);
                if (!topic || !player) {
                    SKSE::log::warn("thuum: no voice topic {:06X}, the Dragonborn may still be heard", id);
                    continue;
                }
                for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
                    auto* info = topic->topicInfos[i];
                    if (!info) {
                        continue;
                    }
                    auto* item = new RE::TESConditionItem();
                    item->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetIsID;
                    item->data.functionData.params[0] = player;
                    item->data.comparisonValue.f = 0.0f;
                    item->data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
                    item->data.flags.isOR = false;
                    item->data.object = RE::CONDITIONITEMOBJECT::kSelf;
                    state.muted.push_back({ info, item });
                }
            }
            SKSE::log::info("thuum: {} lines of the Dragonborn's voice found", state.muted.size());
        }

        void Silence(bool a_silent)
        {
            if (a_silent == state.silent) {
                return;
            }
            if (!state.built) {
                Build();
            }
            for (auto& m : state.muted) {
                auto& head = m.info->objConditions.head;
                if (a_silent) {
                    m.notPlayer->next = head;
                    head = m.notPlayer;
                } else if (head == m.notPlayer) {
                    head = m.notPlayer->next;
                } else {
                    // Something else put a condition in front since: take ours out of the chain.
                    for (auto* item = head; item; item = item->next) {
                        if (item->next == m.notPlayer) {
                            item->next = m.notPlayer->next;
                            break;
                        }
                    }
                }
            }
            state.silent = a_silent;
            SKSE::log::info("thuum: the Dragonborn's voice {}", a_silent ? "muted, CJ shouts" : "back");
        }

        // The words of a shout released with no variation seen: the one whose cooldown is nearest.
        int WordsByCooldown(const RE::TESShout* a_shout, float a_recovery)
        {
            int   best = 1;
            float nearest = 1e9f;
            for (int i = 0; i < kMaxWords; ++i) {
                const float d = std::fabs(a_shout->variations[i].recoveryTime - a_recovery);
                if (d < nearest) {
                    nearest = d;
                    best = i + 1;
                }
            }
            return best;
        }
    }

    void Update(RE::PlayerCharacter* a_player, bool a_cjHasPlayer)
    {
        Silence(a_cjHasPlayer);
        auto* process = a_player ? a_player->GetActorRuntimeData().currentProcess : nullptr;
        auto* high = process ? process->high : nullptr;
        if (high && high->currentShout) {
            state.shout = high->currentShout;
            if (high->currentShoutVariation != RE::TESShout::VariationID::kNone) {
                state.variation = high->currentShoutVariation;
            }
        }
    }

    std::uint8_t Released(RE::PlayerCharacter* a_player, float a_recovery)
    {
        Update(a_player, state.silent);  // this frame's state, in case the release came first
        auto* shout = state.shout;
        if (auto* power = a_player ? a_player->GetActorRuntimeData().selectedPower : nullptr; !shout && power) {
            shout = power->As<RE::TESShout>();
        }
        int words = 1;
        if (state.variation != RE::TESShout::VariationID::kNone) {
            words = static_cast<int>(state.variation) + 1;
        } else if (shout) {
            words = WordsByCooldown(shout, a_recovery);
        }
        const auto* file = shout ? shout->GetFile(0) : nullptr;
        const int   index = file ? Find(file->GetFilename(), shout->GetLocalFormID()) : -1;
        SKSE::log::info("thuum: {} with {} word{} ({}){}", shout ? shout->GetName() : "a shout", words, words == 1 ? "" : "s",
            index >= 0 ? kShouts[index].key : "not in the table: CJ's generic lines",
            state.variation == RE::TESShout::VariationID::kNone ? ", words by the cooldown" : "");
        state.shout = nullptr;
        state.variation = RE::TESShout::VariationID::kNone;
        return Encode(index, words);
    }
}
