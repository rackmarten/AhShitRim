// What becomes of each of Skyrim's HUD messages while San Andreas has the player: kept as Skyrim shows it,
// restyled as one of San Andreas's texts, or hidden. Game-free, so the protocol tests cover it; the SKSE
// plugin's HUD message hook (Conversation) asks it for every message Skyrim's HUD gets, and docs/DESIGN.md
// ("Skyrim's HUD messages") has the table in words.
//
// By the message's type first. Skyrim's notifications (the text top left: "Iron Sword added", "You cannot
// fast travel when enemies are nearby.") all share one type, so those go by their text: Skyrim builds them
// from its game settings, whose text the plugin reads in the player's language at load (Patterns), and
// matches each notification against them. A notification nothing matches is shown in San Andreas's help
// box: whatever a script or a mod says gets through, in San Andreas's style.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AhShitRim::HudMessages
{
    enum class Action : std::uint8_t
    {
        Keep,      // Skyrim's own, as it is: not a text (the crosshair, meters, modes) or one that stays Skyrim's
        Hide,      // not shown at all; what it reports still happens
        Help,      // San Andreas's help box, top left
        ZoneName,  // San Andreas's zone name, bottom right
        Mission,   // San Andreas's mission title or "mission passed!" (Missions decides which)
        Subtitle,  // San Andreas's subtitles (Conversation)
    };

    constexpr std::string_view NameOf(Action a_action)
    {
        switch (a_action) {
        case Action::Keep: return "kept";
        case Action::Hide: return "hidden";
        case Action::Help: return "help box";
        case Action::ZoneName: return "zone name";
        case Action::Mission: return "mission text";
        case Action::Subtitle: return "subtitles";
        }
        return "?";
    }

    // Skyrim SE and AE's HUD message types (CommonLibSSE's RE::HUD_MESSAGE_TYPE), as numbers to stay game-free.
    namespace Types
    {
        inline constexpr int kNotification = 1;
        inline constexpr int kShowSubtitle = 5;
        inline constexpr int kHideSubtitle = 6;
        inline constexpr int kQuestStarted = 14;
        inline constexpr int kQuestComplete = 15;
        inline constexpr int kQuestFailed = 16;
        inline constexpr int kObjectiveStarted = 17;
        inline constexpr int kObjectiveComplete = 18;
        inline constexpr int kObjectiveFailed = 19;
        inline constexpr int kSkillIncrease = 20;
        inline constexpr int kWordOfPowerLearned = 21;
        inline constexpr int kDragonSoulAbsorbed = 22;
        inline constexpr int kLocationDiscovery = 26;
        inline constexpr int kShowLocationName = 29;
        inline constexpr int kShowHintText = 30;
    }

    struct TypeRule
    {
        int              type;
        Action           action;
        std::string_view what;  // for the log and the docs
    };

    // The message types that carry a text. Any other type (the crosshair's target, the arrow count, the
    // heartbeat, the HUD's modes and meters) is kept: it's not a text, or what SaHud hides already.
    inline constexpr TypeRule kTypeRules[] = {
        { Types::kNotification, Action::Help, "notification (by its text, see kSettingRules)" },
        { Types::kShowSubtitle, Action::Subtitle, "subtitle" },
        { Types::kHideSubtitle, Action::Subtitle, "subtitle gone" },
        { Types::kQuestStarted, Action::Mission, "quest started" },
        { Types::kQuestComplete, Action::Mission, "quest completed" },
        { Types::kQuestFailed, Action::Keep, "quest failed" },
        { Types::kObjectiveStarted, Action::Keep, "objective" },
        { Types::kObjectiveComplete, Action::Keep, "objective completed" },
        { Types::kObjectiveFailed, Action::Keep, "objective failed" },
        { Types::kSkillIncrease, Action::Hide, "skill increase and level up" },
        { Types::kWordOfPowerLearned, Action::Help, "word of power learned" },
        { Types::kDragonSoulAbsorbed, Action::Help, "dragon soul absorbed" },
        { Types::kLocationDiscovery, Action::ZoneName, "location discovered" },
        { Types::kShowLocationName, Action::ZoneName, "location entered" },
        { Types::kShowHintText, Action::Help, "hint" },
    };

    constexpr const TypeRule* RuleOf(int a_type)
    {
        for (const auto& rule : kTypeRules) {
            if (rule.type == a_type) {
                return &rule;
            }
        }
        return nullptr;
    }

    // A notification built from one of Skyrim's game settings, by the setting's name, with its English text
    // (what the plugin falls back to when the setting isn't there, and what the tests use). %s, %i and the
    // like stand for anything.
    struct SettingRule
    {
        std::string_view setting;
        Action           action;
        std::string_view english;
    };

    inline constexpr SettingRule kSettingRules[] = {
        // Junk: San Andreas tells CJ none of this.
        { "sAddItemtoInventory", Action::Hide, "Added" },  // "Iron Sword added", "Gold (25) added"
        { "sRemoveItemfromInventory", Action::Hide, "Removed" },
        { "sAddItemtoSpellList", Action::Hide, "added" },  // a spell learned
        { "sSpellAdded", Action::Hide, "New Spell Learned" },
        { "sEquipItemOnPlayer", Action::Hide, "was equipped" },
        { "sUnequipItemOnPlayer", Action::Hide, "was unequipped" },
        { "sScrollEquipped", Action::Hide, " equipped." },
        { "sAddedNote", Action::Hide, "Note Added: %s" },
        { "sMapMarkerAdded", Action::Hide, "Map updated." },
        { "sOpenWithKey", Action::Hide, "Unlocked with %s." },
        { "sOpenedContainer", Action::Hide, "Opened the container with a key." },
        { "sSoulCaptured", Action::Hide, "Soul captured!" },
        { "sContractedDisease", Action::Hide, "You have contracted" },
        { "sPoisoned", Action::Hide, "You have been poisoned." },
        { "sAttributeDrained", Action::Hide, "is being drained" },
        { "sDiscoveredIngredientEffectEating", Action::Hide, "%s discovered from %s" },
        { "sIngredientFail", Action::Hide, " Ingredient had no effect." },
        { "sEnchantmentsLearned", Action::Hide, "You learned the %s enchantment from destroying this item." },
        { "sLevelUpAvailable", Action::Hide, "Level up available." },
        { "sNoArrows", Action::Hide, "No arrows equipped." },  // CJ shoots San Andreas's weapons
        { "sWeaponBreak", Action::Hide, "Your weapon has broken." },
        { "sAddCrimeGold", Action::Hide, "bounty added to" },  // the wanted stars say it, and the bounty is wiped at once
        { "sRemoveCrimeGold", Action::Hide, "bounty removed from" },
        { "sGotAwayWithStealing", Action::Hide, "got away with stealing" },
        // What matters: what CJ can't do, in the help box. Any notification nothing here matches goes there
        // too; these are named so the log says which it was, and so a longer one wins over a word above.
        { "sNoFastTravelHostileActorsNear", Action::Help, "You cannot fast travel when enemies are nearby." },
        { "sNoFastTravelCombat", Action::Help, "You cannot fast travel during combat!" },
        { "sNoFastTravelAlarm", Action::Help, "You cannot fast travel while guards are pursuing you." },
        { "sNoFastTravelDefault", Action::Help, "You cannot fast travel at this time." },
        { "sNoFastTravelCell", Action::Help, "You cannot fast travel from this location." },
        { "sNoFastTravelScriptBlock", Action::Help, "Fast travel is currently unavailable from this location." },
        { "sNoFastTravelOverencumbered", Action::Help, "You cannot fast travel while overencumbered." },
        { "sImpossibleLock", Action::Help, "This lock cannot be picked. It requires a key to open." },
        { "sOutOfLockpicks", Action::Help, "You are out of lockpicks." },
        { "sLockpickInsufficientPerks", Action::Help, "You lack the required perk to pick this lock." },
        { "sNoLockPickIfCrimeAlert", Action::Help, "You can't pick a lock while being pursued by guards." },
        { "sNoWaitHostileActorsNear", Action::Help, "You cannot wait when enemies are nearby." },
        { "sNoSleepHostileActorsNear", Action::Help, "You cannot sleep when enemies are nearby." },
        { "sItemTooHeavy", Action::Help, "The item is too heavy to carry." },
        { "sCombatCannotActivate", Action::Help, "You cannot use this while in combat." },
        { "sPlayerLeavingBorderRegion", Action::Help, "You cannot go that way." },
        { "sDropQuestItemWarning", Action::Help, "Quest Items cannot be removed from your Inventory." },
        { "sNoMoreFollowers", Action::Help, "You have too many followers." },
        { "sTeammateOverencumbered", Action::Help, "can't carry any more." },
    };

    // Notifications no game setting builds (scripts' and messages' texts), in English only: they're the
    // game's own text, the same in every language's game settings or none.
    inline constexpr SettingRule kPhraseRules[] = {
        { "", Action::Hide, "You feel" },  // shrines, standing stones, diseases cured: "You feel refreshed."
    };

    struct Pattern
    {
        std::string text;
        Action      action = Action::Help;
        std::string_view name;  // the setting's, for the log
    };

    // The English patterns, for the tests and as the fallback for a game setting the game doesn't have.
    inline std::vector<Pattern> EnglishPatterns()
    {
        std::vector<Pattern> patterns;
        for (const auto& rule : kSettingRules) {
            patterns.push_back({ std::string(rule.english), rule.action, rule.setting });
        }
        for (const auto& rule : kPhraseRules) {
            patterns.push_back({ std::string(rule.english), rule.action, "phrase" });
        }
        return patterns;
    }

    namespace Detail
    {
        constexpr char Lower(char a_c) { return a_c >= 'A' && a_c <= 'Z' ? static_cast<char>(a_c - 'A' + 'a') : a_c; }
        constexpr bool Word(char a_c)
        {
            return (a_c >= 'a' && a_c <= 'z') || (a_c >= 'A' && a_c <= 'Z') || (a_c >= '0' && a_c <= '9') || static_cast<unsigned char>(a_c) >= 0x80;
        }

        // Where a_part is in a_text from a_from on, compared without case; npos for nowhere.
        inline std::size_t Find(std::string_view a_text, std::string_view a_part, std::size_t a_from)
        {
            if (a_part.size() > a_text.size()) {
                return std::string_view::npos;
            }
            for (std::size_t i = a_from; i + a_part.size() <= a_text.size(); i++) {
                std::size_t k = 0;
                while (k < a_part.size() && Lower(a_text[i + k]) == Lower(a_part[k])) {
                    k++;
                }
                if (k == a_part.size()) {
                    return i;
                }
            }
            return std::string_view::npos;
        }

        // A pattern's literal parts, between its printf specifiers (%s, %i, %.0f, ...), trimmed of spaces.
        inline std::vector<std::string_view> Parts(std::string_view a_pattern)
        {
            std::vector<std::string_view> parts;
            std::size_t                   start = 0;
            const auto                    push = [&](std::size_t a_end) {
                auto part = a_pattern.substr(start, a_end - start);
                while (!part.empty() && part.front() == ' ') {
                    part.remove_prefix(1);
                }
                while (!part.empty() && part.back() == ' ') {
                    part.remove_suffix(1);
                }
                if (!part.empty()) {
                    parts.push_back(part);
                }
            };
            for (std::size_t i = 0; i < a_pattern.size(); i++) {
                if (a_pattern[i] != '%') {
                    continue;
                }
                push(i);
                std::size_t k = i + 1;
                while (k < a_pattern.size() && (a_pattern[k] == '.' || a_pattern[k] == '-' || (a_pattern[k] >= '0' && a_pattern[k] <= '9'))) {
                    k++;
                }
                i = k;  // the conversion letter, or the second % of %%
                start = k + 1;
            }
            push(a_pattern.size());
            return parts;
        }
    }

    // Whether a_text holds a_pattern: its literal parts in order, compared without case, the first starting
    // and the last ending on a word's edge (so "added" doesn't match "addedly"), anything between them and
    // round them. A pattern without a literal part matches nothing.
    inline bool Matches(std::string_view a_text, std::string_view a_pattern)
    {
        const auto parts = Detail::Parts(a_pattern);
        if (parts.empty()) {
            return false;
        }
        for (std::size_t from = 0;;) {
            const auto first = Detail::Find(a_text, parts.front(), from);
            if (first == std::string_view::npos) {
                return false;
            }
            from = first + 1;
            if (first > 0 && Detail::Word(a_text[first - 1]) && Detail::Word(parts.front().front())) {
                continue;
            }
            std::size_t at = first + parts.front().size();
            bool        found = true;
            for (std::size_t i = 1; i < parts.size() && found; i++) {
                const auto next = Detail::Find(a_text, parts[i], at);
                found = next != std::string_view::npos;
                at = found ? next + parts[i].size() : at;
            }
            if (!found) {
                return false;
            }
            const char lastOfPattern = parts.back().back();
            if (at < a_text.size() && Detail::Word(a_text[at]) && Detail::Word(lastOfPattern)) {
                continue;
            }
            return true;
        }
    }

    struct Verdict
    {
        Action           action = Action::Keep;
        std::string_view rule;  // the matching setting or type, for the log
    };

    // What becomes of a HUD message of a_type with a_text. A notification goes by the longest pattern it
    // matches, the help box when none does; an empty one is hidden.
    inline Verdict Classify(int a_type, std::string_view a_text, const std::vector<Pattern>& a_patterns)
    {
        const auto* rule = RuleOf(a_type);
        if (!rule) {
            return { Action::Keep, "not a text" };
        }
        if (a_type != Types::kNotification) {
            return { rule->action, rule->what };
        }
        if (a_text.empty()) {
            return { Action::Hide, "empty" };
        }
        const Pattern* best = nullptr;
        std::size_t    bestLength = 0;
        for (const auto& pattern : a_patterns) {
            if (pattern.text.size() > bestLength && Matches(a_text, pattern.text)) {
                best = &pattern;
                bestLength = pattern.text.size();
            }
        }
        if (!best) {
            return { Action::Help, "no rule" };
        }
        return { best->action, best->name };
    }

    // The zone name for a location message's text: without the "discovered" Skyrim's banner may put after it
    // (its sDiscoveredText), and trimmed.
    inline std::string ZoneText(std::string_view a_text, std::string_view a_discovered)
    {
        const auto trim = [](std::string_view s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\n')) {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\n')) {
                s.remove_suffix(1);
            }
            return s;
        };
        auto text = trim(a_text);
        if (!a_discovered.empty() && text.size() > a_discovered.size() &&
            Detail::Find(text, a_discovered, text.size() - a_discovered.size()) == text.size() - a_discovered.size()) {
            text = trim(text.substr(0, text.size() - a_discovered.size()));
        }
        return std::string(text);
    }
}
