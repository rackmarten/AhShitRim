// Ammu-Nation (#830), the game-free half: what the blacksmiths of the hold capitals sell CJ while San
// Andreas has the player, for Skyrim's gold. Weapons by San Andreas's slot, their ammo and body armour,
// everything for sale from the start, the heavy weapons dear. The shop's menu (San Andreas's standard
// menu: a list of categories, then the category's items with their prices) is driven by CJ's actions.
// Header-only and free of game types, so the protocol tests cover it; the SKSE plugin draws and runs it
// (skse/src/player/Shop.cpp).

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "../ahshitrim_protocol.h"
#include "input.h"
#include "weapons.h"

namespace AhShitRim::Shop
{
    // ---- Who sells ----

    // The blacksmiths who keep an Ammu-Nation under the counter: Warmaiden's in Whiterun, and the main
    // smith of each other hold capital that has one (Winterhold has none, Morthal's Jorgen sells nothing).
    // Their base NPCs in Skyrim.esm.
    struct Shopkeeper
    {
        std::uint32_t npc;
        const char*   name;
        const char*   town;
    };

    inline constexpr Shopkeeper kShopkeepers[] = {
        { 0x00013BB9, "Adrianne Avenicci", "Whiterun" },
        { 0x00013B9F, "Ulfberth War-Bear", "Whiterun" },
        { 0x00013261, "Beirand", "Solitude" },
        { 0x00014142, "Oengul War-Anvil", "Windhelm" },
        { 0x0001334C, "Balimund", "Riften" },
        { 0x0001339A, "Ghorza gra-Bagol", "Markarth" },
        { 0x0001361E, "Rustleif", "Dawnstar" },
        { 0x00013650, "Lod", "Falkreath" },
    };

    constexpr const Shopkeeper* ShopkeeperOf(std::uint32_t a_npc)
    {
        for (const auto& keeper : kShopkeepers) {
            if (keeper.npc == a_npc) {
                return &keeper;
            }
        }
        return nullptr;
    }

    // ---- What it sells ----

    inline constexpr std::uint8_t kArmour = 0xFF;  // body armour, in place of a weapon type
    inline constexpr float        kArmourMax = 100.0f;  // San Andreas's points, a full bar

    struct Item
    {
        std::uint8_t  type;       // San Andreas's weapon type, or kArmour
        const char*   name;
        std::uint32_t price;      // gold for the weapon (or the armour)
        std::uint32_t rounds;     // the ammo that comes with it
        std::uint32_t ammoPrice;  // gold for more ammo once CJ has it
        std::uint32_t ammoRounds;
    };

    struct Category
    {
        const char* name;
        Item        items[4];
        std::size_t count;
    };

    // By San Andreas's slot, as its Ammu-Nation sorts them, and dearer the heavier: a bandit carries a few
    // dozen gold, so the pistol is a first buy and the minigun a fortune.
    inline constexpr Category kCategories[] = {
        { "Pistols",
            { { Weapons::Pistol, "Pistol", 150, 68, 30, 34 },
                { Weapons::PistolSilenced, "Silenced pistol", 300, 51, 40, 34 },
                { Weapons::DesertEagle, "Desert Eagle", 750, 35, 70, 21 } },
            3 },
        { "Shotguns",
            { { Weapons::Shotgun, "Shotgun", 400, 25, 50, 15 },
                { Weapons::Sawnoff, "Sawn-off shotgun", 600, 24, 60, 12 },
                { Weapons::Spas12, "Combat shotgun", 1000, 28, 100, 14 } },
            3 },
        { "Sub-machine guns",
            { { Weapons::Tec9, "Tec-9", 300, 150, 50, 50 },
                { Weapons::MicroUzi, "Micro SMG", 450, 150, 60, 50 },
                { Weapons::Mp5, "SMG", 1000, 180, 90, 60 } },
            3 },
        { "Assault rifles",
            { { Weapons::Ak47, "AK-47", 1500, 180, 120, 60 },
                { Weapons::M4, "M4", 2000, 200, 150, 100 } },
            2 },
        { "Rifles",
            { { Weapons::CountryRifle, "Country rifle", 500, 20, 50, 10 },
                { Weapons::SniperRifle, "Sniper rifle", 2500, 20, 200, 10 } },
            2 },
        { "Heavy weapons",
            { { Weapons::RocketLauncher, "Rocket launcher", 6000, 5, 1000, 2 },
                { Weapons::RocketLauncherHs, "Heat seeker", 8000, 5, 1500, 2 },
                { Weapons::Flamethrower, "Flamethrower", 5000, 500, 800, 500 },
                { Weapons::Minigun, "Minigun", 15000, 500, 3000, 500 } },
            4 },
        { "Thrown",
            { { Weapons::Grenade, "Grenades", 300, 5, 300, 5 },
                { Weapons::TearGas, "Tear gas", 150, 5, 150, 5 },
                { Weapons::Molotov, "Molotov cocktails", 200, 5, 200, 5 },
                { Weapons::SatchelCharge, "Satchel charges", 1000, 4, 1000, 4 } },
            4 },
        { "Body armour", { { kArmour, "Body armour", 200, 0, 200, 0 } }, 1 },
    };
    inline constexpr std::size_t kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

