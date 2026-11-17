// San Andreas's colours as Skyrim's lighting expects them. San Andreas's vehicle textures run up to pure white, which its
// own lighting (an ambient and one sun, together never past full) keeps in range. Skyrim's lighting is brighter (HDR:
// the sun or the moon and the sky's directional ambient add up past full, then the picture is tone mapped), so its own
// art stays darker. A San Andreas white drawn there as it is reads as a lit sign at night (the Sandking's blank number
// panels and its plate: three times the moonlit road beside them, cyan white in the moonlight) and blows out to 255 in
// the sun. ToSkyrim rolls off the light end, by brightness, so paint, grime, tyres and saturated colours keep theirs.
// Header-only and free of game types, so the protocol tests cover it.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace AhShitRim::Albedo
{
    struct Curve
    {
        float knee = 0.5f;    // luma (0 to 1) up to which a colour is left as it is
        float white = 0.6f;   // what pure white becomes; between the knee and white the luma is brought down in a line
    };

    // A colour's luma (Rec. 709 weights on the texture's own values, 0 to 1).
    inline float Luma(float a_r, float a_g, float a_b) { return 0.2126f * a_r + 0.7152f * a_g + 0.0722f * a_b; }

    // The luma a_luma becomes: unchanged up to the knee, then from the knee up to full brought into knee to white.
    inline float Rolled(float a_luma, const Curve& a_curve)
    {
        if (a_luma <= a_curve.knee || a_curve.knee >= 1.0f) {
            return a_luma;
        }
        return a_curve.knee + (a_luma - a_curve.knee) * (a_curve.white - a_curve.knee) / (1.0f - a_curve.knee);
    }

    // Rolls a_pixels RGBA pixels into Skyrim's range in place: each one scaled as a whole (its hue and saturation kept),
    // its alpha untouched.
    inline void ToSkyrim(std::uint8_t* a_rgba, std::size_t a_pixels, const Curve& a_curve = {})
    {
        for (std::size_t i = 0; i < a_pixels; i++) {
            std::uint8_t* p = a_rgba + i * 4;
            const float   luma = Luma(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
            if (luma <= a_curve.knee) {
                continue;
            }
            const float scale = Rolled(luma, a_curve) / luma;
            for (int c = 0; c < 3; c++) {
                p[c] = static_cast<std::uint8_t>(std::clamp(std::lround(p[c] * scale), 0L, 255L));
            }
        }
    }
}
