#pragma once

#include "ini_text.h"

// AhShitRim.ini next to the plugin (Skyrim runs in its own folder), written by
// scripts/deploy-local.ps1: [SanAndreas] Path, [Input] KeyLayout; the launcher also writes
// [Screenshots] Dir. Players may add their own settings, such as [Missions] MiscPassed. The plugin records its own
// one-time changes there too ([Input] LookReset), which both writers keep.
namespace AhShitRim::Ini
{
    inline constexpr const char* kPath = "Data/SKSE/Plugins/AhShitRim.ini";

    // a_key's value in [a_section], trimmed; empty when the file, the section or the key is missing.
    inline std::string Value(std::string_view a_section, std::string_view a_key)
    {
        auto trim = [](const std::string& a_text) {
            const auto first = a_text.find_first_not_of(" \t\r");
            const auto last = a_text.find_last_not_of(" \t\r");
            return first == std::string::npos ? std::string{} : a_text.substr(first, last - first + 1);
        };
        const auto    wanted = "[" + std::string(a_section) + "]";
        std::ifstream file(kPath);
        std::string   line, section;
        while (std::getline(file, line)) {
            line = trim(line);
            const auto equals = line.find('=');
            if (line.starts_with('[')) {
                section = line;
            } else if (section == wanted && equals != std::string::npos && trim(line.substr(0, equals)) == a_key) {
                return trim(line.substr(equals + 1));
            }
        }
        return {};
    }

    // Sets a_key=a_value in [a_section], the rest of the file as it was (IniText::WithValue). False when it can't be written.
    inline bool Set(std::string_view a_section, std::string_view a_key, std::string_view a_value, std::string_view a_comment = {})
    {
        std::string text;
        if (std::ifstream in(kPath, std::ios::binary); in) {
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const auto    changed = IniText::WithValue(text, a_section, a_key, a_value, a_comment);
        std::ofstream out(kPath, std::ios::binary | std::ios::trunc);
        out << changed;
        return static_cast<bool>(out.flush());
    }
}