    // San Andreas's weapon slot for a type (weapon.dat's), the one a new weapon of it takes over.
    constexpr std::uint32_t SlotOf(std::uint8_t a_type)
    {
        if (a_type <= Weapons::BrassKnuckle) return 0;
        if (a_type <= Weapons::Chainsaw) return 1;
        if (a_type <= Weapons::Cane) return 10;
        if (a_type <= Weapons::Molotov || a_type == Weapons::SatchelCharge) return 8;
        if (a_type <= Weapons::DesertEagle) return 2;
        if (a_type <= Weapons::Spas12) return 3;
        if (a_type == Weapons::MicroUzi || a_type == Weapons::Mp5 || a_type == Weapons::Tec9) return 4;
        if (a_type == Weapons::Ak47 || a_type == Weapons::M4) return 5;
        if (a_type == Weapons::CountryRifle || a_type == Weapons::SniperRifle) return 6;
        if (a_type <= Weapons::Minigun) return 7;
        if (a_type == Weapons::Detonator) return 12;
        if (a_type <= Weapons::Camera) return 9;
        return 11;
    }

    // A weapon given to a loadout San Andreas doesn't have yet (a reward while the link is down): the same
    // type gets the rounds on top, another takes over its slot, as San Andreas's CPed::GiveWeapon does.
    inline void GiveTo(Weapons::Loadout& a_loadout, std::uint8_t a_type, std::uint32_t a_rounds)
    {
        if (!Weapons::Carried(a_type)) {
            return;
        }
        auto& entry = a_loadout[SlotOf(a_type)];
        if (entry.type == a_type) {
            entry.ammo = (std::min)(entry.ammo + a_rounds, Weapons::kAmmoMax);
        } else {
            entry = {};
            entry.type = a_type;
            entry.ammo = (std::min)((std::max)(a_rounds, 1u), Weapons::kAmmoMax);
        }
    }

    // ---- Buying ----

    enum class Result : std::uint8_t
    {
        Bought,
        TooPoor,  // not enough gold
        Full,     // full armour already, or the ammo can't take another lot
    };

    // What buying an item does now: the weapon with its ammo, or only ammo for the one CJ has.
    struct Offer
    {
        std::uint32_t price = 0;
        std::uint32_t rounds = 0;
        bool          ammoOnly = false;
        bool          full = false;
    };

    inline Offer OfferFor(const Item& a_item, const Weapons::Loadout& a_loadout, float a_armour)
    {
        Offer offer;
        if (a_item.type == kArmour) {
            offer.price = a_item.price;
            offer.full = a_armour >= kArmourMax;
            return offer;
        }
        const auto& held = a_loadout[SlotOf(a_item.type)];
        offer.ammoOnly = held.type == a_item.type;
        offer.price = offer.ammoOnly ? a_item.ammoPrice : a_item.price;
        offer.rounds = offer.ammoOnly ? a_item.ammoRounds : a_item.rounds;
        offer.full = offer.ammoOnly && held.ammo + offer.rounds > Weapons::kAmmoMax;
        return offer;
    }

    inline Result Buy(const Offer& a_offer, std::int64_t a_gold)
    {
        if (a_offer.full) {
            return Result::Full;
        }
        return a_gold >= static_cast<std::int64_t>(a_offer.price) ? Result::Bought : Result::TooPoor;
    }

    // An item's row: its name, or "<name> ammo" once CJ has it (a thrown weapon is its own ammo).
    inline std::string RowName(const Item& a_item, const Offer& a_offer)
    {
        if (!a_offer.ammoOnly || a_item.type == kArmour || a_item.type == Weapons::Grenade || a_item.type == Weapons::TearGas ||
            a_item.type == Weapons::Molotov || a_item.type == Weapons::SatchelCharge) {
            return a_item.name;
        }
        return std::string(a_item.name) + " ammo";
    }

