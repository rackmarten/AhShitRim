// The pad's gun lock-on (docs/DESIGN.md, Controller, Aiming and lock-on): LT held with a gun over the shoulder locks on
// to a target as San Andreas's R1 does on a console, which San Andreas can't do here, its world having nobody in it.
// Skyrim picks the target and steers San Andreas's camera onto it with mouse counts, a closed loop each frame, so San
// Andreas's own mouse sensitivity doesn't matter. The d-pad shifts the lock to the next target on that side (San
// Andreas's L2 and R2 while targeting); the right stick pushed hard for a moment drops it to free aim. Header-only and
// free of game types, so the protocol tests cover it; skse/src/player/LockOn.cpp feeds it.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "melee.h"

namespace AhShitRim::GunLock
{
    inline constexpr float kRange = 45.0f;      // metres: nobody farther is locked on to
    inline constexpr float kRadius = 0.45f;     // screen heights from the crosshair a target is picked within
    inline constexpr float kGain = 9.0f;        // of the offset closed a second (about a ninth of it a frame at 60 fps)
    inline constexpr float kCountsPerScreen = 500.0f;  // mouse counts that turn San Andreas's camera about a screen width (its default sensitivity)
    inline constexpr float kMaxCounts = 2400.0f;       // counts a second at most, so a lock never whips the camera round
    inline constexpr float kBreakStick = 0.8f;   // the right stick this far over...
    inline constexpr float kBreakSeconds = 0.25f;  // ...this long drops the lock

    // One actor that could be locked on to, as it shows through the camera.
    struct Candidate
    {
        std::uint32_t    id = 0;
        Melee::OnScreen  screen;      // where its chest shows, 0..1 across and down
        bool             hostile = false;  // fights the player or is hostile to it
        bool             inSight = true;   // a clear line from the player
    };

    // Screen heights from the crosshair (a_x, a_y) on a screen a_aspect wide.
    inline float Off(const Melee::OnScreen& a_on, float a_x, float a_y, float a_aspect)
    {
        const float aspect = std::isfinite(a_aspect) && a_aspect > 0.1f ? a_aspect : 16.0f / 9.0f;
        return std::hypot((a_on.x - a_x) * aspect, a_on.y - a_y);
    }

    inline bool Lockable(const Candidate& a_c, float a_x, float a_y, float a_aspect)
    {
        return a_c.screen.visible && a_c.inSight && a_c.screen.distance <= kRange && std::isfinite(a_c.screen.x) && std::isfinite(a_c.screen.y) &&
               Off(a_c.screen, a_x, a_y, a_aspect) <= kRadius;
    }

    // The best target as LT goes down: someone hostile first, then the one nearest the crosshair. Index into a_all,
    // or a_all.size() for nobody.
    inline std::size_t Pick(std::span<const Candidate> a_all, float a_x, float a_y, float a_aspect)
    {
        std::size_t best = a_all.size();
        for (std::size_t i = 0; i < a_all.size(); i++) {
            const auto& c = a_all[i];
            if (!Lockable(c, a_x, a_y, a_aspect)) {
                continue;
            }
            if (best == a_all.size() || (c.hostile && !a_all[best].hostile) ||
                (c.hostile == a_all[best].hostile && Off(c.screen, a_x, a_y, a_aspect) < Off(a_all[best].screen, a_x, a_y, a_aspect))) {
                best = i;
            }
        }
        return best;
    }

    inline constexpr float kShiftReach = 0.5f;  // screen widths past either edge the d-pad's shift still finds someone

    // The next target to one side of a_current (a_side -1 left, 1 right): the nearest across the screen on that side,
    // in range and sight, on screen or up to kShiftReach past its edge (the lock has turned the camera off the others).
    // a_current when there is none.
    inline std::size_t Shift(std::span<const Candidate> a_all, std::size_t a_current, int a_side)
    {
        if (a_current >= a_all.size() || a_side == 0) {
            return a_current;
        }
        const float from = a_all[a_current].screen.x;
        std::size_t best = a_current;
        float       bestGap = 0.0f;
        for (std::size_t i = 0; i < a_all.size(); i++) {
            const auto& c = a_all[i];
            if (i == a_current || !c.screen.visible || !c.inSight || c.screen.distance > kRange || c.screen.x < -kShiftReach || c.screen.x > 1.0f + kShiftReach) {
                continue;
            }
            const float gap = (c.screen.x - from) * static_cast<float>(a_side);
            if (gap > 0.0f && (best == a_current || gap < bestGap)) {
                best = i;
                bestGap = gap;
            }
        }
        return best;
    }

    // The mouse counts this frame that turn San Andreas's camera toward the target at (a_tx, a_ty) from the crosshair at
    // (a_x, a_y), screen fractions, over a_seconds. y as DirectInput's, down positive (a target below the crosshair
    // pulls the camera down).
    struct Counts
    {
        double x = 0.0, y = 0.0;
    };
    inline Counts Steer(float a_tx, float a_ty, float a_x, float a_y, float a_aspect, float a_seconds)
    {
        const float aspect = std::isfinite(a_aspect) && a_aspect > 0.1f ? a_aspect : 16.0f / 9.0f;
        const float share = (std::min)(kGain * a_seconds, 1.0f);
        float       dx = (a_tx - a_x) * kCountsPerScreen * share;
        float       dy = (a_ty - a_y) / aspect * kCountsPerScreen * share;  // a screen height is 1/aspect of a width's turn
        const float cap = kMaxCounts * a_seconds;
        const float len = std::hypot(dx, dy);
        if (len > cap && len > 0.0f) {
            dx *= cap / len;
            dy *= cap / len;
        }
        return { dx, dy };
    }

    inline constexpr float kGrace = 0.5f;  // seconds a locked target may be out of sight (a post, someone passing) before the lock moves on

    // Keeps a lock through a moment out of sight, so it doesn't flicker between the target and nobody.
    class Grace
    {
    public:
        // a_lockable: the target is in range, in front and in sight this frame. Whether the lock stays on it.
        bool Keep(bool a_lockable, float a_seconds)
        {
            if (a_lockable) {
                _lost = 0.0f;
                return true;
            }
            _lost += a_seconds;
            return _lost <= kGrace;
        }
        void Reset() { _lost = 0.0f; }

    private:
        float _lost = 0.0f;
    };

    // Holds the right stick's push: true once it has been kBreakStick over for kBreakSeconds.
    class Breaker
    {
    public:
        bool Update(float a_rx, float a_ry, float a_seconds)
        {
            if (std::hypot(a_rx, a_ry) >= kBreakStick) {
                _held += a_seconds;
            } else {
                _held = 0.0f;
            }
            return _held >= kBreakSeconds;
        }
        void Reset() { _held = 0.0f; }

    private:
        float _held = 0.0f;
    };
}
