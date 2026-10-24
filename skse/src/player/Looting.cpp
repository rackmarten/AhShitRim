#include "player/Looting.h"

#include "hud/SaHud.h"
#include "loot.h"
#include "menus.h"

#include <atomic>
#include <thread>

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

        // ---- Skyrim's menus (UI tasks, on Skyrim's UI processing) ----

        // A UI task must never queue another from inside itself: SKSE runs the UI task queue until it is empty,
        // so a task that queues itself spins there for good and Skyrim hangs (it did, closing a barter). A pacer
        // thread queues one tick at a time instead, every kTickEvery, while there is something to watch.
        constexpr auto kTickEvery = std::chrono::milliseconds(50);

        constexpr std::string_view kClosed[] = { RE::InventoryMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME };
        constexpr std::size_t      kBarter = 2;

        std::atomic<bool> handedOver = false;

        struct MenuState
        {
            Menus::Closer                         closers[std::size(kClosed)];
            Menus::DialogueWatch                  watch;
            std::chrono::steady_clock::time_point last;
        } menus;  // UI tasks only

        std::atomic<bool> ticking = false;  // the pacer thread runs
        std::atomic<bool> posted = false;   // a tick is queued and hasn't run yet

        bool Talking()
        {
            auto* topics = RE::MenuTopicManager::GetSingleton();
            return topics && topics->menuOpen && topics->speaker.get();
        }

        void Send(std::string_view a_menu, RE::UI_MESSAGE_TYPE a_type)
        {
            if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(a_menu, a_type, nullptr);
            }
        }

        // Once a UI frame while one of the menus is open or a dialogue is watched: closes the menus once they
        // have settled, and shows a dialogue's menu again if it was left without it.
        void Tick()
        {
            posted = false;
            auto*      ui = RE::UI::GetSingleton();
            const auto now = std::chrono::steady_clock::now();
            const float seconds = (std::min)(std::chrono::duration<float>(now - menus.last).count(), 0.25f);
            menus.last = now;
            if (!ui) {
                ticking = false;
                return;
            }
            const bool dialogueMenu = ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
            bool       anyOpen = false;
            for (std::size_t i = 0; i < std::size(kClosed); i++) {
                const bool open = handedOver && ui->IsMenuOpen(kClosed[i]);
                anyOpen = anyOpen || open;
                if (menus.closers[i].Step(open, i == kBarter && dialogueMenu, seconds)) {
                    Send(kClosed[i], RE::UI_MESSAGE_TYPE::kHide);
                    SKSE::log::info("looting: Skyrim's {} closed while San Andreas has the player", kClosed[i]);
                    if (i == kBarter) {
                        menus.watch.Arm();
                    }
                }
            }
            const bool barter = ui->IsMenuOpen(RE::BarterMenu::MENU_NAME);
            if (menus.watch.Step(Talking(), dialogueMenu, barter, seconds)) {
                Send(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                SKSE::log::warn("looting: the dialogue was left without its menu after a barter, shown again");
            }
            if (!anyOpen && !menus.watch.Armed()) {
                ticking = false;  // nothing left to watch: the pacer stops
            }
        }

        void StartTicking()
        {
            if (ticking.exchange(true)) {
                return;
            }
            std::thread([] {
                while (ticking) {
                    std::this_thread::sleep_for(kTickEvery);
                    if (ticking && !posted.exchange(true)) {
                        SKSE::GetTaskInterface()->AddUITask(Tick);
                    }
                }
            }).detach();
        }

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
        // Not from here: closing a barter from its own opening event raced Skyrim taking the dialogue menu
        // down for it, and now and then the dialogue never got its menu back.
        StartTicking();
        SaHud::ShowHelp("F10 hands the player back to Skyrim for its menus.", 3.0f);
    }

    void SetHandedOver(bool a_handedOver)
    {
        handedOver = a_handedOver;
        if (a_handedOver) {
            return;
        }
        // F10 always gets the player out: a dialogue stuck without its menu gets it back.
        SKSE::GetTaskInterface()->AddUITask([] {
            auto* ui = RE::UI::GetSingleton();
            if (ui && Menus::Orphaned(Talking(), ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME), ui->IsMenuOpen(RE::BarterMenu::MENU_NAME))) {
                Send(RE::DialogueMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow);
                SKSE::log::warn("looting: the handover ended in a dialogue without its menu, shown again");
            }
        });
    }
}