    inline std::string PriceText(std::uint32_t a_price) { return "$" + std::to_string(a_price); }

    // San Andreas's help box line after a purchase that didn't go through.
    constexpr const char* MessageOf(Result a_result, std::uint8_t a_type)
    {
        switch (a_result) {
        case Result::TooPoor: return "You don't have enough money to buy this.";
        case Result::Full: return a_type == kArmour ? "You already have full body armour." : "You can't carry any more of this.";
        default: return "";
        }
    }

    // The help box while the shop is open, with the keys of the key layout.
    inline std::string HelpText(Input::KeyLayout a_layout)
    {
        const char* buy = a_layout == Input::KeyLayout::Classic ? "Space" : "Shift";
        return std::string("W/S to browse, E or ") + buy + " to choose, F to go back.";
    }

    // ---- The menu ----

    // CJ's actions this frame, as the input builds them (W/S and the arrows, sprint, enter/exit), plus E.
    struct Controls
    {
        float         moveY = 0.0f;
        std::uint32_t buttons = 0;
        bool          activate = false;  // E, pressed this frame
    };

    enum class Event : std::uint8_t
    {
        None,
        Moved,
        Opened,  // into a category
        Buy,     // the selected item, in category Category() row Row()
        Back,    // out of a category
        Close,   // out of the shop
    };

    // San Andreas's standard menu, two deep: the categories, then a category's items. Up and down move the
    // highlight (held, they repeat after 0.4 s every 0.12 s, as San Andreas's menus do) and wrap round, the
    // sprint key or E chooses, the enter/exit key goes back and, from the categories, leaves.
    class Menu
    {
    public:
        static constexpr float kRepeatDelay = 0.4f;
        static constexpr float kRepeatEvery = 0.12f;

        void Open()
        {
            _category = -1;
            _row = 0;
            _seeded = false;
            _stale = false;
            _held = 0;
        }

        bool        InCategory() const { return _category >= 0; }
        std::size_t Category() const { return InCategory() ? static_cast<std::size_t>(_category) : 0; }
        std::size_t Row() const { return _row; }
        std::size_t Rows() const { return InCategory() ? kCategories[_category].count : kCategoryCount; }
        const Item& Selected() const { return kCategories[Category()].items[_row]; }

        Event Step(const Controls& a_controls, float a_delta)
        {
            const int  dir = a_controls.moveY > 0.5f || (a_controls.buttons & Protocol::kButtonLeanForward) ? -1
                             : a_controls.moveY < -0.5f || (a_controls.buttons & Protocol::kButtonLeanBack) ? 1
                                                                                                             : 0;
            const bool choose = (a_controls.buttons & Protocol::kButtonSprint) != 0;
            const bool back = (a_controls.buttons & Protocol::kButtonEnterExit) != 0;
            // Keys already down when the shop opened (E's own press, a held W) count once let go.
            const bool seeded = _seeded;
            const bool chooseEdge = seeded && choose && !_choose;
            const bool backEdge = seeded && back && !_back;
            _choose = choose;
            _back = back;
            _seeded = true;

            int moves = 0;
            if (!seeded && dir != 0) {
                _stale = true;  // held since before the shop opened: nothing until let go
            }
            if (dir == 0 || _stale) {
                _stale = _stale && dir != 0;
                _held = 0;
                _heldFor = 0.0f;
            } else if (dir != _held) {
                _held = dir;
                _heldFor = 0.0f;
                moves = 1;
            } else {
                const float before = _heldFor;
                _heldFor += a_delta;
                for (float at = kRepeatDelay; at <= _heldFor; at += kRepeatEvery) {
                    if (at > before) {
                        moves++;
                    }
                }
            }

            if (backEdge) {
                if (InCategory()) {
                    _row = static_cast<std::size_t>(_category);
                    _category = -1;
                    return Event::Back;
                }
                return Event::Close;
            }
            if (chooseEdge || a_controls.activate) {
                if (!InCategory()) {
                    _category = static_cast<int>(_row);
                    _row = 0;
                    return Event::Opened;
                }
                return Event::Buy;
            }
            if (moves > 0) {
                const std::size_t rows = Rows();
                for (int i = 0; i < moves; i++) {
                    _row = dir > 0 ? (_row + 1) % rows : (_row + rows - 1) % rows;
                }
                return Event::Moved;
            }
            return Event::None;
        }

    private:
        int         _category = -1;
        std::size_t _row = 0;
        bool        _seeded = false;
        bool        _choose = false;
        bool        _back = false;
        bool        _stale = false;
        int         _held = 0;
        float       _heldFor = 0.0f;
    };
}
