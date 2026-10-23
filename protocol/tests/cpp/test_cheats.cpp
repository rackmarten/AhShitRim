#include <doctest/doctest.h>

#include "../../cpp/cheats.h"

#include <algorithm>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

using namespace AhShitRim;
using namespace AhShitRim::Cheats;

namespace
{
    // San Andreas 1.0's cheat keys (CCheat::m_aCheatHashKeys, as gta-reversed lists them), in no particular order.
    const std::set<std::uint32_t> kSanAndreasKeys = { 0x00000000, 0xDE4B237D, 0xB22A28D1, 0x5A783FAE, 0xEECCEA2B, 0x42AF1E28, 0x555FC201, 0x2A845345,
        0xE1EF01EA, 0x771B83FC, 0x5BF12848, 0x44453A17, 0xFCFF1D08, 0xB69E8532, 0x8B828076, 0xDD6ED9E9, 0xA290FD8C, 0x3484B5A7, 0x43DB914E, 0xDBC0DD65,
        0xD08A30FE, 0x37BF1B4E, 0xB5D40866, 0xE63B0D99, 0x675B8945, 0x4987D5EE, 0x2E8F84E8, 0x1A9AA3D6, 0xE842F3BC, 0x0D5C6A4E, 0x74D4FCB1, 0x66516EBC,
        0x4B137E45, 0x78520E33, 0x3A577325, 0xD4966D59, 0x5FD1B49D, 0xA7613F99, 0x1792D871, 0xCBC579DF, 0x4FEDCCFF, 0x44B34866, 0x2EF877DB, 0x2781E797,
        0x2BC1A045, 0xB2AFE368, 0xFA8DD45B, 0x8DED75BD, 0x1A5526BC, 0xA48A770B, 0xB07D3B32, 0x80C1E54B, 0x5DAD0087, 0x7F80B950, 0x6C0FA650, 0xF46F2FA4,
        0x70164385, 0x885D0B50, 0x151BDCB3, 0xADFA640A, 0xE57F96CE, 0x040CF761, 0xE1B33EB9, 0xFEDA77F7, 0x8CA870DD, 0x9A629401, 0xF53EF5A5, 0xF2AA0C1D,
        0xF36345A8, 0x8990D5E1, 0xB7013B1B, 0xCAEC94EE, 0x31F0C3CC, 0xB3B3E72A, 0xC25CDBFF, 0xD5CF4EFF, 0x680416B1, 0xCF5FDA18, 0xF01286E9, 0xA841CC0A,
        0x31EA09CF, 0xE958788A, 0x02C83A7C, 0xE49C3ED4, 0x171BA8CC, 0x86988DAE, 0x2BDD2FA1 };

    std::uint32_t Dik(char a_letter)
    {
        for (std::uint32_t dik = 0; dik < 256; ++dik) {
            if (LetterOf(dik) == a_letter) {
                return dik;
            }
        }
        return 0;
    }

    // Types a_text key by key; the cheat the last key completed, or nullptr.
    const Cheat* TypeAll(Typist& a_typist, std::string_view a_text)
    {
        const Cheat* last = nullptr;
        for (char c : a_text) {
            last = a_typist.Key(Dik(c)).cheat;
        }
        return last;
    }

    const Cheat& Row(std::string_view a_code)
    {
        for (const auto& cheat : kCheats) {
            if (cheat.code == a_code) {
                return cheat;
            }
        }
        FAIL("no row for ", std::string(a_code));
        return kCheats[0];
    }
}

TEST_CASE("cheats: San Andreas's keys of its codes")
{
    // CKeyGen::GetKey over the code backwards: CRC-32's register, not flipped at the end.
    CHECK(CodeKey("HESOYAM") == 0xEECCEA2Bu);
    CHECK(CodeKey("LXGIWYL") == 0xDE4B237Du);
    CHECK(CodeKey("KJKSZPJ") == 0xB22A28D1u);
    CHECK(CodeKey("UZUMYMW") == 0x5A783FAEu);
    CHECK(CodeKey("AIWPRTON") == 0x43DB914Eu);
    CHECK(CodeKey("BAGUVIX") == 0x151BDCB3u);
    CHECK(CodeKey("RIPAZHA") == 0xFA8DD45Bu);
    CHECK(CodeKey("JQNTDMH") == 0u);  // the Rancher's key really is 0
    // The alternative codes share their key.
    CHECK(CodeKey("FULLCLIP") == CodeKey("WANRLTW"));
    CHECK(CodeKey("THUGSARMOURY") == CodeKey("LXGIWYL"));
    const char backwards[] = "MAYOSEH";
    CHECK(KeyOf(backwards, 7) == CodeKey("HESOYAM"));
}

