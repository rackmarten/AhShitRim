#include <doctest/doctest.h>

#include "../../cpp/pad_cheats.h"

#include <ostream>
#include <string>

using namespace AhShitRim;
using namespace AhShitRim::PadCheats;

namespace
{
    std::uint32_t IdOf(char a_letter)
    {
        switch (a_letter) {
        case '1': return Pad::kLeftShoulder;
        case '2': return Pad::kLeftTrigger;
        case '3': return Pad::kRightShoulder;
        case '4': return Pad::kRightTrigger;
        case 'X': return Pad::kA;
        case 'O': return Pad::kB;
        case 'S': return Pad::kX;
        case 'T': return Pad::kY;
        case 'U': return Pad::kUp;
        case 'D': return Pad::kDown;
        case 'L': return Pad::kLeft;
        case 'R': return Pad::kRight;
        default: return 0;
        }
    }

    // Presses a sequence's buttons in turn; what the last one finished.
    const Cheats::Cheat* Enter(Buttons& a_buttons, std::string_view a_sequence)
    {
        const Cheats::Cheat* done = nullptr;
        for (std::size_t i = 0; i < a_sequence.size(); ++i) {
            const auto* cheat = a_buttons.Press(IdOf(a_sequence[i]));
            if (i + 1 < a_sequence.size()) {
                CHECK_MESSAGE(cheat == nullptr, "a sequence went off early, inside ", std::string(a_sequence));
            }
            done = cheat;
        }
        return done;
    }
}

TEST_CASE("every console sequence finishes its own San Andreas cheat")
{
    for (const auto& sequence : kSequences) {
        CAPTURE(sequence.code);
        const auto* cheat = CheatOf(sequence);
        REQUIRE(cheat != nullptr);
        CHECK(cheat->sa != Cheats::SaCheat::None);  // ours never have one
        for (const char c : sequence.buttons) {
            CHECK(IdOf(c) != 0);
            CHECK(LetterOf(IdOf(c)) == c);
        }
        Buttons buttons;
        CHECK(Enter(buttons, sequence.buttons) == cheat);
    }
}

TEST_CASE("no sequence ends another one, so each can be entered whole")
{
    for (const auto& a : kSequences) {
        for (const auto& b : kSequences) {
            if (&a == &b || a.buttons.size() > b.buttons.size()) {
                continue;
            }
            CAPTURE(a.code);
            CAPTURE(b.code);
            CHECK_FALSE(b.buttons.ends_with(a.buttons));
        }
    }
}

TEST_CASE("HESOYAM on the pad: RB, RT, LB, A, left, down, right, up, left, down, right, up")
{
    Buttons buttons;
    const std::uint32_t hesoyam[] = { Pad::kRightShoulder, Pad::kRightTrigger, Pad::kLeftShoulder, Pad::kA, Pad::kLeft, Pad::kDown,
        Pad::kRight, Pad::kUp, Pad::kLeft, Pad::kDown, Pad::kRight, Pad::kUp };
    const Cheats::Cheat* done = nullptr;
    for (const auto id : hesoyam) {
        done = buttons.Press(id);
    }
    REQUIRE(done != nullptr);
    CHECK(done->kind == Cheats::Kind::Health);

    SUBCASE("presses before it don't matter, and the list empties after")
    {
        Buttons more;
        more.Press(Pad::kY);
        more.Press(Pad::kX);
        for (const auto id : hesoyam) {
            done = more.Press(id);
        }
        CHECK(done != nullptr);
        CHECK(more.Press(Pad::kUp) == nullptr);
    }

    SUBCASE("buttons no sequence has are skipped, not counted")
    {
        Buttons skipped;
        for (std::size_t i = 0; i < std::size(hesoyam); ++i) {
            if (i == 5) {
                CHECK(skipped.Press(Pad::kLeftThumb) == nullptr);  // L3 in the middle
            }
            done = skipped.Press(hesoyam[i]);
        }
        CHECK(done != nullptr);
    }
}

TEST_CASE("the jetpack's longer published versions end with the one in the table, so they work too")
{
    for (const std::string_view longer : { "UDLR1234UDLR", "1234UDLR1234UDLR" }) {
        Buttons buttons;
        const auto* done = Enter(buttons, longer.substr(0, longer.size()));
        REQUIRE(done != nullptr);
        CHECK(std::string_view(done->code) == "YECGAA");
    }
    Buttons sixteen;
    const auto* done = Enter(sixteen, "1234UDLR1234UDLR");
    REQUIRE(done != nullptr);
    CHECK(std::string_view(done->code) == "YECGAA");
}
