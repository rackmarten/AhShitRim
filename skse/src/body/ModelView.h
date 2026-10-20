#pragma once

#include <cstdint>

#include "pose.h"

// Draws San Andreas's models in Skyrim: CJ (protocol v5) and the vehicles (v6). Their meshes and
// textures arrive once as ModelChunk messages and are built in memory (nothing converted or written
// to disk), as PerkeleRim's ModelView builds MSC's models: hand-built BSTriShapes cloned from a
// vanilla shape. Skyrim's own skinning isn't used: Show poses each vertex by the model's bones on the
// CPU (protocol/cpp/body.h; a vehicle's bones are its parts) and writes the result into the shapes'
// dynamic vertex buffers. Main thread only.
namespace AhShitRim::ModelView
{
    void OnChunk(const std::uint8_t* a_payload, std::uint32_t a_size);

    // Whether model a_model is built and ready to draw.
    bool Has(std::uint32_t a_model);

    // Draws model a_pose.model posed by a_pose with its origin (CJ's feet, a vehicle's position) at
    // a_origin (Skyrim world units), this frame. False when that model isn't built.
    bool Show(const Protocol::BodyState& a_pose, const RE::NiPoint3& a_origin);

    // Once a frame after the Shows: hides every model not shown this frame.
    void EndFrame();
    void HideAll();
}
