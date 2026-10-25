#include "player/CombatBalance.h"

#include "Ini.h"

namespace AhShitRim::Balance
{
    namespace
    {
        Settings settings;

        int Difficulty(RE::PlayerCharacter* a_player)
        {
            return Clamp(a_player ? a_player->GetGameStatsData().difficulty : kAdept);
        }
    }

    void OnDataLoaded()
    {
        settings = Parse(Ini::Value("Balance", "CJDamage"), Ini::Value("Balance", "DamageTaken"), Ini::Value("Balance", "FollowDifficulty"));
        SKSE::log::info("balance: CJ's guns x{:.2f}{}, Skyrim's people's hits on CJ x{:.2f} (on top of Skyrim's difficulty), melee as it is", settings.cjDamage,
            settings.followDifficulty ? " and by the difficulty (Skyrim's fDiffMultHPByPC a step harder)" : "", settings.damageTaken);
    }

    const Settings& Current() { return settings; }

    float DealtFactor(RE::PlayerCharacter* a_player, std::uint8_t a_type)
    {
        return Dealt(settings, a_type, Difficulty(a_player));
    }

    float TakenFactor(float a_share) { return Taken(settings, a_share); }
}
