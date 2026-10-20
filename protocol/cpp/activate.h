// What E activates while CJ has the player, the game-free half: a ray from CJ along San Andreas's
// camera against each activatable reference's box, the line of sight to it, and the help box prompt
// from Skyrim's own activate text. Header-only and free of game types, so the protocol tests cover
// it; the SKSE plugin feeds it the references around the player (skse/src/player/Activation.cpp).
//
// Skyrim units and axes: X east, Y north, Z up; heading 0 = north, clockwise; pitch positive looks
// down (as Pose::HeadingOf and Pose::PitchOf).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pose.h"

namespace AhShitRim::Activate
{
    using Pose::Vec3;

    // The ray starts this far above CJ's feet (his chest, about 1.4 m).
    inline constexpr float kRayHeight = 100.0f;
    // Skyrim's fActivatePickLength when the setting can't be read; reach along the ray, units.
    inline constexpr float kDefaultReach = 180.0f;
    // A wall hit this much short of the target's box still lets it through (the box is loose).
    inline constexpr float kSightTolerance = 16.0f;

    inline Vec3 Add(const Vec3& a_a, const Vec3& a_b) { return { a_a.x + a_b.x, a_a.y + a_b.y, a_a.z + a_b.z }; }
    inline Vec3 Sub(const Vec3& a_a, const Vec3& a_b) { return { a_a.x - a_b.x, a_a.y - a_b.y, a_a.z - a_b.z }; }
    inline Vec3 Scale(const Vec3& a_v, float a_s) { return { a_v.x * a_s, a_v.y * a_s, a_v.z * a_s }; }
    inline float Dot(const Vec3& a_a, const Vec3& a_b) { return a_a.x * a_b.x + a_a.y * a_b.y + a_a.z * a_b.z; }

    // Unit direction of a Skyrim heading and pitch: the inverse of Pose::HeadingOf and PitchOf.
    inline Vec3 Direction(float a_heading, float a_pitch)
    {
        const float flat = std::cos(a_pitch);
        return { std::sin(a_heading) * flat, std::cos(a_heading) * flat, -std::sin(a_pitch) };
    }

    // An oriented box in Skyrim world space.
    struct Box
    {
        Vec3 center;
        Vec3 axes[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };  // unit, world
        Vec3 half;                                                                            // half extents along axes
    };

    // A reference's box from its bounds in model space (a_min, a_max) and its 3D's world transform:
    // world = rotate * (scale * local) + translate, rotate as NiMatrix3's entry[row][column].
    inline Box BoxOf(const Vec3& a_min, const Vec3& a_max, const float (&a_rotate)[3][3], const Vec3& a_translate, float a_scale)
    {
        Box        box;
        const Vec3 mid = Scale(Add(a_min, a_max), 0.5f * a_scale);
        for (int i = 0; i < 3; ++i) {
            box.axes[i] = { a_rotate[0][i], a_rotate[1][i], a_rotate[2][i] };  // model axis i in the world
        }
        box.center = Add(a_translate, Add(Add(Scale(box.axes[0], mid.x), Scale(box.axes[1], mid.y)), Scale(box.axes[2], mid.z)));
        box.half = { std::abs(a_max.x - a_min.x) * 0.5f * a_scale, std::abs(a_max.y - a_min.y) * 0.5f * a_scale,
            std::abs(a_max.z - a_min.z) * 0.5f * a_scale };
        return box;
    }

    // A person standing at a_feet facing a_heading, when their bounds don't say: about a metre across
    // and two high, scaled.
    inline Box StandingBox(const Vec3& a_feet, float a_heading, float a_scale)
    {
        Box         box;
        const float s = std::sin(a_heading), c = std::cos(a_heading);
        box.axes[0] = { c, -s, 0.0f };  // their right
        box.axes[1] = { s, c, 0.0f };   // where they face
        box.half = { 30.0f * a_scale, 30.0f * a_scale, 64.0f * a_scale };
        box.center = { a_feet.x, a_feet.y, a_feet.z + box.half.z };
        return box;
    }

