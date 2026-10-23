#include "player/Looting.h"

#include "hud/SaHud.h"
#include "loot.h"

namespace AhShitRim::Looting
{
    namespace
    {
        constexpr float kScanEvery = 0.2f;     // seconds between looks for a body under CJ's feet
        constexpr float kScanRadius = 200.0f;  // references this close to the player are looked at

        struct State
        {
            float scan = 0.0f;
        } state;

        // Takes a_ref's gold and quest items into the player's inventory. a_steal: from an owned container, as
        // Skyrim's container menu would. Returns whether anything was taken.
        bool Take(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref, bool a_steal)
        {
            auto                              inventory = a_ref->GetInventory();
            std::vector<Loot::Stack>          stacks;
            std::vector<RE::TESBoundObject*>  objects;
            std::vector<RE::InventoryEntryData*> entries;
            for (auto& [object, data] : inventory) {
                if (!object) {
                    continue;
                }
                const char* name = object->GetName();
                stacks.push_back({ object->GetFormID(), data.first, data.second && data.second->IsQuestObject(), name ? name : "" });
                objects.push_back(object);
                entries.push_back(data.second.get());
            }
            const auto haul = Loot::Pick(stacks);
            if (haul.taken.empty()) {
                return false;
            }
            const auto reason = a_steal ? RE::ITEM_REMOVE_REASON::kSteal : RE::ITEM_REMOVE_REASON::kStoreInContainer;
            for (const auto i : haul.taken) {
                auto*        object = objects[i];
                std::int32_t left = stacks[i].count;
                // Each extra list on its own, so a quest item keeps its alias (the Golden Claw stays the quest's).
                if (auto* entry = entries[i]; entry && entry->extraLists) {
                    std::vector<RE::ExtraDataList*> lists;  // a copy: removing changes the list
                    for (auto* list : *entry->extraLists) {
                        lists.push_back(list);
                    }
                    for (auto* list : lists) {
                        const std::int32_t count = list ? (std::min)(list->GetCount(), left) : 0;
                        if (count > 0) {
                            a_ref->RemoveItem(object, count, reason, list, a_player);
                            left -= count;
                        }
                    }
                }
                if (left > 0) {
                    a_ref->RemoveItem(object, left, reason, nullptr, a_player);
                }
            }
            if (a_steal && haul.gold > 0) {
                if (auto* gold = RE::TESForm::LookupByID(Loot::kGold)) {
                    a_player->StealAlarm(a_ref, gold, haul.gold, haul.gold, a_ref->GetOwner(), true);
                }
            }
            if (const auto text = Loot::QuestItemsText(haul.questItems); !text.empty()) {
                SaHud::ShowHelp(text, 4.0f);
            }
            SKSE::log::info("looting: {} gold and {} quest items taken from {} ({:08X}){}", haul.gold, haul.questItems.size(), a_ref->GetDisplayFullName(),
                a_ref->GetFormID(), a_steal ? ", stolen" : "");
            return true;
        }

        bool IsContainer(RE::TESObjectREFR* a_ref)
        {
            auto* base = a_ref->GetBaseObject();
            return base && base->GetFormType() == RE::FormType::Container;
        }

        bool IsBody(RE::TESObjectREFR* a_ref)
        {
            auto* actor = a_ref->As<RE::Actor>();
            return actor && actor->IsDead();
        }
    }

    void Update(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_feet, bool a_onFoot, float a_delta)
    {
        state.scan += a_delta;
        if (!a_onFoot || state.scan < kScanEvery) {
            return;
        }
        state.scan = 0.0f;
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return;
        }
        std::vector<RE::TESObjectREFR*> bodies;
        tes->ForEachReferenceInRange(a_player, kScanRadius, [&](RE::TESObjectREFR* a_ref) {
            if (a_ref && a_ref != a_player && !a_ref->IsDisabled() && !a_ref->IsDeleted() && IsBody(a_ref)) {
                if (auto* root = a_ref->Get3D()) {
                    const auto& at = root->worldBound.center;
                    if (Loot::WalksOver(a_feet.x, a_feet.y, a_feet.z, at.x, at.y, at.z)) {
                        bodies.push_back(a_ref);
                    }
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        for (auto* body : bodies) {
            Take(a_player, body, false);
        }
    }

    bool Activate(RE::PlayerCharacter* a_player, RE::TESObjectREFR* a_ref)
    {
        const bool body = IsBody(a_ref);
        if (!body && (!IsContainer(a_ref) || a_ref->IsLocked())) {
            return false;  // a locked container goes to Skyrim's lock (and its key or lockpicks)
        }
        if (!Take(a_player, a_ref, !body && a_ref->IsCrimeToActivate())) {
            SaHud::ShowHelp("Nothing to take here. F10 to search it in Skyrim's menu.", 3.0f);
        }
        return true;
    }

    void OnMenuOpening(std::string_view a_menu)
    {
        if (a_menu != RE::InventoryMenu::MENU_NAME && a_menu != RE::ContainerMenu::MENU_NAME && a_menu != RE::BarterMenu::MENU_NAME) {
            return;
        }
        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(a_menu, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
        SaHud::ShowHelp("F10 hands the player back to Skyrim for its menus.", 3.0f);
        SKSE::log::info("looting: Skyrim's {} closed while San Andreas has the player", a_menu);
    }
}
