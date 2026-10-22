#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Flat 2D quads drawn over Skyrim's finished frame: a hook on its swap chain's Present draws them
// with DirectXTK's SpriteBatch, Skyrim's Direct3D state put back afterwards. Compiled without
// CommonLib's PCH, since it needs the real d3d11.h, so it only takes plain types.
namespace AhShitRim::Overlay
{
    struct Image
    {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;  // width * height * 4, top row first
    };

    // Texture 0 is one white texel, for solid boxes; the images given to SetImages follow from 1.
    inline constexpr std::uint32_t kWhite = 0;

    struct Quad
    {
        std::uint32_t texture = kWhite;
        float         x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // back buffer pixels
        float         u0 = 0, v0 = 0, u1 = 1, v1 = 1;  // 0..1 across the texture
        std::uint32_t colour = 0xFFFFFFFF;              // multiplies the texture; red in the low byte, alpha in the high
    };

    // Hooks a_swapChain's Present (an IDXGISwapChain*); false when there is none.
    bool Install(void* a_swapChain);

    // The textures quads draw from, made on the next frame (main thread, once at startup).
    void SetImages(std::vector<Image> a_images);

    // The quads drawn over every frame from now, in order. They stop being drawn when no new ones
    // come for a quarter of a second, so nothing hangs on screen once the game stops updating them
    // (a pausing menu).
    void Submit(std::vector<Quad> a_quads);

    // The back buffer's size at the last Present; false before the first one.
    bool ScreenSize(float& a_width, float& a_height);

    // Saves the next frame as it's presented, with Skyrim's HUD and menus and these quads on it
    // (Skyrim's own screenshot is taken before any of them are drawn), as a PNG in a_folder (ANSI, as
    // AhShitRim.ini has it; a relative one is under Skyrim's folder), or in Documents\My Games\
    // AhShitRim\Screenshots when it's empty. The file's path goes to the log.
    void Screenshot(std::string a_folder);

    // Whether a screenshot was saved since the last call.
    bool ScreenshotSaved();
}
