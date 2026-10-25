// The heat seeker's lock-on marker as San Andreas draws it for a flying vehicle's lock and the heat seeker
// (CWeaponEffects::Render, 0x742CF0, not reversed in gta-reversed; read from gta_sa.exe 1.0 US). Header-only and
// free of game types, so the protocol tests cover it; Skyrim's HUD (SaHud) draws it.
//
// Two of particle.txd's textures, both green (102, 255, 0): lockon, a ring with four ticks, and lockonFire, an
// inner ring of two arcs that turns once every 1.024 s. Each is drawn three times over the target: black at 0.95
// and 1.05 of its size (its outline), then in the marker's colour. San Andreas tints them white while it locks on
// (the texture's own green) and red once locked (its green goes dark red), and keeps the marker one size on the
// screen however far the target is: lockon 1.8 and lockonFire 0.8 of 20 pixels a half, at 640 pixels across.

#pragma once

#include <array>
#include <cstdint>

namespace AhShitRim::LockMarker
{
    inline constexpr float kPi = 3.14159265358979f;

    inline constexpr std::uint32_t kSpinPeriodMs = 1024;  // lockonFire turns once in this, by the game's clock
    inline constexpr float         kHalfSize = 20.0f;      // HUD units (640 across): the size both are scaled from
    inline constexpr float         kRingScale = 1.8f;      // lockon
    inline constexpr float         kInnerScale = 0.8f;     // lockonFire
    inline constexpr float         kOutlineScales[2] = { 0.95f, 1.05f };

    // Colours as the overlay takes them: red in the low byte, alpha in the high.
    inline constexpr std::uint32_t kLocking = 0xFFFFFFFF;  // white: the textures' own green
    inline constexpr std::uint32_t kLocked = 0xFF0000FF;   // red: the green goes dark red (102, 0, 0)
    inline constexpr std::uint32_t kOutline = 0xFF000000;  // black

    constexpr std::uint32_t ColourOf(bool a_locked) { return a_locked ? kLocked : kLocking; }

    // lockonFire's turn at a_ms on the game's clock, in radians: (ms & 1023) of 1024 parts of a full turn.
    constexpr float SpinAngle(std::uint64_t a_ms)
    {
        return static_cast<float>(a_ms & (kSpinPeriodMs - 1)) * (2.0f * kPi / static_cast<float>(kSpinPeriodMs));
    }

    struct Layer
    {
        bool          inner = false;  // lockonFire rather than lockon
        float         halfSize = 0;   // HUD units
        std::uint32_t colour = 0;
        float         rotation = 0;   // radians
    };

    // The six draws in San Andreas's order, bottom first: lockon's two outlines and lockon, then lockonFire's
    // two outlines and lockonFire, turned by SpinAngle(a_ms).
    constexpr std::array<Layer, 6> Layers(bool a_locked, std::uint64_t a_ms)
    {
        std::array<Layer, 6> layers{};
        const float          spin = SpinAngle(a_ms);
        for (int part = 0; part < 2; ++part) {
            const bool  inner = part == 1;
            const float size = kHalfSize * (inner ? kInnerScale : kRingScale);
            const float turn = inner ? spin : 0.0f;
            layers[part * 3 + 0] = { inner, size * kOutlineScales[0], kOutline, turn };
            layers[part * 3 + 1] = { inner, size * kOutlineScales[1], kOutline, turn };
            layers[part * 3 + 2] = { inner, size, ColourOf(a_locked), turn };
        }
        return layers;
    }
}
