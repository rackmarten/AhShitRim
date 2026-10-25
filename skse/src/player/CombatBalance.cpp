#include "player/CombatBalance.h"

#include "Ini.h"

#include <format>

namespace AhShitRim::Balance
{
    namespace
    {
        Settings settings;

        int Difficulty(RE::PlayerCharacter* a_player)
        {
            return Clamp(a_player ? a_player->GetGameStatsData().difficulty : kAdept);
        }

        // fDiffMultHPByPC<suffix> for a_difficulty, else Skyrim.esm's.
        float ByPlayer(int a_difficulty)
        {
            const auto name = std::format("fDiffMultHPByPC{}", kSuffixes[Clamp(a_difficulty)]);
            auto*      collection = RE::GameSettingCollection::GetSingleton();
            auto*      setting = collection ? collection->GetSetting(name.c_str()) : nullptr;
            return OrDefault(setting ? setting->GetFloat() : 0.0f, kByPlayer, a_difficulty);
        }
    }

    void OnDataLoaded()
    {
        settings = Parse(Ini::Value("Balance", "CJDamage"), Ini::Value("Balance", "DamageTaken"), Ini::Value("Balance", "FollowDifficulty"));
        SKSE::log::info("balance: CJ's guns x{:.2f}{}, Skyrim's people's hits on CJ x{:.2f} (on top of Skyrim's difficulty), melee as it is", settings.cjDamage,
            settings.followDifficulty ? " and Skyrim's fDiffMultHPByPC for the difficulty" : "", settings.damageTaken);
    }

    const Settings& Current() { return settings; }

    float DealtFactor(RE::PlayerCharacter* a_player, std::uint8_t a_type)
    {
        return Dealt(settings, a_type, settings.followDifficulty && !Weapons::IsMelee(a_type) ? ByPlayer(Difficulty(a_player)) : 1.0f);
    }

    float TakenFactor(float a_share) { return Taken(settings, a_share); }
}
