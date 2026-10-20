#pragma once

#include <string>

#include "skin_model.h"

struct RwTexture;

namespace AhShitRim::RwTextures
{
    // The top level of a RenderWare texture as RGBA, read from its Direct3D 9 texture (the raster's
    // Direct3D part); empty, with a_why set, when it can't be read. Game thread only.
    SkinModel::TextureIn Read(RwTexture* a_texture, std::string& a_why);
}
