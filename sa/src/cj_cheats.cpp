#include "cj_cheats.h"

#include "log.h"

#include <plugin.h>

#include <CCheat.h>

#include <cstring>
#include <deque>
#include <format>

namespace AhShitRim::CjCheats
{
    namespace
    {
        struct State
        {
            std::deque<Protocol::CheatCommand> commands;
            int                                resolved[Cheats::kCheatCount] = {};  // San Andreas's index of each row, found once
            bool                               haveResolved = false;
        } state;

        // San Andreas's index of the cheat with a_key: a_expected when its table agrees, else the last one with that key (as
        // AddToCheatString picks), -1 for none.
        int IndexOf(std::uint32_t a_key, std::uint8_t a_expected)
        {
            if (a_expected < TOTAL_CHEATS && static_cast<std::uint32_t>(CCheat::m_aCheatHashKeys[a_expected]) == a_key) {
                return a_expected;
            }
            int found = -1;
            for (int i = 0; i < TOTAL_CHEATS; ++i) {
                if (static_cast<std::uint32_t>(CCheat::m_aCheatHashKeys[i]) == a_key) {
                    found = i;
                }
            }
            return found;
        }

        void Resolve()
        {
            if (state.haveResolved) {
                return;
            }
            state.haveResolved = true;
            int moved = 0;
            for (const auto& cheat : Cheats::kCheats) {
                const auto i = Cheats::IndexOf(cheat);
                state.resolved[i] = cheat.sa == Cheats::SaCheat::None ? -1 : IndexOf(Cheats::CodeKey(cheat.code), static_cast<std::uint8_t>(cheat.sa));
                if (cheat.sa != Cheats::SaCheat::None && state.resolved[i] != static_cast<int>(cheat.sa)) {
                    moved++;
                    Log(std::format("cheats: {} is San Andreas's cheat {}, not {} as expected", cheat.code, state.resolved[i], static_cast<int>(cheat.sa)));
                }
            }
            Log(std::format("cheats: {} cheats found in San Andreas's table{}", Cheats::kCheatCount, moved ? std::format(", {} not where expected", moved) : std::string()));
        }

        void Apply(const Protocol::CheatCommand& a_command)
        {
            const int i = IndexOf(a_command.key, a_command.cheat);
            if (i < 0) {
                Log(std::format("cheats: no San Andreas cheat has the key {:08X} (cheat {}), ignored", a_command.key, a_command.cheat));
                return;
            }
            const bool on = (a_command.flags & Protocol::kCheatOn) != 0;
            if (auto* function = CCheat::m_aCheatFunctions[i]) {
                if (on) {
                    function();
                    Log(std::format("cheats: San Andreas's cheat {} done", i));
                }
            } else {
                CCheat::m_aCheatsActive[i] = on;
                Log(std::format("cheats: San Andreas's cheat {} {}", i, on ? "on" : "off"));
            }
            CCheat::m_bHasPlayerCheated = true;
        }
    }

    void Queue(const std::uint8_t* a_data, std::uint32_t a_size)
    {
        if (a_size != sizeof(Protocol::CheatCommand)) {
            Log(std::format("cheats: a command of {} bytes (expected {}), ignored", a_size, sizeof(Protocol::CheatCommand)));
            return;
        }
        Protocol::CheatCommand command{};
        std::memcpy(&command, a_data, sizeof(command));
        state.commands.push_back(command);
        while (state.commands.size() > 64) {
            state.commands.pop_front();
        }
    }

    void Update(CPlayerPed* a_ped)
    {
        Resolve();
        if (!a_ped) {
            return;
        }
        while (!state.commands.empty()) {
            Apply(state.commands.front());
            state.commands.pop_front();
        }
    }

    bool Active(Cheats::SaCheat a_cheat)
    {
        const auto* row = Cheats::Find(a_cheat);
        if (!row || !state.haveResolved) {
            return false;
        }
        const int i = state.resolved[Cheats::IndexOf(*row)];
        return i >= 0 && CCheat::m_aCheatsActive[i];
    }
}
