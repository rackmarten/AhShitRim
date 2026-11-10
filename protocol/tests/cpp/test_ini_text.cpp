#include <doctest/doctest.h>

#include "../../cpp/ini_text.h"

TEST_CASE("ini text: a key the section has is replaced in place")
{
    const std::string text = "[Input]\nKeyLayout=Modern\nLookReset = 0\n\n[Hud]\nCompass=0\n";
    CHECK(IniText::WithValue(text, "Input", "LookReset", "1", "a comment") == "[Input]\nKeyLayout=Modern\nLookReset=1\n\n[Hud]\nCompass=0\n");
}

TEST_CASE("ini text: a new key goes after the section's last setting, with its comment")
{
    const std::string text = "[SanAndreas]\nPath=C:\\Games\\SA\n\n[Input]\n; the layout\nKeyLayout=Modern\n\n[Missions]\nMiscPassed=1\n";
    CHECK(IniText::WithValue(text, "Input", "LookReset", "1", "done once") ==
          "[SanAndreas]\nPath=C:\\Games\\SA\n\n[Input]\n; the layout\nKeyLayout=Modern\n; done once\nLookReset=1\n\n[Missions]\nMiscPassed=1\n");
}

TEST_CASE("ini text: a missing section is added at the end")
{
    CHECK(IniText::WithValue("[SanAndreas]\nPath=x\n\n\n", "Input", "LookReset", "1") == "[SanAndreas]\nPath=x\n\n[Input]\nLookReset=1\n");
    CHECK(IniText::WithValue("", "Input", "LookReset", "1", "c") == "[Input]\n; c\nLookReset=1\n");
    // The same key in another section isn't the one.
    CHECK(IniText::WithValue("[Debug]\nLookReset=0", "Input", "LookReset", "1") == "[Debug]\nLookReset=0\n\n[Input]\nLookReset=1\n");
}

TEST_CASE("ini text: Windows line endings stay, and setting it twice changes nothing more")
{
    const std::string text = "[SanAndreas]\r\nPath=x\r\n\r\n[Input]\r\nKeyLayout=Classic\r\n";
    const auto once = IniText::WithValue(text, "Input", "LookReset", "1", "c");
    CHECK(once == "[SanAndreas]\r\nPath=x\r\n\r\n[Input]\r\nKeyLayout=Classic\r\n; c\r\nLookReset=1\r\n");
    CHECK(IniText::WithValue(once, "Input", "LookReset", "1", "c") == once);
}
