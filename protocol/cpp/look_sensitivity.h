#pragma once

// CJ's camera follows Skyrim's own look sensitivity: the mouse's counts and the right stick's are scaled by Skyrim's live
// setting over its default before they reach San Andreas, so at Skyrim's default nothing changes, and moving the slider
// in Skyrim's Settings (Controls, Look Sensitivity) turns CJ's camera faster or slower at once.

namespace LookSensitivity
{
    // SkyrimPrefs.ini [Controls], their defaults: what the counts were tuned at.
    constexpr const char* kMouseSetting = "fMouseHeadingSensitivity:Controls";
    constexpr const char* kPadSetting = "fGamepadHeadingSensitivity:Controls";
    constexpr float       kMouseDefault = 0.0125f;
    constexpr float       kPadDefault = 0.6667f;

    // Kept within these, so a stray ini value can neither freeze CJ's camera nor spin it.
    constexpr double kMinScale = 0.1;
    constexpr double kMaxScale = 10.0;

    // a_live over a_default, clamped. 1 when the setting is missing or not a positive number.
    inline double Scale(float a_live, float a_default)
    {
        if (!(a_live > 0.0f) || !(a_default > 0.0f)) {
            return 1.0;
        }
        const double s = static_cast<double>(a_live) / static_cast<double>(a_default);
        return s < kMinScale ? kMinScale : s > kMaxScale ? kMaxScale : s;
    }

    inline double Mouse(float a_live) { return Scale(a_live, kMouseDefault); }
    inline double Pad(float a_live) { return Scale(a_live, kPadDefault); }
}
