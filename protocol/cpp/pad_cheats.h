// San Andreas's console cheat sequences on the pad (docs/DESIGN.md, Controller, Cheats on the pad). The PS2's buttons by
// position, as San Andreas's later console versions map them: L1 LB, L2 LT, R1 RB, R2 RT, Cross A, Circle B, Square X,
// Triangle Y, and the d-pad. Every press while CJ has the player goes into a list of the newest presses; a sequence the
// list ends with finishes its cheat (the same row of Cheats::kCheats a typed code finishes), and the list empties.
// Header-only and free of game types, so the protocol tests cover it; skse/src/player/PlayerSync.cpp feeds it.
//
// The sequences are the PS2's, from gtabase.com's and igrandtheftauto.com's lists, checked against gtaboom.com's and
// gta-sanandreas.com's. Where the pages differ: the jetpack is given with 10, 12 and 16 presses, and the longer two end
// with the 10-press one, which is the one here (so all three work); two more wanted stars is R1, R1, Circle, R2, then
// left, right three times on two pages and right, left on one, and both are here. AhShitRim's own codes, the vehicle
// names and VKYPQCF (maximum stamina, never on the console) have no sequence and stay keyboard only.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cheats.h"
#include "pad.h"

namespace AhShitRim::PadCheats
{
    // A sequence's buttons as letters: 1 L1, 2 L2, 3 R1, 4 R2, X Cross, O Circle, S Square, T Triangle, U D L R the d-pad.
    struct Sequence
    {
        std::string_view code;     // the PC code whose row of Cheats::kCheats it finishes
        std::string_view buttons;
    };

    inline constexpr Sequence kSequences[] = {
        { "HESOYAM", "341XLDRULDRU" },
        { "BAGUVIX", "DXRLR3RDUT" },
        { "LXGIWYL", "3414LDRULDRU" },
        { "KJKSZPJ", "3414LDRULDDL" },
        { "UZUMYMW", "3414LDRULDDD" },
        { "FULLCLIP", "13S3L43LSD11" },
        { "NCSGDAG", "DSXL34LDD111" },
        { "AIWPRTON", "OO1OOO123TOT" },
        { "CQZIJMB", "D3O22X31LL" },
        { "JQNTDMH", "URR1RUS2" },
        { "PDNEJOH", "3O4R12XXS3" },
        { "VPJTQWV", "41OR13RUO4" },
        { "AQTBCODX", "D4D32L31LR" },
        { "KRIJEBR", "4U2LL31OR" },
        { "UBHYZHQ", "O3O3LL31OR" },
        { "RZHSUEW", "O1U32X31OX" },
        { "JUMPJET", "TTSOX11DU" },
        { "KGGGDKP", "TTSOX12DD" },
        { "OHDUDE", "OX1OO1O34211" },
        { "AKJJYGLC", "LLDDUUSOT34" },
        { "AMOMHRER", "3ULR4URSR211" },
        { "EEGCYXT", "411RRUUX1L" },
        { "URKQSRK", "OU12D311LLXT" },
        { "AGBDLCID", "RU333DTTXO11" },
        { "YECGAA", "LR1234UDLR" },
        { "AIYPWZQP", "LR12344UDR1" },
        { "RIPAZHA", "SD2U1OUXL" },
        { "JHJOECW", "TSOOSOO12234" },
        { "PGGOMOY", "T33L3141" },
        { "COXEFGU", "LT31USTDO211" },
        { "JCNRUAD", "122UDDU344" },
        { "LFGMHAL", "UUTTUULRS44" },
        { "IAVENJQ", "ULXT3OOO2" },
        { "BSXSGGC", "S4DDLDLL2X" },
        { "CPKTNWT", "423124STOT21" },
        { "BTCDBCB", "TUULRSOD" },
        { "BUFFMEUP", "TUULRSOL" },
        { "KVGYZQK", "TUULRSOR" },
        { "VQIMAHA", "S2X322L3R111" },
        { "SZCMAWO", "R2D3LL3121" },
        { "AEZAKMI", "ORORLSTU" },
        { "OSRBLHH", "33O4LRLRLR" },
        { "OSRBLHH", "33O4RLRLRL" },
        { "LJSPQK", "ORORLSXD" },
        { "ASNAEB", "33O4UDUDUD" },
    };

    // A Skyrim gamepad id code as a sequence letter, 0 for a button no sequence has (L3, R3, Back, Start).
    constexpr char LetterOf(std::uint32_t a_id)
    {
        switch (a_id) {
        case Pad::kLeftShoulder: return '1';
        case Pad::kLeftTrigger: return '2';
        case Pad::kRightShoulder: return '3';
        case Pad::kRightTrigger: return '4';
        case Pad::kA: return 'X';
        case Pad::kB: return 'O';
        case Pad::kX: return 'S';
        case Pad::kY: return 'T';
        case Pad::kUp: return 'U';
        case Pad::kDown: return 'D';
        case Pad::kLeft: return 'L';
        case Pad::kRight: return 'R';
        default: return 0;
        }
    }

    // The row of Cheats::kCheats a sequence finishes, nullptr if its code isn't one (the tests check every one is).
    constexpr const Cheats::Cheat* CheatOf(const Sequence& a_sequence)
    {
        for (const auto& cheat : Cheats::kCheats) {
            if (cheat.code == a_sequence.code) {
                return &cheat;
            }
        }
        return nullptr;
    }

    // The newest presses, and the sequence they finish.
    class Buttons
    {
    public:
        static constexpr std::size_t kLength = 16;  // the longest sequence is 12

        // A press (a Skyrim gamepad id code). Returns the cheat it finished, or nullptr.
        const Cheats::Cheat* Press(std::uint32_t a_id)
        {
            const char letter = LetterOf(a_id);
            if (!letter) {
                return nullptr;
            }
            for (std::size_t i = kLength - 1; i > 0; --i) {
                _letters[i] = _letters[i - 1];
            }
            _letters[0] = letter;  // newest first
            _count = _count < kLength ? _count + 1 : kLength;
            for (const auto& sequence : kSequences) {
                if (Ends(sequence.buttons)) {
                    Clear();
                    return CheatOf(sequence);
                }
            }
            return nullptr;
        }

        void Clear() { _count = 0; }

    private:
        bool Ends(std::string_view a_buttons) const
        {
            const std::size_t n = a_buttons.size();
            if (n == 0 || n > _count) {
                return false;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (_letters[i] != a_buttons[n - 1 - i]) {
                    return false;
                }
            }
            return true;
        }

        std::array<char, kLength> _letters{};
        std::size_t               _count = 0;
    };
}
