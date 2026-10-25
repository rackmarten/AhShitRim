// CJ talking on his own (v29), the game-free half: the situations he speaks in the way he does in San
// Andreas (a blow of his lands, a burst of his hits someone, he pulls a gun, a sneak kill, singing when
// nothing goes on, looting gold, buying in a shop and leaving it, running from a fight, crashing his
// car), and the rules that keep him from chattering: a trigger only speaks 4 s after any line of his,
// each has its own chance and, some, a wait before it speaks again, and he never talks over himself.
// The insult and swear keys aren't triggers: they always cut in.
//
// Each trigger plays its lines in voice_lines.h (the picker's situation columns). An empty table means
// San Andreas's own pick for the trigger's speech context in CJ's mood; the three contexts San Andreas
// has no pick of CJ's for (a stealth kill, buying, leaving a shop) hold his calm lines (CR, CR2) of that
// context until some are picked (apply.mjs). Header-only and free of game types, so the protocol tests
// cover it; Skyrim's half is skse/src/player/Chatter.cpp, San Andreas's sa/src/scenes.cpp.
//
// The picker (scripts/voice-picker) gets its situation columns from kTriggers: keep each entry on one line.

#pragma once

#include "voice_lines.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace AhShitRim::Situations
{
    enum class Trigger : std::uint8_t
    {
        Fight,        // a melee blow of his lands on a Skyrim actor (the Hit commands, v13)
        Shoot,        // the first hit of a burst of his gunfire on an actor
        PullGun,      // he aims a gun on foot
        StealthKill,  // he kills someone while sneaking, unseen (InputState.stealthKillCount)
        Singing,      // in a vehicle with the radio off, or idle on foot, with nothing said for 90 s
        Cash,         // gold added to the player outside a shop (InputState.cashCount)
        ShopBuy,      // gold spent in Ammu-Nation, or a barter menu (CJ doesn't get one) (InputState.shopBuyCount)
        ShopLeave,    // the shop closed after buying (InputState.shopLeaveCount)
        Chased,       // sprinting with a hostile in combat with him within 20 m (kInputHostileNear)
        Crash,        // his vehicle loses health to a crash; San Andreas only has other drivers say it
    };

    struct Info
    {
        std::string_view key;       // the picker's column ("situation.<key>") and the name of its lines
        std::string_view name;
        std::uint16_t    context;   // the CTX_GLOBAL_* speech context CJ says it in
        float            chance;    // of speaking when it fires
        float            repeat;    // s before this trigger speaks again
        std::string_view defaults;  // CJ's own speech context whose calm lines fill an empty table; "" = San Andreas picks
    };

    // gta-reversed's eGlobalSpeechContext: FIGHT 89, SHOOT 182, PULL_GUN 176, PICKUP_CASH 172, CHASED 44,
    // CRASH_CAR 67, SINGING 336 (whose 30 s repeat SayNow clears). No global context reaches CJ's
    // STEALTH_KILL, SHOP_BUY or SHOP_LEAVE lines (SHOP_BUY and SHOP_LEAVE are other peds'), so the insult's
    // (GENERIC_INSULT_MALE, 110) carries them, as it carries his answers in conversations.
    // clang-format off
    inline constexpr std::array kTriggers{
        Info{ "fight", "Fight: a blow lands", 89, 1.0f / 3.0f, 0.0f, "" },
        Info{ "shoot", "Shoot: a burst hits", 182, 0.5f, 0.0f, "" },
        Info{ "pull-gun", "Pull gun: aiming on foot", 176, 0.5f, 20.0f, "" },
        Info{ "stealth-kill", "Stealth kill", 110, 1.0f, 0.0f, "STEALTH_KILL" },
        Info{ "singing", "Singing", 336, 1.0f, 0.0f, "" },
        Info{ "cash", "Looting gold", 172, 0.5f, 0.0f, "" },
        Info{ "shop-buy", "Shop: buying", 110, 0.5f, 0.0f, "SHOP_BUY" },
        Info{ "shop-leave", "Shop: leaving after buying", 110, 1.0f, 0.0f, "SHOP_LEAVE" },
        Info{ "chased", "Chased: running from a fight", 44, 0.5f, 20.0f, "" },
        Info{ "crash", "Crash: the vehicle hits hard", 67, 0.5f, 0.0f, "" },
    };
    // clang-format on
    inline constexpr std::size_t kTriggerCount = kTriggers.size();
    static_assert(kTriggerCount == static_cast<std::size_t>(Trigger::Crash) + 1);

    constexpr const Info& InfoOf(Trigger a_trigger) { return kTriggers[static_cast<std::size_t>(a_trigger)]; }

    // The lines picked for a trigger (voice_lines.h's situation picks); empty: San Andreas picks.
    constexpr std::span<const VoiceLines::Line> LinesOf(Trigger a_trigger)
    {
        switch (a_trigger) {
        case Trigger::Fight: return VoiceLines::kFightLines;
        case Trigger::Shoot: return VoiceLines::kShootLines;
        case Trigger::PullGun: return VoiceLines::kPullGunLines;
        case Trigger::StealthKill: return VoiceLines::kStealthKillLines;
        case Trigger::Singing: return VoiceLines::kSingingLines;
        case Trigger::Cash: return VoiceLines::kCashLines;
        case Trigger::ShopBuy: return VoiceLines::kShopBuyLines;
        case Trigger::ShopLeave: return VoiceLines::kShopLeaveLines;
        case Trigger::Chased: return VoiceLines::kChasedLines;
        case Trigger::Crash: return VoiceLines::kCrashLines;
        }
        return {};
    }

    inline constexpr float kGap = 4.0f;            // s after any line of his before a trigger speaks
    inline constexpr float kQuietToSing = 90.0f;   // s of nothing said, and settled, before he sings
    inline constexpr float kBurstGap = 1.5f;       // s between hits that ends a burst of gunfire
    inline constexpr float kCrashHealthLoss = 5.0f;  // vehicle health lost at once that counts as a crash (San Andreas's own, for other drivers)
    inline constexpr float kCrashGap = 1.0f;       // s before another crash counts
    inline constexpr float kChaseMeters = 20.0f;   // a hostile this near, in combat with him, chases him

    // When a trigger speaks. Times are seconds on one clock (San Andreas's, which stops while paused).
    class Chatter
    {
    public:
        explicit Chatter(float a_now = 0.0f) : _lastLine(a_now) { _last.fill(-1e9f); }

        // CJ said something (a key, a conversation, a trigger, or a line of San Andreas's own).
        void Heard(float a_now) { _lastLine = a_now; }

        float Quiet(float a_now) const { return a_now - _lastLine; }

        // a_trigger fired: whether CJ says its line now. a_roll is uniform in [0, 1); a_speaking: a line of
        // his still plays, which a trigger never cuts off. Speaking counts as a line heard.
        bool Speak(Trigger a_trigger, float a_now, float a_roll, bool a_speaking = false)
        {
            const auto& info = InfoOf(a_trigger);
            auto&       last = _last[static_cast<std::size_t>(a_trigger)];
            if (a_speaking || Quiet(a_now) < kGap || a_now - last < info.repeat || !(a_roll < info.chance)) {
                return false;
            }
            last = a_now;
            _lastLine = a_now;
            return true;
        }

    private:
        float                              _lastLine;
        std::array<float, kTriggerCount>   _last{};
    };

    // A rising edge: true the first time a_now is true after it was false.
    class Edge
    {
    public:
        bool Update(bool a_now)
        {
            const bool rose = a_now && !_was;
            _was = a_now;
            return rose;
        }

    private:
        bool _was = false;
    };

    // Bursts of gunfire: a hit is a burst's first when the last one was more than kBurstGap ago.
    class Burst
    {
    public:
        bool Hit(float a_now)
        {
            const bool first = a_now - _last > kBurstGap;
            _last = a_now;
            return first;
        }

    private:
        float _last = -1e9f;
    };

    // A crash: the vehicle's health dropping by kCrashHealthLoss or more since the last frame, once per
    // kCrashGap (gunfire on it counts too). A new vehicle (or none) starts over; repairs only raise the
    // baseline.
    class Crash
    {
    public:
        bool Update(const void* a_vehicle, float a_health, float a_now)
        {
            if (a_vehicle != _vehicle) {
                _vehicle = a_vehicle;
                _health = a_health;
                return false;
            }
            const bool crashed = _health - a_health >= kCrashHealthLoss && a_now - _last >= kCrashGap;
            if (crashed) {
                _last = a_now;
            }
            _health = a_health;
            return crashed;
        }

    private:
        const void* _vehicle = nullptr;
        float       _health = 0.0f;
        float       _last = -1e9f;
    };

    // Singing: he starts once he's been settled (in a vehicle with the radio off, or idle on foot, with no
    // hostile near) for kQuietToSing with nothing said for as long, and is tried again after as long if the
    // song didn't come; a song stops when he isn't settled any more.
    class Singing
    {
    public:
        enum class Action
        {
            None,
            Start,
            Stop,
        };

        // a_singing: his line now is a song still playing.
        Action Update(bool a_settled, float a_now, float a_quiet, bool a_singing)
        {
            if (!a_settled) {
                _settled = false;
                return a_singing ? Action::Stop : Action::None;
            }
            if (!_settled) {
                _settled = true;
                _since = a_now;
            }
            if (a_singing || a_now - _since < kQuietToSing || a_quiet < kQuietToSing) {
                return Action::None;
            }
            _since = a_now;  // a song that didn't come (he spoke meanwhile) is tried again in 90 s
            return Action::Start;
        }

    private:
        bool  _settled = false;
        float _since = 0.0f;
    };

    // Counters Skyrim bumps (stealthKillCount, cashCount, shopBuyCount, shopLeaveCount): the first value
    // read only seeds, as for the spawn and swear keys; after that a new value fired.
    class Counter
    {
    public:
        bool Update(std::uint16_t a_value)
        {
            const bool fired = _have && a_value != _value;
            _value = a_value;
            _have = true;
            return fired;
        }

    private:
        bool          _have = false;
        std::uint16_t _value = 0;
    };

    // Skyrim's half of the shop triggers: the player's gold, read every so often. Gold gained outside a
    // shop is loot; gold spent in one a buy; the shop closing after a buy is leaving it. Seed() drops the
    // baseline (a load, a new handover), so the next read doesn't count.
    class Purse
    {
    public:
        struct Events
        {
            bool cash = false;
            bool buy = false;
            bool leave = false;
        };

        void Seed() { _have = false; }

        Events Update(std::int64_t a_gold, bool a_shopOpen)
        {
            Events out;
            if (_have) {
                if (a_gold > _gold && !a_shopOpen) {
                    out.cash = true;
                } else if (a_gold < _gold && a_shopOpen) {
                    out.buy = true;
                    _bought = true;
                }
            }
            if (_shopOpen && !a_shopOpen) {
                out.leave = _bought;
                _bought = false;
            }
            _shopOpen = a_shopOpen;
            _gold = a_gold;
            _have = true;
            return out;
        }

    private:
        bool         _have = false;
        std::int64_t _gold = 0;
        bool         _shopOpen = false;
        bool         _bought = false;
    };
}
