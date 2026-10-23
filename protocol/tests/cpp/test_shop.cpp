#include <doctest/doctest.h>

#include <string>

#include "../../cpp/hud_layout.h"
#include "../../cpp/shop.h"

using namespace AhShitRim;
using namespace AhShitRim::Shop;

namespace
{
    const Item& ItemOf(std::uint8_t a_type)
    {
        for (const auto& category : kCategories) {
            for (std::size_t i = 0; i < category.count; i++) {
                if (category.items[i].type == a_type) {
                    return category.items[i];
                }
            }
        }
        FAIL("not for sale");
        return kCategories[0].items[0];
    }

    Controls Move(float a_y) { return { a_y, 0, false }; }
    Controls Press(std::uint32_t a_buttons) { return { 0.0f, a_buttons, false }; }

    // A frame with the button down, then one with it up.
    Event Tap(Menu& a_menu, std::uint32_t a_buttons)
    {
        const auto event = a_menu.Step(Press(a_buttons), 0.016f);
        a_menu.Step({}, 0.016f);
        return event;
    }

    Event TapMove(Menu& a_menu, float a_y)
    {
        const auto event = a_menu.Step(Move(a_y), 0.016f);
        a_menu.Step({}, 0.016f);
        return event;
    }

    Menu Opened()
    {
        Menu menu;
        menu.Open();
        menu.Step({}, 0.016f);
        return menu;
    }
}

TEST_CASE("the blacksmiths at Warmaiden's and the hold capitals keep an Ammu-Nation")
{
    REQUIRE(ShopkeeperOf(0x00013BB9));
    CHECK(std::string(ShopkeeperOf(0x00013BB9)->name) == "Adrianne Avenicci");
    REQUIRE(ShopkeeperOf(0x00013B9F));
    CHECK(std::string(ShopkeeperOf(0x00013B9F)->town) == "Whiterun");
    CHECK(ShopkeeperOf(0x00013261));  // Beirand, Solitude
    CHECK_FALSE(ShopkeeperOf(0x00013BBB));  // Farengar sells no guns
    CHECK_FALSE(ShopkeeperOf(0));
}

TEST_CASE("everything for sale is a weapon San Andreas carries, or the armour, in its own slot")
{
    for (const auto& category : kCategories) {
        REQUIRE(category.count > 0);
        REQUIRE(category.count <= 4);
        const auto slot = SlotOf(category.items[0].type);
        for (std::size_t i = 0; i < category.count; i++) {
            const auto& item = category.items[i];
            CHECK(item.price > 0);
            if (item.type == kArmour) {
                continue;
            }
            CHECK(Weapons::Carried(item.type));
            CHECK(item.rounds > 0);
            CHECK(item.ammoRounds > 0);
            CHECK(item.ammoPrice > 0);
            CHECK(SlotOf(item.type) == slot);  // a category is a slot
        }
    }
    CHECK(SlotOf(Weapons::Pistol) == 2);
    CHECK(SlotOf(Weapons::Spas12) == 3);
    CHECK(SlotOf(Weapons::Tec9) == 4);
    CHECK(SlotOf(Weapons::M4) == 5);
    CHECK(SlotOf(Weapons::SniperRifle) == 6);
    CHECK(SlotOf(Weapons::Minigun) == 7);
    CHECK(SlotOf(Weapons::SatchelCharge) == 8);
    CHECK(SlotOf(Weapons::Camera) == 9);
    CHECK(SlotOf(Weapons::Flowers) == 10);
    CHECK(SlotOf(Weapons::Parachute) == 11);
    CHECK(SlotOf(Weapons::Detonator) == 12);
    CHECK(SlotOf(Weapons::Katana) == 1);
}

TEST_CASE("the heavy weapons are dearer than anything else, and the pistol is the cheapest gun")
{
    std::uint32_t dearestOther = 0, cheapestHeavy = UINT32_MAX;
    for (const auto& category : kCategories) {
        for (std::size_t i = 0; i < category.count; i++) {
            const auto& item = category.items[i];
            if (item.type != kArmour && SlotOf(item.type) == 7) {
                cheapestHeavy = (std::min)(cheapestHeavy, item.price);
            } else {
                dearestOther = (std::max)(dearestOther, item.price);
            }
            if (item.type != kArmour && SlotOf(item.type) >= 2 && SlotOf(item.type) <= 7) {
                CHECK(item.price >= ItemOf(Weapons::Pistol).price);
            }
        }
    }
    CHECK(cheapestHeavy > dearestOther);
}

