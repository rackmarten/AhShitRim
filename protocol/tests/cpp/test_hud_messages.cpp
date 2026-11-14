#include <doctest/doctest.h>

#include "../../cpp/hud_messages.h"

#include <ostream>  // doctest prints string_views
#include <set>

using namespace AhShitRim::HudMessages;

namespace
{
    Action Of(std::string_view a_text, int a_type = Types::kNotification)
    {
        static const auto patterns = EnglishPatterns();
        return Classify(a_type, a_text, patterns).action;
    }
}

TEST_CASE("Skyrim's location names become San Andreas's zone name")
{
    CHECK(Of("Riverwood", Types::kShowLocationName) == Action::ZoneName);
    CHECK(Of("Bleak Falls Barrow", Types::kLocationDiscovery) == Action::ZoneName);
    CHECK(ZoneText("Bleak Falls Barrow", "discovered") == "Bleak Falls Barrow");
    CHECK(ZoneText("  Riverwood Discovered", "discovered") == "Riverwood");
    CHECK(ZoneText("Riverwood\n", "") == "Riverwood");
    CHECK(ZoneText("discovered", "discovered") == "discovered");  // nothing in front of it: the text as it is
}

TEST_CASE("quest texts, subtitles and skill messages keep what they had")
{
    CHECK(Of("Bleak Falls Barrow", Types::kQuestStarted) == Action::Mission);
    CHECK(Of("Before the Storm", Types::kQuestComplete) == Action::Mission);
    CHECK(Of("The Golden Claw", Types::kQuestFailed) == Action::Keep);
    CHECK(Of("Retrieve the Golden Claw", Types::kObjectiveStarted) == Action::Keep);
    CHECK(Of("Retrieve the Golden Claw", Types::kObjectiveComplete) == Action::Keep);
    CHECK(Of("", Types::kObjectiveFailed) == Action::Keep);
    CHECK(Of("Lydia: I am sworn to carry your burdens.", Types::kShowSubtitle) == Action::Subtitle);
    CHECK(Of("", Types::kHideSubtitle) == Action::Subtitle);
    CHECK(Of("One-handed increased to 16", Types::kSkillIncrease) == Action::Hide);
    CHECK(Of("", Types::kWordOfPowerLearned) == Action::Help);
    CHECK(Of("", Types::kDragonSoulAbsorbed) == Action::Help);
}

TEST_CASE("messages that aren't texts stay Skyrim's")
{
    CHECK(Of("Iron Sword", 2) == Action::Keep);   // the crosshair's target
    CHECK(Of("", 7) == Action::Keep);             // the arrow count
    CHECK(Of("", 23) == Action::Keep);            // the HUD's mode
    CHECK(Of("anything", 999) == Action::Keep);
}

TEST_CASE("junk notifications are hidden")
{
    CHECK(Of("Iron Sword added") == Action::Hide);
    CHECK(Of("Gold (25) added") == Action::Hide);
    CHECK(Of("Iron Arrow (12) added") == Action::Hide);
    CHECK(Of("Lockpick removed") == Action::Hide);
    CHECK(Of("Flames added") == Action::Hide);
    CHECK(Of("Iron Helmet was equipped") == Action::Hide);
    CHECK(Of("Iron Helmet was unequipped") == Action::Hide);
    CHECK(Of("Note Added: Sven's letter") == Action::Hide);
    CHECK(Of("Map updated.") == Action::Hide);
    CHECK(Of("Unlocked with Riverwood Trader Key.") == Action::Hide);
    CHECK(Of("Soul captured!") == Action::Hide);
    CHECK(Of("You have contracted Rockjoint") == Action::Hide);
    CHECK(Of("You have been poisoned.") == Action::Hide);
    CHECK(Of("Restore Health discovered from Blue Mountain Flower") == Action::Hide);
    CHECK(Of("You feel refreshed.") == Action::Hide);
    CHECK(Of("you feel the blessing of Talos") == Action::Hide);
    CHECK(Of("40 bounty added to Whiterun") == Action::Hide);
    CHECK(Of("Bounty Added to Whiterun Hold") == Action::Hide);
    CHECK(Of("40 bounty removed from Whiterun") == Action::Hide);
    CHECK(Of("Level up available.") == Action::Hide);
    CHECK(Of("") == Action::Hide);
}

TEST_CASE("notifications that matter go to San Andreas's help box")
{
    CHECK(Of("You cannot fast travel when enemies are nearby.") == Action::Help);
    CHECK(Of("You cannot fast travel during combat!") == Action::Help);
    CHECK(Of("This lock cannot be picked. It requires a key to open.") == Action::Help);
    CHECK(Of("You are out of lockpicks.") == Action::Help);
    CHECK(Of("The item is too heavy to carry.") == Action::Help);
    CHECK(Of("Lydia can't carry any more.") == Action::Help);
    // Anything nothing names: a script's or a mod's text, in San Andreas's style.
    CHECK(Of("The Jarl wants to see you.") == Action::Help);
    // A word inside another isn't the word.
    CHECK(Of("Spadded armour") == Action::Help);
    CHECK(Of("Unremoved") == Action::Help);
}

TEST_CASE("the longest matching pattern wins")
{
    const std::vector<Pattern> patterns = { { "added", Action::Hide, "short" }, { "cannot be added to the cart", Action::Help, "long" } };
    const auto                 verdict = Classify(Types::kNotification, "This item cannot be added to the cart", patterns);
    CHECK(verdict.action == Action::Help);
    CHECK(verdict.rule == "long");
    CHECK(Classify(Types::kNotification, "Cart added", patterns).rule == "short");
}

TEST_CASE("patterns: printf specifiers stand for anything, case and word edges")
{
    CHECK(Matches("Note Added: Sven's letter", "Note Added: %s"));
    CHECK(Matches("Restore Health discovered from Blue Mountain Flower", "%s discovered from %s"));
    CHECK(Matches("CANNOT REPAIR PAST 100%", "CANNOT REPAIR PAST %.0f%s"));
    CHECK(Matches("Found 3 Dragon Souls", "%i Dragon Souls"));
    CHECK_FALSE(Matches("Dragon Souls", "%i Dragon Soul"));  // "Soul" ends inside "Souls"
    CHECK(Matches("iron sword ADDED", "Added"));
    CHECK_FALSE(Matches("Sword adder", "added"));
    CHECK_FALSE(Matches("anything", "%s"));  // nothing to match on
    CHECK_FALSE(Matches("anything", ""));
    CHECK(Matches("discovered from", "%s discovered from %s"));
    CHECK_FALSE(Matches("from discovered", "discovered %s from"));
}

TEST_CASE("the tables are whole: every rule named once, every type once")
{
    std::set<std::string_view> settings;
    for (const auto& rule : kSettingRules) {
        CHECK_FALSE(rule.setting.empty());
        CHECK_FALSE(rule.english.empty());
        CHECK(rule.setting.front() == 's');  // a string game setting
        CHECK(settings.insert(rule.setting).second);
        CHECK((rule.action == Action::Hide || rule.action == Action::Help));
        // Each rule's own English text is classified by that rule (or one as long that agrees with it).
        CHECK(Of(rule.english) == rule.action);
    }
    std::set<int> types;
    for (const auto& rule : kTypeRules) {
        CHECK(types.insert(rule.type).second);
        CHECK(RuleOf(rule.type) == &rule);
    }
    CHECK(NameOf(Action::ZoneName) == "zone name");
}
