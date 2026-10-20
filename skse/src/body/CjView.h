#pragma once

#include <cstdint>

#include "pose.h"

// Draws CJ in Skyrim from San Andreas's own model (protocol v5). His meshes and textures arrive
// once as ModelChunk messages and are built in memory (nothing converted or written to disk), as
// PerkeleRim's ModelView builds MSC's models: hand-built BSTriShapes cloned from a vanilla shape.
// Skyrim's own skinning isn't used: every frame Show poses each vertex by CJ's bones on the CPU
// (protocol/cpp/body.h) and writes the result into the shapes' dynamic vertex buffers. Main thread
// only.
namespace AhShitRim::CjView
{
    void OnChunk(const std::uint8_t* a_payload, std::uint32_t a_size);

    // The id of the model built and ready to draw, 0 while none is.
    std::uint32_t Model();

    // Draws CJ posed by a_body with his feet at a_feet (Skyrim world units). a_body must belong to
    // Model().
    void Show(const Protocol::BodyState& a_body, const RE::NiPoint3& a_feet);
    void Hide();
}
