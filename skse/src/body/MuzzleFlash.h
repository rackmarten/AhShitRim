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
}
