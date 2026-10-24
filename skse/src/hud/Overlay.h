#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Flat 2D quads drawn over Skyrim's finished frame: a hook on its swap chain's Present draws them
// with DirectXTK's SpriteBatch (and shapes of any four corners with its PrimitiveBatch), Skyrim's
// Direct3D state put back afterwards. Compiled without
// CommonLib's PCH, since it needs the real d3d11.h, so it only takes plain types.
namespace AhShitRim::Overlay
{
    struct Image
    {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;  // width * height * 4, top row first
        std::uint32_t             border = 0;  // a shape's colour past the image's edges (as Quad::colour); 0 = its edge texels
    };

    // Texture 0 is one white texel, for solid boxes; the images given to SetImages follow from 1.
    inline constexpr std::uint32_t kWhite = 0;

    struct Quad
    {
        std::uint32_t texture = kWhite;
        float         x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // back buffer pixels
        float         u0 = 0, v0 = 0, u1 = 1, v1 = 1;  // 0..1 across the texture
        std::uint32_t colour = 0xFFFFFFFF;              // multiplies the texture; red in the low byte, alpha in the high
        std::int32_t  shape = -1;                       // 0 and up: the Shape of that index (Submit) is drawn here instead of the box
    };

    // A quad of any shape: a turned sprite, a triangle (two corners the same), a slice of the radar's map.
    struct Corner
    {
        float x = 0, y = 0;  // back buffer pixels
        float u = 0, v = 0;  // texture coordinates; past 0..1 the image's border colour, if it has one
    };

    struct Shape
    {
        std::uint32_t texture = kWhite;
        Corner        corners[4];  // in order round it
        std::uint32_t colour = 0xFFFFFFFF;
    };

    // Hooks a_swapChain's Present (an IDXGISwapChain*); false when there is none.
    bool Install(void* a_swapChain);

    // The textures quads draw from, made on the next frame (main thread, once at startup).
    void SetImages(std::vector<Image> a_images);

    // The quads drawn over every frame from now, in order. They stop being drawn when no new ones
    // come for a quarter of a second, so nothing hangs on screen once the game stops updating them
    // (a pausing menu). a_shapes are the shapes the quads point to.
    void Submit(std::vector<Quad> a_quads, std::vector<Shape> a_shapes = {});

    // The back buffer's size at the last Present; false before the first one.
    bool ScreenSize(float& a_width, float& a_height);

    // Saves the next frame as it's presented, with Skyrim's HUD and menus and these quads on it
    // (Skyrim's own screenshot is taken before any of them are drawn), as a PNG in a_folder (ANSI, as
    // AhShitRim.ini has it; a relative one is under Skyrim's folder), or in Documents\My Games\
    // AhShitRim\Screenshots when it's empty. The file's path goes to the log.
    void Screenshot(std::string a_folder);

    // Whether a screenshot was saved since the last call.
    bool ScreenshotSaved();

    // Documents\My Games\AhShitRim; empty when there's no Documents folder.
    std::filesystem::path MyGamesFolder();

    // An image file (a PNG, or anything else Windows reads) as RGBA; false when it doesn't read.
    bool ReadImage(const std::filesystem::path& a_path, Image& a_out);
}
