#include "player/Chatter.h"

#include "player/AmmuNation.h"
#include "pose.h"
#include "situations.h"

namespace AhShitRim::Chatter
{
    namespace
    {
        constexpr float kReadEvery = 0.25f;  // s between reads of the gold and the hostiles near

        struct State
        {
            int               stealthKills = 0;  // since the last Update
            Situations::Purse purse;
            bool              hostileNear = false;
            float             untilRead = 0.0f;
        } state;

        bool ShopOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return AmmuNation::IsOpen() || (ui && ui->IsMenuOpen(RE::BarterMenu::MENU_NAME));
        }

        // A living actor in combat with the player within kChaseMeters.
        bool HostileNear(RE::PlayerCharacter* a_player)
        {
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!lists) {
                return false;
            }
            const auto  feet = a_player->GetPosition();
            const float range = Situations::kChaseMeters / Pose::kMetersPerUnit;
            for (auto& handle : lists->highActorHandles) {
                const auto actor = handle.get();
                if (actor && !actor->IsDead() && actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player &&
                    actor->GetPosition().GetDistance(feet) <= range) {
                    return true;
                }
            }
            return false;
        }
    }

    void StealthKill() { state.stealthKills++; }

    void Seed()
    {
        state.purse.Seed();
        state.hostileNear = false;
        state.untilRead = 0.0f;
    }

    void Update(RE::PlayerCharacter* a_player, Input::Builder& a_input, float a_delta)
    {
        for (; state.stealthKills > 0; state.stealthKills--) {
            a_input.AddStealthKill();
            SKSE::log::info("chatter: a stealth kill");
        }
        state.untilRead -= a_delta;
        if (state.untilRead > 0.0f) {
            return;
        }
        state.untilRead = kReadEvery;
        const auto gold = a_player->GetGoldAmount();
        const auto events = state.purse.Update(gold, ShopOpen());
        if (events.cash) {
            a_input.AddCash();
            SKSE::log::info("chatter: gold looted ({} now)", gold);
        }
        if (events.buy) {
            a_input.AddShopBuy();
            SKSE::log::info("chatter: bought in a shop ({} gold left)", gold);
        }
        if (events.leave) {
            a_input.AddShopLeave();
            SKSE::log::info("chatter: left the shop after buying");
        }
        const bool hostile = HostileNear(a_player);
        a_input.SetHostileNear(hostile);
        if (hostile != state.hostileNear) {
            state.hostileNear = hostile;
            SKSE::log::info("chatter: {}", hostile ? "a hostile is within 20 m" : "no hostile within 20 m");
        }
    }
}
