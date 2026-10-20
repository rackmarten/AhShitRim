#pragma once

// AhShitRim.ini next to the plugin (Skyrim runs in its own folder), written by
// scripts/deploy-local.ps1: [SanAndreas] Path, [Input] KeyLayout.
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
}
