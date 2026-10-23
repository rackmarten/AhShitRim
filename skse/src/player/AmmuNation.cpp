#include "player/AmmuNation.h"

#include "hud/SaHud.h"
#include "player/Arms.h"
#include "player/Vitals.h"

namespace AhShitRim::AmmuNation
{
    namespace
    {
        constexpr float kLeaveDistance = 400.0f;  // CJ walked (or was pushed) this far from the shopkeeper: the shop closes

        struct State
        {
            bool                 open = false;
            RE::ActorHandle      keeper;
            const Shop::Shopkeeper* who = nullptr;
            Shop::Menu           menu;
            std::string          help;
        } state;

        constexpr RE::FormID kGold = 0x0000000F;

        // The shopkeeper a_ref is, if it's one who'll sell to CJ now.
        const Shop::Shopkeeper* KeeperOf(RE::TESObjectREFR* a_ref)
        {
            auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
            auto* base = actor ? actor->GetActorBase() : nullptr;
            const auto* keeper = base ? Shop::ShopkeeperOf(base->GetFormID()) : nullptr;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!keeper || actor->IsDead() || actor->IsInCombat() || (player && actor->IsHostileToActor(player))) {
                return nullptr;
            }
            return keeper;
        }

        std::int64_t Gold(RE::PlayerCharacter* a_player) { return a_player->GetGoldAmount(); }

        void Pay(RE::PlayerCharacter* a_player, std::uint32_t a_price)
        {
            if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold); gold && a_price > 0) {
                a_player->RemoveItem(gold, static_cast<std::int32_t>(a_price), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }
        }

        void Buy(RE::PlayerCharacter* a_player, const Weapons::Loadout& a_loadout)
        {
            const auto& item = state.menu.Selected();
            const auto  offer = Shop::OfferFor(item, a_loadout, Vitals::Armor());
            const auto  result = Shop::Buy(offer, Gold(a_player));
            if (result != Shop::Result::Bought) {
                SaHud::ShowHelp(Shop::MessageOf(result, item.type), 3.0f);
                SKSE::log::info("ammu-nation: {} not bought ({}, {} gold, {} wanted)", item.name, result == Shop::Result::Full ? "full" : "too poor",
                    Gold(a_player), offer.price);
                return;
            }
            Pay(a_player, offer.price);
            SaHud::PlayHudSound(HudSounds::Sound::PurchaseWeapon);
            if (item.type == Shop::kArmour) {
                Vitals::SetArmor(Shop::kArmourMax);
            } else {
                Arms::Give(item.type, offer.rounds);
            }
            SKSE::log::info("ammu-nation: {}{} bought for {} gold ({} rounds)", item.name, offer.ammoOnly ? " ammo" : "", offer.price, offer.rounds);
        }

        void Draw(RE::PlayerCharacter* a_player, const Weapons::Loadout& a_loadout)
        {
            std::vector<SaHud::MenuRow> rows;
            const auto gold = Gold(a_player);
            if (!state.menu.InCategory()) {
                for (std::size_t i = 0; i < Shop::kCategoryCount; i++) {
                    rows.push_back({ Shop::kCategories[i].name, "", i == state.menu.Row(), true, false });
                }
                SaHud::ShowMenu("Ammu-Nation", std::move(rows));
                return;
            }
            const auto& category = Shop::kCategories[state.menu.Category()];
            for (std::size_t i = 0; i < category.count; i++) {
                const auto& item = category.items[i];
                const auto  offer = Shop::OfferFor(item, a_loadout, Vitals::Armor());
                rows.push_back({ Shop::RowName(item, offer), Shop::PriceText(offer.price), i == state.menu.Row(),
                    Shop::Buy(offer, gold) == Shop::Result::Bought, offer.ammoOnly });
            }
            SaHud::ShowMenu(category.name, std::move(rows));
        }
    }

    std::string PromptFor(RE::TESObjectREFR* a_ref)
    {
        const auto* keeper = KeeperOf(a_ref);
        return keeper ? std::format("E: Ammu-Nation ({})", keeper->name) : std::string();
    }

    bool Open(RE::TESObjectREFR* a_ref, Input::KeyLayout a_layout)
    {
        const auto* keeper = KeeperOf(a_ref);
        if (!keeper) {
            return false;
        }
        state.open = true;
        state.keeper = a_ref->As<RE::Actor>()->GetHandle();
        state.who = keeper;
        state.menu.Open();
        state.help = Shop::HelpText(a_layout);
        SKSE::log::info("ammu-nation: {} in {} opens the shop", keeper->name, keeper->town);
        return true;
    }

    bool IsOpen() { return state.open; }

    void Close()
    {
        if (!state.open) {
            return;
        }
        state.open = false;
        state.keeper = {};
        SKSE::log::info("ammu-nation: the shop closes");
    }

    void Update(RE::PlayerCharacter* a_player, const Shop::Controls& a_controls, float a_delta, const Protocol::PlayerState* a_pose)
    {
        if (!state.open) {
            return;
        }
        const auto keeper = state.keeper.get();
        if (!keeper || !KeeperOf(keeper.get()) || keeper->GetPosition().GetDistance(a_player->GetPosition()) > kLeaveDistance) {
            Close();
            return;
        }
        const Weapons::Loadout& loadout = a_pose ? a_pose->loadout : Arms::Loadout();
        switch (state.menu.Step(a_controls, a_delta)) {
        case Shop::Event::Buy: Buy(a_player, loadout); break;
        case Shop::Event::Close: Close(); return;
        default: break;
        }
        Draw(a_player, loadout);
        SaHud::ShowPrompt(state.help);  // the keys, in the help box whenever no other help shows
    }
}