    // A box round a bounding sphere, for a reference with no usable bounds.
    inline Box SphereBox(const Vec3& a_center, float a_radius)
    {
        Box box;
        box.center = a_center;
        box.half = { a_radius, a_radius, a_radius };
        return box;
    }

    // Whether a box is worth testing: every extent finite and not all of them flat.
    inline bool Usable(const Box& a_box)
    {
        const float h[3] = { a_box.half.x, a_box.half.y, a_box.half.z };
        int         flat = 0;
        for (float v : h) {
            if (!std::isfinite(v) || v < 0.0f) {
                return false;
            }
            flat += v < 1.0f;
        }
        return flat < 2 && std::isfinite(a_box.center.x) && std::isfinite(a_box.center.y) && std::isfinite(a_box.center.z);
    }

    // Distance along a unit ray to where it enters a box (0 when it starts inside), or nothing when it
    // misses or the box is behind it.
    inline std::optional<float> Hit(const Vec3& a_origin, const Vec3& a_dir, const Box& a_box)
    {
        const Vec3  rel = Sub(a_origin, a_box.center);
        const float half[3] = { a_box.half.x, a_box.half.y, a_box.half.z };
        float       enter = 0.0f, leave = std::numeric_limits<float>::infinity();
        for (int i = 0; i < 3; ++i) {
            const float o = Dot(rel, a_box.axes[i]);
            const float d = Dot(a_dir, a_box.axes[i]);
            if (std::abs(d) < 1e-6f) {
                if (std::abs(o) > half[i]) {
                    return std::nullopt;  // parallel to this slab and outside it
                }
                continue;
            }
            float t0 = (-half[i] - o) / d, t1 = (half[i] - o) / d;
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            enter = (std::max)(enter, t0);
            leave = (std::min)(leave, t1);
            if (enter > leave) {
                return std::nullopt;
            }
        }
        return enter;
    }

    struct Candidate
    {
        Box           box;
        std::uint32_t id = 0;  // the reference's form id, for the caller
    };

    struct Picked
    {
        std::size_t index = 0;
        float       distance = 0.0f;  // along the ray to the box
    };

    // The candidate the ray enters first within a_reach.
    inline std::optional<Picked> Pick(const Vec3& a_origin, const Vec3& a_dir, float a_reach, const std::vector<Candidate>& a_candidates)
    {
        std::optional<Picked> best;
        for (std::size_t i = 0; i < a_candidates.size(); ++i) {
            const auto t = Hit(a_origin, a_dir, a_candidates[i].box);
            if (t && *t <= a_reach && (!best || *t < best->distance)) {
                best = Picked{ i, *t };
            }
        }
        return best;
    }

    // Whether CJ sees the target: the line of sight ray along the pick ray met nothing (a_hit false),
    // met the target itself, or met something no nearer than the target's box (less a tolerance:
    // the box is loose, and a sight ray passes through clutter and people to whatever is behind them).
    inline bool InSight(bool a_hit, float a_hitDistance, bool a_hitIsTarget, float a_targetDistance)
    {
        return !a_hit || a_hitIsTarget || a_hitDistance + kSightTolerance >= a_targetDistance;
    }

    // The help box prompt from Skyrim's activate text ("Open\nBreezehome", sometimes with font tags
    // around a word, "<font color='#FF0000'>Steal</font>"): tags dropped, lines joined by spaces, the
    // key in front ("E: Open Breezehome"). a_name stands in when there is no text; empty when neither.
    inline std::string Prompt(std::string_view a_activateText, std::string_view a_name, std::string_view a_key = "E")
    {
        std::string text;
        bool        inTag = false, space = false;
        for (const char c : a_activateText.empty() ? a_name : a_activateText) {
            if (c == '<') {
                inTag = true;
            } else if (c == '>' && inTag) {
                inTag = false;
            } else if (!inTag) {
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
                    space = !text.empty();
                } else {
                    if (space) {
                        text += ' ';
                        space = false;
                    }
                    text += c;
                }
            }
        }
        if (text.empty()) {
            return {};
        }
        return std::string(a_key) + ": " + text;
    }
}
