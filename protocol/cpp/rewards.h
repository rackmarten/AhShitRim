// Gun rewards (#830), the game-free half: the main quest's steps up to the Greybeards and the bosses on the
// way give CJ San Andreas's guns, heavier as the story goes. A quest step is a stage of a Skyrim.esm quest
// being set (the stage that completes it, from the quest's own data); a boss is an actor Skyrim marks as
// its location's boss (the Boss location ref type) dying in one of the table's locations, once per
// location until a save is loaded. Header-only and free of game types, so the protocol tests cover it; the SKSE plugin
// watches Skyrim's quest stages and deaths (skse/src/player/Rewards.cpp).

#pragma once

#include <cstdint>
#include <string>

#include "weapons.h"

namespace AhShitRim::Rewards
{
    inline constexpr std::uint32_t kBossRefType = 0x000130F7;  // LCRT Boss

    struct Reward
    {
        std::uint8_t  type;    // San Andreas's weapon type
        std::uint32_t rounds;
        const char*   weapon;  // for the help box
        const char*   why;     // who gave it or what for, for the help box
    };

    // A quest's stage: Skyrim.esm's quest and the stage that completes it.
    struct QuestStep
    {
        std::uint32_t quest;
        std::uint16_t stage;
        const char*   name;
        Reward        reward;
    };

    inline constexpr QuestStep kQuestSteps[] = {
        { 0x0003372B, 900, "Unbound", { Weapons::Pistol, 68, "Pistol", "for escaping Helgen" } },
        { 0x0004E50D, 160, "Before the Storm", { Weapons::Shotgun, 25, "Shotgun", "from Jarl Balgruuf" } },
        { 0x000D0800, 190, "Bleak Falls Barrow", { Weapons::MicroUzi, 200, "Micro SMG", "from Farengar, for the Dragonstone" } },
        { 0x0002610C, 160, "Dragon Rising", { Weapons::Ak47, 240, "AK-47", "from Jarl Balgruuf, for the dragon" } },
        { 0x000242BA, 160, "The Way of the Voice", { Weapons::RocketLauncher, 6, "Rocket launcher", "from the Greybeards" } },
    };

    // A boss's location (Skyrim.esm's LCTN).
    struct Boss
    {
        std::uint32_t location;
        const char*   name;
        Reward        reward;
    };

    inline constexpr Boss kBosses[] = {
        { 0x000B712C, "Embershard Mine", { Weapons::PistolSilenced, 51, "Silenced pistol", "for Embershard Mine's bandit boss" } },
        { 0x00018EE9, "Bleak Falls Barrow", { Weapons::Sawnoff, 36, "Sawn-off shotgun", "for Bleak Falls Barrow's draugr boss" } },
        { 0x00019193, "Halted Stream Camp", { Weapons::DesertEagle, 35, "Desert Eagle", "for Halted Stream Camp's bandit boss" } },
        { 0x000192A9, "Silent Moons Camp", { Weapons::Mp5, 240, "SMG", "for Silent Moons Camp's bandit boss" } },
    };

    constexpr const QuestStep* ForStage(std::uint32_t a_quest, std::uint16_t a_stage)
    {
        for (const auto& step : kQuestSteps) {
            if (step.quest == a_quest && step.stage == a_stage) {
                return &step;
            }
        }
        return nullptr;
    }

    // The boss reward for a death in a location or any of the locations that hold it (a dungeon's own
    // cells can sit in a location of their own under the dungeon's), innermost first.
    template <class It>
    const Boss* ForBoss(It a_begin, It a_end)
    {
        for (auto it = a_begin; it != a_end; ++it) {
            for (const auto& boss : kBosses) {
                if (boss.location == *it) {
                    return &boss;
                }
            }
        }
        return nullptr;
    }

    // The help box: "Shotgun and 25 rounds from Jarl Balgruuf."
    inline std::string Text(const Reward& a_reward)
    {
        const bool rockets = a_reward.type == Weapons::RocketLauncher || a_reward.type == Weapons::RocketLauncherHs;
        return std::string(a_reward.weapon) + " and " + std::to_string(a_reward.rounds) + (rockets ? " rockets " : " rounds ") + a_reward.why + ".";
    }
}
