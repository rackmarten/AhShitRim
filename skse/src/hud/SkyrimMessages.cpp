#include "hud/SkyrimMessages.h"

#include "hud/SaHud.h"
#include "hud_messages.h"

namespace AhShitRim::SkyrimMessages
{
    namespace
    {
        // Written once on kDataLoaded, read by the hook afterwards.
        std::vector<HudMessages::Pattern> patterns = HudMessages::EnglishPatterns();
        std::string                       discovered = "discovered";  // sDiscoveredText
        std::string                       wordLearned = "Word of Power Learned";  // sShoutAdded
        std::string                       soulAbsorbed = "Dragon Soul Absorbed";  // sDragonSoulAcquired

        // The hook's catch, for the main thread.
        struct Caught
        {
            std::mutex  lock;
            std::string zone;
            bool        here = false;  // a location message without a name: the player's location
            std::string help;
        } caught;

        bool wasOwned = false;

        std::string SettingText(const char* a_name)
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            auto* setting = settings ? settings->GetSetting(a_name) : nullptr;
            if (!setting || setting->GetType() != RE::Setting::Type::kString || !setting->GetString()) {
                return {};
            }
            return setting->GetString();
        }

        std::string LocationName(RE::PlayerCharacter* a_player)
        {
            auto* location = a_player ? a_player->GetCurrentLocation() : nullptr;
            for (; location; location = location->parentLoc) {
                if (const char* name = location->GetName(); name && name[0]) {
                    return name;
                }
            }
            return {};
        }
    }

    void OnDataLoaded()
    {
        std::vector<HudMessages::Pattern> read;
        std::size_t                       missing = 0;
        for (const auto& rule : HudMessages::kSettingRules) {
            auto text = SettingText(std::string(rule.setting).c_str());
            if (text.empty()) {
                missing++;
                SKSE::log::warn("skyrim messages: no game setting {}, its English text \"{}\" stands in", rule.setting, rule.english);
                text = rule.english;
            }
            read.push_back({ std::move(text), rule.action, rule.setting });
        }
        for (const auto& rule : HudMessages::kPhraseRules) {
            read.push_back({ std::string(rule.english), rule.action, "phrase" });
        }
        patterns = std::move(read);
        for (auto [name, target] : { std::pair{ "sDiscoveredText", &discovered }, std::pair{ "sShoutAdded", &wordLearned }, std::pair{ "sDragonSoulAcquired", &soulAbsorbed } }) {
            if (auto text = SettingText(name); !text.empty()) {
                *target = std::move(text);
            }
        }
        SKSE::log::info("skyrim messages: {} notification patterns from Skyrim's game settings ({} missing)", patterns.size(), missing);
    }

    bool OnHudMessage(const RE::HUDData& a_data, bool a_cjHasPlayer)
    {
        if (!a_cjHasPlayer) {
            return false;
        }
        const int        type = static_cast<int>(a_data.type.get());
        const std::string text = a_data.text.c_str() ? a_data.text.c_str() : "";
        const auto       verdict = HudMessages::Classify(type, text, patterns);
        switch (verdict.action) {
        case HudMessages::Action::Hide:
            SKSE::log::info("skyrim messages: \"{}\" (type {}) hidden ({})", text, type, verdict.rule);
            return true;
        case HudMessages::Action::Help: {
            std::string help = text;
            if (type == HudMessages::Types::kWordOfPowerLearned) {
                const char* word = a_data.wordOfPower ? a_data.wordOfPower->GetName() : nullptr;
                help = word && word[0] ? wordLearned + ": " + word : wordLearned;
            } else if (type == HudMessages::Types::kDragonSoulAbsorbed) {
                help = soulAbsorbed;
            }
            if (help.empty()) {
                return true;
            }
            SKSE::log::info("skyrim messages: \"{}\" (type {}) in San Andreas's help box ({})", help, type, verdict.rule);
            std::scoped_lock guard(caught.lock);
            caught.help = std::move(help);
            return true;
        }
        case HudMessages::Action::ZoneName: {
            auto zone = HudMessages::ZoneText(text, discovered);
            SKSE::log::info("skyrim messages: \"{}\" (type {}) as San Andreas's zone name \"{}\"", text, type, zone);
            std::scoped_lock guard(caught.lock);
            caught.here = zone.empty();
            caught.zone = std::move(zone);
            return true;
        }
        default:
            return false;  // Skyrim's, or Missions' and Conversation's, which saw it first
        }
    }

    void Update(bool a_owned)
    {
        std::string zone, help;
        bool        here = false;
        {
            std::scoped_lock guard(caught.lock);
            zone = std::exchange(caught.zone, {});
            help = std::exchange(caught.help, {});
            here = std::exchange(caught.here, false);
        }
        const bool taken = a_owned && !wasOwned;
        wasOwned = a_owned;
        if (!a_owned) {
            return;
        }
        if (here || (taken && zone.empty())) {
            zone = LocationName(RE::PlayerCharacter::GetSingleton());
            if (taken && !zone.empty()) {
                SKSE::log::info("skyrim messages: CJ took the player in {}, San Andreas's zone name", zone);
            }
        }
        if (!zone.empty()) {
            SaHud::ShowZoneName(std::move(zone));
        }
        if (!help.empty()) {
            SaHud::ShowHelp(std::move(help));
        }
    }
}
