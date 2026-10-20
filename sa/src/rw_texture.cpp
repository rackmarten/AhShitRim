#include "rw_texture.h"

#include "texture_decode.h"

#include <plugin.h>

#include <d3d9.h>

#include <format>

namespace AhShitRim::RwTextures
{
    namespace
    {
        constexpr std::uintptr_t kRasterExtOffset = 0xB4E9E0;  // _RwD3D9RasterExtOffset: where a raster's Direct3D part starts

        // The start of RenderWare's _rwD3D9RasterExt (gta-reversed's WindowedMode.cpp).
        struct RasterExt
        {
            IDirect3DTexture9* texture;
            PALETTEENTRY*      palette;  // 256 entries, alpha in peFlags
        };
    }

    SkinModel::TextureIn Read(RwTexture* a_texture, std::string& a_why)
    {
        RwRaster* raster = a_texture ? a_texture->raster : nullptr;
        if (!raster) {
            a_why = "no raster";
            return {};
        }
        const auto* ext = reinterpret_cast<const RasterExt*>(reinterpret_cast<std::uint8_t*>(raster) + *reinterpret_cast<std::int32_t*>(kRasterExtOffset));
        IDirect3DTexture9* texture = ext->texture;
        D3DSURFACE_DESC    desc{};
        if (!texture || FAILED(texture->GetLevelDesc(0, &desc))) {
            a_why = "no Direct3D texture";
            return {};
        }
        if (!TextureDecode::Supported(desc.Format) || desc.Width > Protocol::kModelTextureSizeMax || desc.Height > Protocol::kModelTextureSizeMax) {
            a_why = std::format("format {} {}x{} not supported", static_cast<std::uint32_t>(desc.Format), desc.Width, desc.Height);
            return {};
        }
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY))) {
            a_why = std::format("could not lock it (pool {})", static_cast<int>(desc.Pool));
            return {};
        }
        SkinModel::TextureIn out;
        out.width = desc.Width;
        out.height = desc.Height;
        out.rgba = TextureDecode::ToRgba(desc.Format, desc.Width, desc.Height, static_cast<const std::uint8_t*>(locked.pBits),
            static_cast<std::uint32_t>(locked.Pitch), reinterpret_cast<const std::uint8_t*>(ext->palette));
        texture->UnlockRect(0);
        if (out.rgba.empty()) {
            a_why = std::format("format {} did not decode", static_cast<std::uint32_t>(desc.Format));
            return {};
        }
        return out;
    }
}
