// Looting without menus (#830), the game-free half. While San Andreas has the player, walking over a dead
// actor, or activating a container, takes its gold and its quest items and nothing else, with no menu:
// the gold counts up in San Andreas's money display and each quest item shows in the help box. Header-only
// and free of game types, so the protocol tests cover it; the SKSE plugin feeds it what Skyrim's
// inventories hold (skse/src/player/Loot.cpp).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace AhShitRim::Loot
{
    inline constexpr std::uint32_t kGold = 0x0000000F;  // Gold001, Skyrim's septims

    // One stack in an inventory, as far as looting cares.
    struct Stack
    {
        std::uint32_t form = 0;
        std::int32_t  count = 0;
        bool          questItem = false;  // a quest alias holds it as a quest object (it can't be dropped)
        std::string   name;
    };

    // Only gold and quest items are taken; the rest stays for Skyrim's own menus (F10).
    constexpr bool Takes(std::uint32_t a_form, bool a_questItem) { return a_form == kGold || a_questItem; }

    struct Haul
    {
        std::int32_t             gold = 0;
        std::vector<std::size_t> taken;  // indices into the stacks, gold's too
        std::vector<std::string> questItems;
    };

    inline Haul Pick(const std::vector<Stack>& a_stacks)
    {
        Haul haul;
        for (std::size_t i = 0; i < a_stacks.size(); i++) {
            const auto& stack = a_stacks[i];
            if (stack.count <= 0 || !Takes(stack.form, stack.questItem)) {
                continue;
            }
            haul.taken.push_back(i);
            if (stack.form == kGold) {
                haul.gold += stack.count;
            } else {
                haul.questItems.push_back(stack.name.empty() ? std::string("a quest item") : stack.name);
            }
        }
        return haul;
    }

    // The help box for the quest items taken, "" for none: "Golden Claw taken." or "Golden Claw and
    // Arvel's Journal taken."
    inline std::string QuestItemsText(const std::vector<std::string>& a_names)
    {
        std::string text;
        for (std::size_t i = 0; i < a_names.size(); i++) {
            if (i > 0) {
                text += i + 1 == a_names.size() ? " and " : ", ";
            }
            text += a_names[i];
        }
        return text.empty() ? text : text + " taken.";
    }

    // ---- Walking over a body ----

    // CJ walks over a body when his feet come within 60 units (under a metre) of the middle of its 3D
    // across, and no more than 100 up or down (a body on a ledge above him isn't under his feet).
    inline constexpr float kWalkOverReach = 60.0f;
    inline constexpr float kWalkOverHeight = 100.0f;

    constexpr bool WalksOver(float a_feetX, float a_feetY, float a_feetZ, float a_bodyX, float a_bodyY, float a_bodyZ)
    {
        const float dx = a_bodyX - a_feetX, dy = a_bodyY - a_feetY, dz = a_bodyZ - a_feetZ;
        return dx * dx + dy * dy <= kWalkOverReach * kWalkOverReach && dz <= kWalkOverHeight && dz >= -kWalkOverHeight;
    }

    // ---- The money display ----

    // San Andreas's money counts towards what CJ has instead of jumping (CPlayerInfo::Process, the rule re3
    // reversed from the series' earlier game): each of its frames (30 a second) by 12345 while more than 100,000 apart, 1234 over 10,000,
    // 123 over 1,000, 42 over 50, else 1. The first value it sees, and any while it isn't shown, it takes as is.
    class Counter
    {
    public:
        static constexpr float kFrame = 1.0f / 30.0f;

        std::int64_t Shown() const { return _shown; }

        // a_shown: the HUD shows the money this frame.
        std::int64_t Tick(std::int64_t a_money, float a_delta, bool a_shown)
        {
            if (!_seeded || !a_shown) {
                _seeded = true;
                _shown = a_money;
                _time = 0.0f;
                return _shown;
            }
            _time = std::isfinite(a_delta) && a_delta > 0.0f ? (std::min)(_time + a_delta, 1.0f) : _time;
            while (_time >= kFrame && _shown != a_money) {
                _time -= kFrame;
                const std::int64_t diff = a_money - _shown;
                const std::int64_t apart = diff < 0 ? -diff : diff;
                const std::int64_t step = apart > 100000 ? 12345 : apart > 10000 ? 1234 : apart > 1000 ? 123 : apart > 50 ? 42 : 1;
                _shown += diff < 0 ? -step : step;
            }
            if (_shown == a_money) {
                _time = 0.0f;
            }
            return _shown;
        }

    private:
        bool         _seeded = false;
        std::int64_t _shown = 0;
        float        _time = 0.0f;
    };
}
