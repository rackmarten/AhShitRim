#pragma once

#include <cstdint>

// The spray can's and the fire extinguisher's clouds in Skyrim, as San Andreas draws them
// (protocol/cpp/puff_model.h: its spraycan and extinguisher systems' sprites and numbers, read from the
// player's own San Andreas install and drawn through ModelView), and a puff of the extinguisher's froosh
// where a fire goes out. Main thread only.
namespace AhShitRim::Puffs
{
    enum class Kind
    {
        Spray,
        Hose,
    };

    // A puff of a_kind from a_nozzle along a_dir (a unit vector), Skyrim world units: its cloud keeps coming
    // out while the puffs keep coming.
    void Puff(Kind a_kind, const RE::NiPoint3& a_nozzle, const RE::NiPoint3& a_dir);

    // A fire went out at a_at.
    void PutOut(const RE::NiPoint3& a_at);

    // Once a frame, a_delta seconds on: moves and draws the clouds.
    void Update(float a_delta);

    void Clear();
}
