#pragma once

#include <cmath>
#include <string_view>

// CJ's camera follows Skyrim's own look sensitivity (Settings, Controls, Look Sensitivity): the mouse's counts and the
// right stick's are scaled by where Skyrim's live setting sits along its slider before they reach San Andreas, so moving
// the slider turns CJ's camera faster or slower at once. The slider's middle is San Andreas's own speed, its bottom a
// quarter of it and its top four times it, in even steps (each tenth of the slider about a third faster). Skyrim's own
// default sits near the bottom of its slider (the mouse's 6% up, the pad's 20%), so the first time AhShitRim runs it
// moves both sliders to the middle, once ([Input] LookReset in AhShitRim.ini), giving the player room either way.

namespace LookSensitivity
{
    // SkyrimPrefs.ini [Controls].
    constexpr const char* kMouseSetting = "fMouseHeadingSensitivity:Controls";
    constexpr const char* kPadSetting = "fGamepadHeadingSensitivity:Controls";
    // Skyrim.ini [Controls]: the slider's ends.
    constexpr const char* kMouseMinSetting = "fMouseHeadingSensitivityMin:Controls";
    constexpr const char* kMouseMaxSetting = "fMouseHeadingSensitivityMax:Controls";
    constexpr const char* kPadMinSetting = "fGamepadHeadingSensitivityMin:Controls";
    constexpr const char* kPadMaxSetting = "fGamepadHeadingSensitivityMax:Controls";

    struct Range
    {
        float min;
        float max;
    };

    // The ends Skyrim SE ships with (its exe's defaults for the settings above), when the live ones are missing.
    constexpr Range kMouseRange{ 0.0100f, 0.0500f };
    constexpr Range kPadRange{ 0.4445f, 1.5556f };
    // Skyrim's own defaults, near the bottom of each slider.
    constexpr float kMouseDefault = 0.0125f;
    constexpr float kPadDefault = 0.6667f;

    // The slider's bottom and top, times San Andreas's speed. Its middle is 1.
    constexpr double kSlowest = 0.25;
    constexpr double kFastest = 4.0;

    // a_live when both ends are positive numbers in order, else a_fallback.
    inline Range Checked(Range a_live, Range a_fallback)
    {
        const bool sane = a_live.min > 0.0f && a_live.max > a_live.min && std::isfinite(a_live.max);
        return sane ? a_live : a_fallback;
    }

    // Where a_live sits along the slider: 0 at its bottom, 1 at its top, clamped (an ini can hold more). The middle when
    // the value is missing or not a positive number.
    inline double Position(float a_live, Range a_range)
    {
        if (!(a_live > 0.0f) || !(a_range.max > a_range.min)) {
            return 0.5;
        }
        const double t = (static_cast<double>(a_live) - a_range.min) / (static_cast<double>(a_range.max) - a_range.min);
        return t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    }

    // The scale for the look counts: kSlowest at the bottom, 1 at the middle, kFastest at the top, evenly in log steps.
    inline double Scale(float a_live, Range a_range)
    {
        return kSlowest * std::pow(kFastest / kSlowest, Position(a_live, a_range));
    }

    // The slider's middle: San Andreas's own speed, where the one-time reset puts it.
    inline float Middle(Range a_range) { return (a_range.min + a_range.max) / 2.0f; }

    inline double Mouse(float a_live) { return Scale(a_live, kMouseRange); }
    inline double Pad(float a_live) { return Scale(a_live, kPadRange); }

    // The one-time reset: due unless [Input] LookReset is already 1.
    inline bool ResetDue(std::string_view a_recorded) { return a_recorded != "1"; }
}