TEST_CASE("buying a gun CJ hasn't got gives the gun and its ammo; one he has, more ammo for less")
{
    Weapons::Loadout loadout{};
    const auto&      pistol = ItemOf(Weapons::Pistol);
    auto             offer = OfferFor(pistol, loadout, 0.0f);
    CHECK_FALSE(offer.ammoOnly);
    CHECK(offer.price == pistol.price);
    CHECK(offer.rounds == pistol.rounds);
    CHECK(RowName(pistol, offer) == "Pistol");

    loadout[2] = { Weapons::Pistol, 0, 17, 40 };
    offer = OfferFor(pistol, loadout, 0.0f);
    CHECK(offer.ammoOnly);
    CHECK(offer.price == pistol.ammoPrice);
    CHECK(offer.rounds == pistol.ammoRounds);
    CHECK(offer.price < pistol.price);
    CHECK(RowName(pistol, offer) == "Pistol ammo");

    // Another pistol takes the slot over: the whole price.
    const auto& eagle = ItemOf(Weapons::DesertEagle);
    offer = OfferFor(eagle, loadout, 0.0f);
    CHECK_FALSE(offer.ammoOnly);
    CHECK(offer.price == eagle.price);

    // Thrown weapons are their own ammo.
    loadout[8] = { Weapons::Grenade, 0, 0, 3 };
    offer = OfferFor(ItemOf(Weapons::Grenade), loadout, 0.0f);
    CHECK(offer.ammoOnly);
    CHECK(RowName(ItemOf(Weapons::Grenade), offer) == "Grenades");

    // No more than San Andreas's 99,999.
    loadout[2].ammo = Weapons::kAmmoMax - 10;
    offer = OfferFor(pistol, loadout, 0.0f);
    CHECK(offer.full);
    CHECK(Buy(offer, 1000000) == Result::Full);
}

TEST_CASE("the gold has to be there, and full armour can't be bought again")
{
    Weapons::Loadout loadout{};
    const auto       offer = OfferFor(ItemOf(Weapons::Pistol), loadout, 0.0f);
    CHECK(Buy(offer, offer.price) == Result::Bought);
    CHECK(Buy(offer, offer.price - 1) == Result::TooPoor);
    CHECK(Buy(offer, -100) == Result::TooPoor);

    const auto& armour = ItemOf(kArmour);
    CHECK(Buy(OfferFor(armour, loadout, 40.0f), 10000) == Result::Bought);
    CHECK(Buy(OfferFor(armour, loadout, 100.0f), 10000) == Result::Full);
    CHECK(std::string(MessageOf(Result::Full, kArmour)) == "You already have full body armour.");
    CHECK(std::string(MessageOf(Result::TooPoor, Weapons::Pistol)) == "You don't have enough money to buy this.");
    CHECK(PriceText(150) == "$150");
}

TEST_CASE("a reward given to the loadout while San Andreas is away")
{
    Weapons::Loadout loadout{};
    GiveTo(loadout, Weapons::Pistol, 68);
    CHECK(loadout[2].type == Weapons::Pistol);
    CHECK(loadout[2].ammo == 68);
    GiveTo(loadout, Weapons::Pistol, 34);
    CHECK(loadout[2].ammo == 102);
    GiveTo(loadout, Weapons::DesertEagle, 35);  // takes the slot over
    CHECK(loadout[2].type == Weapons::DesertEagle);
    CHECK(loadout[2].ammo == 35);
    GiveTo(loadout, Weapons::DesertEagle, Weapons::kAmmoMax);
    CHECK(loadout[2].ammo == Weapons::kAmmoMax);
    GiveTo(loadout, 19, 5);  // a projectile, not a weapon
    CHECK(Weapons::Sanitise(loadout) == 0);
}

