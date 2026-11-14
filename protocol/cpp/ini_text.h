#pragma once

#include <string>
#include <string_view>
#include <vector>

// Setting one key in an ini file's text, the rest left as it was (comments, other sections, the line endings), for the
// plugin's own records in AhShitRim.ini (LookSensitivity's one-time reset) and the look sliders in SkyrimPrefs.ini; and
// reading one back.

namespace IniText
{
    inline std::string_view Trim(std::string_view a_text)
    {
        const auto first = a_text.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) {
            return {};
        }
        return a_text.substr(first, a_text.find_last_not_of(" \t\r") - first + 1);
    }

    // a_key's value in [a_section] of a_text, trimmed; empty when it isn't there.
    inline std::string ValueIn(std::string_view a_text, std::string_view a_section, std::string_view a_key)
    {
        const std::string wanted = "[" + std::string(a_section) + "]";
        bool              inSection = false;
        for (std::size_t start = 0; start < a_text.size();) {
            auto end = a_text.find('\n', start);
            if (end == std::string_view::npos) {
                end = a_text.size();
            }
            const auto line = Trim(a_text.substr(start, end - start));
            start = end + 1;
            if (line.starts_with('[')) {
                inSection = line == wanted;
                continue;
            }
            const auto equals = line.find('=');
            if (inSection && equals != std::string_view::npos && Trim(line.substr(0, equals)) == a_key) {
                return std::string(Trim(line.substr(equals + 1)));
            }
        }
        return {};
    }

    // a_text with a_key=a_value in [a_section]: the key's line replaced where the section has it, else added after the
    // section's last setting (a_comment first, when given), else a new section at the end.
    inline std::string WithValue(std::string_view a_text, std::string_view a_section, std::string_view a_key, std::string_view a_value,
        std::string_view a_comment = {})
    {
        const std::string eol = a_text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
        std::vector<std::string> lines;
        for (std::size_t start = 0; start < a_text.size();) {
            auto end = a_text.find('\n', start);
            if (end == std::string_view::npos) {
                end = a_text.size();
            }
            auto line = a_text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            lines.emplace_back(line);
            start = end + 1;
        }
        const std::string wanted = "[" + std::string(a_section) + "]";
        const std::string setting = std::string(a_key) + "=" + std::string(a_value);
        std::size_t       insertAt = std::string::npos;  // after the wanted section's last setting
        bool              inSection = false;
        bool              replaced = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto line = Trim(lines[i]);
            if (line.starts_with('[')) {
                inSection = line == wanted;
                if (inSection) {
                    insertAt = i + 1;
                }
                continue;
            }
            if (!inSection || line.empty()) {
                continue;
            }
            insertAt = i + 1;
            const auto equals = line.find('=');
            if (equals != std::string_view::npos && Trim(line.substr(0, equals)) == a_key) {
                lines[i] = setting;
                replaced = true;
                break;
            }
        }
        std::vector<std::string> added;
        if (!a_comment.empty()) {
            added.emplace_back("; " + std::string(a_comment));
        }
        added.push_back(setting);
        if (!replaced) {
            if (insertAt != std::string::npos) {
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), added.begin(), added.end());
            } else {
                while (!lines.empty() && Trim(lines.back()).empty()) {
                    lines.pop_back();
                }
                if (!lines.empty()) {
                    lines.emplace_back();
                }
                lines.push_back(wanted);
                lines.insert(lines.end(), added.begin(), added.end());
            }
        }
        std::string out;
        for (const auto& line : lines) {
            out += line + eol;
        }
        return out;
    }
}