TEST_CASE("cheats: the table")
{
    std::set<std::uint32_t> keys;
    std::set<int>           indices;
    for (const auto& cheat : kCheats) {
        CAPTURE(std::string(cheat.code));
        CHECK(cheat.code.size() >= Typist::kShortest);
        CHECK(cheat.code.size() <= Typist::kLength);
        CHECK(std::all_of(cheat.code.begin(), cheat.code.end(), [](char c) { return c >= 'A' && c <= 'Z'; }));
        CHECK(keys.insert(CodeKey(cheat.code)).second);  // no two share a key
        if (cheat.sa == SaCheat::None) {
            CHECK(kSanAndreasKeys.count(CodeKey(cheat.code)) == 0);  // ours can't be one of San Andreas's
        } else {
            CHECK(kSanAndreasKeys.count(CodeKey(cheat.code)) == 1);  // a real San Andreas code
            CHECK(static_cast<int>(cheat.sa) < kSaCheatCount);
            CHECK(indices.insert(static_cast<int>(cheat.sa)).second);
        }
        if (cheat.kind == Kind::Spawn) {
            CHECK(VehicleKinds::Spawnable(cheat.argument));
        }
        if (cheat.kind == Kind::Weapons) {
            CHECK(cheat.argument >= 1);
            CHECK(cheat.argument <= 3);
        }
        CHECK(ToSanAndreas(cheat.kind) == (cheat.kind == Kind::Health || cheat.kind == Kind::Invincible || cheat.kind == Kind::SaToggle || cheat.kind == Kind::SaOnce));
    }
    CHECK(Row("NOMERCY").kind == Kind::NoMercy);
    CHECK(Row("JUMPJET").argument == 520);  // Hydra
    CHECK(Row("OHDUDE").argument == 425);   // Hunter
    CHECK(Row("YECGAA").argument == VehicleKinds::kJetpack);
    CHECK(Row("AIYPWZQP").argument == VehicleKinds::kParachute);
    CHECK(Find(SaCheat::CarsFly) == &Row("RIPAZHA"));
    CHECK(Find(SaCheat::InfiniteAmmo) == &Row("FULLCLIP"));
    CHECK(Find(SaCheat::None) == nullptr);
    CHECK(Toggles(Kind::SaToggle));
    CHECK(Toggles(Kind::Invincible));
    CHECK(Toggles(Kind::NoMercy));
    CHECK_FALSE(Toggles(Kind::Health));
    CHECK_FALSE(Toggles(Kind::Spawn));
}

TEST_CASE("cheats: letters by their keys' places")
{
    CHECK(LetterOf(Input::kDikW) == 'W');
    CHECK(LetterOf(Input::kDikA) == 'A');
    CHECK(LetterOf(Input::kDikM) == 'M');
    CHECK(LetterOf(Input::kDikQ) == 'Q');
    CHECK(LetterOf(Input::kDikX) == 'X');
    CHECK(LetterOf(0x19) == 'P');
    CHECK(LetterOf(0x26) == 'L');
    CHECK(LetterOf(0x2C) == 'Z');
    CHECK(LetterOf(Input::kDikEscape) == 0);
    CHECK(LetterOf(Input::kDikSpace) == 0);
    CHECK(LetterOf(0x1A) == 0);  // [
    CHECK(LetterOf(0x27) == 0);  // ;
    CHECK(LetterOf(0x33) == 0);  // ,
    std::set<char> letters;
    for (std::uint32_t dik = 0; dik < 256; ++dik) {
        if (const char c = LetterOf(dik)) {
            CHECK(letters.insert(c).second);
        }
    }
    CHECK(letters.size() == 26);
}