TEST_CASE("the menu: categories, then a category's items, wrapping round")
{
    auto menu = Opened();
    CHECK_FALSE(menu.InCategory());
    CHECK(menu.Rows() == kCategoryCount);
    CHECK(TapMove(menu, -1.0f) == Event::Moved);  // S: down
    CHECK(menu.Row() == 1);
    CHECK(TapMove(menu, 1.0f) == Event::Moved);   // W: up
    CHECK(TapMove(menu, 1.0f) == Event::Moved);   // round to the last
    CHECK(menu.Row() == kCategoryCount - 1);
    CHECK(Tap(menu, Protocol::kButtonLeanBack) == Event::Moved);  // the down arrow
    CHECK(menu.Row() == 0);

    CHECK(Tap(menu, Protocol::kButtonSprint) == Event::Opened);
    CHECK(menu.InCategory());
    CHECK(menu.Category() == 0);
    CHECK(menu.Rows() == kCategories[0].count);
    CHECK(menu.Selected().type == Weapons::Pistol);
    CHECK(TapMove(menu, -1.0f) == Event::Moved);
    CHECK(menu.Selected().type == Weapons::PistolSilenced);
    CHECK(Tap(menu, Protocol::kButtonSprint) == Event::Buy);
    CHECK(menu.Step({ 0.0f, 0, true }, 0.016f) == Event::Buy);  // E buys too

    CHECK(Tap(menu, Protocol::kButtonEnterExit) == Event::Back);
    CHECK_FALSE(menu.InCategory());
    CHECK(menu.Row() == 0);  // back on the category it came from
    CHECK(Tap(menu, Protocol::kButtonEnterExit) == Event::Close);
}

TEST_CASE("a held key repeats like San Andreas's menus, and keys already down when it opened wait to be let go")
{
    Menu menu;
    menu.Open();
    // F was down when the shop opened (and W): nothing until they're let go.
    CHECK(menu.Step({ 1.0f, Protocol::kButtonEnterExit, false }, 0.016f) == Event::None);
    CHECK(menu.Step({ 1.0f, Protocol::kButtonEnterExit, false }, 1.0f) == Event::None);
    CHECK(menu.Row() == 0);
    menu.Step({}, 0.016f);

    CHECK(menu.Step(Move(-1.0f), 0.016f) == Event::Moved);
    CHECK(menu.Row() == 1);
    CHECK(menu.Step(Move(-1.0f), 0.3f) == Event::None);  // 0.3 s held
    CHECK(menu.Step(Move(-1.0f), 0.15f) == Event::Moved);  // past 0.4
    CHECK(menu.Row() == 2);
    menu.Step(Move(-1.0f), 0.25f);  // 0.52 and 0.64 go by
    CHECK(menu.Row() == 4);
}

TEST_CASE("the menu's window and rows sit top left, under the help box")
{
    const auto screen = HudLayout::ScreenOf(1920.0f, 1080.0f);
    const auto small = HudLayout::MenuWindow(screen, 2);
    const auto big = HudLayout::MenuWindow(screen, 8);
    CHECK(big.y1 > small.y1);
    CHECK(small.x0 == doctest::Approx(HudLayout::kMenuX * screen.unitX));
    CHECK(small.y0 > HudLayout::Y(screen, HudLayout::kHelp.y + 60.0f, HudLayout::Edge::Start));  // below a few lines of help
    const auto row0 = HudLayout::MenuRow(0, false);
    const auto row1 = HudLayout::MenuRow(1, false);
    const auto price = HudLayout::MenuRow(1, true);
    CHECK(row1.y - row0.y == doctest::Approx(HudLayout::kMenuRowHeight));
    CHECK(price.y == row1.y);
    CHECK(price.align == HudFont::Align::Right);
    CHECK(price.x > row1.x);
    CHECK(HudLayout::X(screen, price.x, price.edgeX) < big.x1);
    CHECK(HudLayout::Y(screen, HudLayout::MenuRow(7, false).y + HudLayout::kMenuRowHeight, HudLayout::Edge::Start) <= big.y1);
    CHECK(HudLayout::MenuRowColour(true, false, false).b == HudLayout::kLightBlue.b);
    CHECK(HudLayout::MenuRowColour(false, false, true).r == HudLayout::kDarkGray.r);
    CHECK(HudLayout::MenuRowColour(false, true, true).g == HudLayout::kGreen.g);
    CHECK(HudLayout::MenuRowColour(false, true, false).b == HudLayout::kNightBlue.b);
}

TEST_CASE("the shop's help names the key layout's keys")
{
    CHECK(HelpText(Input::KeyLayout::Modern).find("Shift") != std::string::npos);
    CHECK(HelpText(Input::KeyLayout::Classic).find("Space") != std::string::npos);
}
