#pragma once

#include <cstdint>

// The flash of a shot (protocol v12): a glow made of three crossed quads (protocol/cpp/flash_model.h) at
// the muzzle for a few frames, drawn through ModelView, and a Skyrim point light there that lights CJ,
// the weapon and the ground around. Main thread only.
namespace AhShitRim::MuzzleFlash
{
    // [Weapons] MuzzleLight in AhShitRim.ini: whether the shots also light the scene (on by default).
    void Configure(bool a_light);

    // A shot left hand a_hand's muzzle (0 right, 1 left).
    void Fire(std::uint32_t a_hand);

    // Every frame for each hand the weapon is drawn in: shows that hand's flash while it lasts, a_feet being CJ's
    // feet in world units, a_muzzle the muzzle relative to them and a_rotation the weapon's own rotation
    // (unit quaternion x, y, z, w). a_delta is the frame's seconds.
    void Show(std::uint32_t a_hand, const RE::NiPoint3& a_feet, const float (&a_muzzle)[3], const float (&a_rotation)[4], float a_delta);

    // Once a frame after the Shows: puts the light out when no flash was shown.
    void EndFrame();

    // Back to Skyrim's own player: no flash lasts.
    void Reset();

    // The test kit (debug): hand a_hand's flash as last shown (shown: in the last frame drawn), its model's id
    // (ModelView::Node finds it while shown) and the scene light shared by both hands.
    struct DebugFlash
    {
        bool              shown = false;
        float             left = 0.0f;  // seconds of it still to come
        float             strength = 0.0f;
        RE::NiPoint3      at;           // the muzzle it was put at, Skyrim's world
        std::uint32_t     model = 0;
        std::uint32_t     fired = 0;    // shots it has flashed for since the plugin loaded
        RE::NiPointLight* light = nullptr;
    };
    DebugFlash Debug(std::uint32_t a_hand);

    // The test kit (debug): while held, a flash that shows doesn't fade or run out (its seconds stop counting down), so a
    // test can pause on it however long its pause takes to arrive. Let go by the next DebugHold(false) or Reset.
    void DebugHold(bool a_hold);
}