TEST_CASE("cheats: typing completes a cheat on its last letter")
{
    Typist typist;
    CHECK(TypeAll(typist, "HESOYA") == nullptr);
    CHECK(TypeAll(typist, "M") == &Row("HESOYAM"));
    CHECK(typist.Count() == 0);  // a match empties the string, as in San Andreas
    // Walking about first doesn't matter: only the newest letters count.
    CHECK(TypeAll(typist, "WWWASDDWEFHESOYAM") == &Row("HESOYAM"));
    // Nor does a wrong letter before it, but one inside breaks it.
    CHECK(TypeAll(typist, "HESXOYAM") == nullptr);
    CHECK(TypeAll(typist, "BAGUVIX") == &Row("BAGUVIX"));
    // San Andreas's alternative codes work by their key.
    CHECK(TypeAll(typist, "FULLCLIP") == &Row("FULLCLIP"));
    CHECK(TypeAll(typist, "WANRLTW") == &Row("FULLCLIP"));
    CHECK(TypeAll(typist, "THUGSARMOURY") == &Row("LXGIWYL"));
    CHECK(TypeAll(typist, "NOMERCY") == &Row("NOMERCY"));
    // Keys that aren't letters go nowhere.
    CHECK(typist.Key(Input::kDikSpace).cheat == nullptr);
    CHECK(typist.Count() == 0);
}

TEST_CASE("cheats: the string keeps San Andreas's 29 newest letters")
{
    Typist typist;
    CHECK(TypeAll(typist, std::string(100, 'W') + "JUMPJE") == nullptr);
    CHECK(typist.Count() == Typist::kLength);
    CHECK(TypeAll(typist, "T") == &Row("JUMPJET"));
}

TEST_CASE("cheats: M finishing a cheat keeps Skyrim's map shut")
{
    Typist typist;
    TypeAll(typist, "HESOYA");
    const auto press = typist.Key(Input::kDikM);
    CHECK(press.cheat == &Row("HESOYAM"));
    CHECK(press.swallow);

    // M that finishes nothing opens the map as always.
    const auto lone = typist.Key(Input::kDikM);
    CHECK(lone.cheat == nullptr);
    CHECK_FALSE(lone.swallow);
    TypeAll(typist, "HESOYAX");
    CHECK_FALSE(typist.Key(Input::kDikM).swallow);

    // Another letter finishing a cheat isn't Skyrim's to begin with.
    TypeAll(typist, "BAGUVI");
    const auto x = typist.Key(Input::kDikX);
    CHECK(x.cheat == &Row("BAGUVIX"));
    CHECK_FALSE(x.swallow);

    // M inside a code is just a letter until the code is done.
    TypeAll(typist, "UZU");
    const auto inside = typist.Key(Input::kDikM);
    CHECK(inside.cheat == nullptr);
    CHECK_FALSE(inside.swallow);
    CHECK(TypeAll(typist, "YMW") == &Row("UZUMYMW"));
}

TEST_CASE("cheats: switches")
{
    Switches on;
    const auto& flying = Row("RIPAZHA");
    const auto& ammo = Row("FULLCLIP");
    CHECK_FALSE(on.Any());
    CHECK(on.Flip(flying));
    CHECK(on.On(flying));
    CHECK_FALSE(on.On(ammo));
    CHECK(on.Flip(ammo));
    std::vector<std::string_view> seen;
    on.ForEachOn([&](const Cheat& a_cheat) { seen.push_back(a_cheat.code); });
    CHECK(seen == std::vector<std::string_view>{ "FULLCLIP", "RIPAZHA" });  // kCheats order
    CHECK_FALSE(on.Flip(flying));
    CHECK(on.Any());
    on.Set(ammo, false);
    CHECK_FALSE(on.Any());
    on.Set(ammo, true);
    on.Clear();
    CHECK_FALSE(on.On(ammo));
}

TEST_CASE("cheats: the command for San Andreas")
{
    const auto command = CommandFor(Row("RIPAZHA"), true);
    CHECK(command.key == CodeKey("RIPAZHA"));
    CHECK(command.cheat == static_cast<std::uint8_t>(SaCheat::CarsFly));
    CHECK(command.cheat == 48);  // plugin-sdk's CHEAT_CARS_FLY
    CHECK(command.flags == Protocol::kCheatOn);
    CHECK(CommandFor(Row("RIPAZHA"), false).flags == 0);
    CHECK(CommandFor(Row("HESOYAM")).cheat == 3);    // CHEAT_HEALTH_ARMOR_250K
    CHECK(CommandFor(Row("BAGUVIX")).cheat == 61);   // CHEAT_INFINITE_HEALTH
    CHECK(CommandFor(Row("FULLCLIP")).cheat == 72);  // CHEAT_INFINITE_AMMO
}
