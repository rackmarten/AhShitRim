// Generates the radar's map of Skyrim from the player's own plugins (protocol/cpp/radar_map.h): reads
// the load order (the base game's masters, Creation Club content, then plugins.txt), paints Tamriel in
// San Andreas's radar style and writes radar-map.png and radar-map.json (the world-to-pixel mapping and
// the plugins it was made from) to Documents\My Games\AhShitRim. Does nothing when the map there was
// made from the same plugins (names, sizes, times) by the same generator, unless --force.
//
//   radar_map [--skyrim <Skyrim folder>] [--out <folder>] [--force] [--no-mods]
//
// scripts/radar-map.ps1 builds and runs it with the Skyrim folder from local.props.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../cpp/radar_map.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;
using namespace AhShitRim;

namespace
{
    struct Source
    {
        std::string   name;
        fs::path      path;
        std::uintmax_t size = 0;
        long long     time = 0;  // last write, seconds since 1970
    };

    std::string Lower(std::string a_text)
    {
        std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a_text;
    }

    fs::path KnownFolder(REFKNOWNFOLDERID a_id)
    {
        PWSTR    raw = nullptr;
        fs::path out;
        if (SUCCEEDED(SHGetKnownFolderPath(a_id, 0, nullptr, &raw))) {
            out = raw;
        }
        CoTaskMemFree(raw);
        return out;
    }

    // Steam's install path for Skyrim Special Edition, from the registry.
    fs::path SkyrimFromRegistry()
    {
        wchar_t buffer[MAX_PATH] = {};
        DWORD   size = sizeof(buffer);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Bethesda Softworks\\Skyrim Special Edition", L"Installed Path", RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS) {
            return buffer;
        }
        return {};
    }

    std::vector<std::string> Lines(const fs::path& a_path)
    {
        std::vector<std::string> out;
        std::ifstream            in(a_path);
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            out.push_back(line);
        }
        return out;
    }

    bool IsMaster(const fs::path& a_path)
    {
        const auto ext = Lower(a_path.extension().string());
        if (ext == ".esm" || ext == ".esl") {
            return true;
        }
        std::ifstream in(a_path, std::ios::binary);
        std::uint8_t  header[12] = {};
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        return (header[8] & Esm::kFlagMaster) != 0;
    }

    // The game's load order: its masters, Creation Club content (Skyrim.ccc), then the active plugins
    // (plugins.txt, '*' first), masters before the rest as the game sorts them.
    std::vector<Source> LoadOrder(const fs::path& a_data, bool a_mods)
    {
        std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };
        if (a_mods) {
            for (const auto& line : Lines(a_data.parent_path() / "Skyrim.ccc")) {
                names.push_back(line);
            }
            for (const auto& line : Lines(KnownFolder(FOLDERID_LocalAppData) / "Skyrim Special Edition" / "plugins.txt")) {
                if (line.size() > 1 && line[0] == '*') {
                    names.push_back(line.substr(1));
                }
            }
        }
        std::vector<Source> out;
        for (const auto& name : names) {
            if (name.empty() || std::any_of(out.begin(), out.end(), [&](const Source& s) { return Lower(s.name) == Lower(name); })) {
                continue;
            }
            const fs::path  path = a_data / name;
            std::error_code error;
            const auto      size = fs::file_size(path, error);
            if (error) {
                continue;
            }
            const auto written = fs::last_write_time(path, error);
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(written).time_since_epoch()).count();
            out.push_back({ name, path, size, seconds });
        }
        std::stable_partition(out.begin(), out.end(), [](const Source& s) { return IsMaster(s.path); });
        return out;
    }

    std::string JsonString(const std::string& a_text)
    {
        std::string out = "\"";
        for (const char c : a_text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }

    std::string SourcesJson(const std::vector<Source>& a_sources)
    {
        std::ostringstream out;
        out << "\"generator\": " << RadarMap::kGeneratorVersion << ",\n  \"sources\": [\n";
        for (std::size_t i = 0; i < a_sources.size(); ++i) {
            out << "    { \"name\": " << JsonString(a_sources[i].name) << ", \"size\": " << a_sources[i].size << ", \"time\": " << a_sources[i].time << " }" << (i + 1 < a_sources.size() ? "," : "") << "\n";
        }
        out << "  ]";
        return out.str();
    }

    bool WritePng(const fs::path& a_path, const RadarMap::Raster<RadarMap::Rgb>& a_pixels)
    {
        std::vector<std::uint8_t> bgr(a_pixels.pixels.size() * 3);
        for (std::size_t i = 0; i < a_pixels.pixels.size(); ++i) {
            bgr[i * 3] = a_pixels.pixels[i].b;
            bgr[i * 3 + 1] = a_pixels.pixels[i].g;
            bgr[i * 3 + 2] = a_pixels.pixels[i].r;
        }
        IWICImagingFactory*    factory = nullptr;
        IWICStream*            stream = nullptr;
        IWICBitmapEncoder*     encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        bool                   ok = false;
        WICPixelFormatGUID     format = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(a_path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(static_cast<UINT>(a_pixels.width), static_cast<UINT>(a_pixels.height))) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
            format == GUID_WICPixelFormat24bppBGR &&
            SUCCEEDED(frame->WritePixels(static_cast<UINT>(a_pixels.height), static_cast<UINT>(a_pixels.width) * 3, static_cast<UINT>(bgr.size()), bgr.data())) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
            ok = true;
        }
        for (IUnknown* object : std::initializer_list<IUnknown*>{ frame, encoder, stream, factory }) {
            if (object) {
                object->Release();
            }
        }
        return ok;
    }

    double Seconds(std::chrono::steady_clock::time_point a_since)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - a_since).count();
    }
}

int wmain(int argc, wchar_t** argv)
{
    fs::path skyrim, out;
    bool     force = false, mods = true;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--skyrim" && i + 1 < argc) {
            skyrim = argv[++i];
        } else if (arg == L"--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == L"--force") {
            force = true;
        } else if (arg == L"--no-mods") {
            mods = false;
        } else {
            std::fprintf(stderr, "usage: radar_map [--skyrim <Skyrim folder>] [--out <folder>] [--force] [--no-mods]\n");
            return 2;
        }
    }
    if (skyrim.empty()) {
        skyrim = SkyrimFromRegistry();
    }
    if (skyrim.empty() || !fs::exists(skyrim / "Data" / "Skyrim.esm")) {
        std::fprintf(stderr, "radar_map: no Skyrim.esm under \"%s\\Data\"; pass --skyrim <Skyrim folder>\n", skyrim.string().c_str());
        return 1;
    }
    if (out.empty()) {
        out = KnownFolder(FOLDERID_Documents) / "My Games" / "AhShitRim";
    }
    std::error_code error;
    fs::create_directories(out, error);

    const auto start = std::chrono::steady_clock::now();
    const auto sources = LoadOrder(skyrim / "Data", mods);
    const auto sourcesJson = SourcesJson(sources);
    const auto pngPath = out / "radar-map.png";
    const auto jsonPath = out / "radar-map.json";
    if (!force && fs::exists(pngPath)) {
        std::ifstream      in(jsonPath, std::ios::binary);
        std::ostringstream existing;
        existing << in.rdbuf();
        if (existing.str().find(sourcesJson) != std::string::npos) {
            std::printf("radar_map: %s is up to date (%zu plugins)\n", pngPath.string().c_str(), sources.size());
            return 0;
        }
    }

    RadarMap::Builder builder;
    for (const auto& source : sources) {
        std::ifstream             in(source.path, std::ios::binary);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(source.size));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in || !builder.AddPlugin(source.name, bytes.data(), bytes.size())) {
            std::fprintf(stderr, "radar_map: couldn't read all of %s\n", source.name.c_str());
        }
    }
    const auto world = builder.Finish();
    const double read = Seconds(start);
    if (!world.found || world.land.empty()) {
        std::fprintf(stderr, "radar_map: no Tamriel landscape in the load order\n");
        return 1;
    }
    std::size_t counts[static_cast<int>(RadarMap::Feature::Tree) + 1] = {};
    for (const auto& placed : world.placed) {
        ++counts[static_cast<int>(placed.feature)];
    }
    std::printf("radar_map: read %zu plugins in %.1f s: %zu landscape cells, %zu road pieces, %zu buildings, %zu trees\n", sources.size(), read, world.land.size(),
        counts[static_cast<int>(RadarMap::Feature::Road)], counts[static_cast<int>(RadarMap::Feature::Building)], counts[static_cast<int>(RadarMap::Feature::Tree)]);

    const auto paintStart = std::chrono::steady_clock::now();
    const auto mapping = RadarMap::MappingFor(world);
    const auto image = RadarMap::Paint(world, mapping);
    const double painted = Seconds(paintStart);

    const auto writeStart = std::chrono::steady_clock::now();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto tempPng = out / "radar-map.png.tmp";
    if (!WritePng(tempPng, image.pixels)) {
        std::fprintf(stderr, "radar_map: couldn't write %s\n", tempPng.string().c_str());
        return 1;
    }
    fs::rename(tempPng, pngPath, error);
    if (error) {
        std::fprintf(stderr, "radar_map: couldn't replace %s: %s\n", pngPath.string().c_str(), error.message().c_str());
        return 1;
    }
    std::ofstream json(jsonPath, std::ios::binary);
    json << "{\n  \"image\": \"radar-map.png\",\n  \"world\": \"Tamriel\",\n"
         << "  \"width\": " << mapping.Width() << ",\n  \"height\": " << mapping.Height() << ",\n"
         << "  \"pixelsPerCell\": " << mapping.pixelsPerCell << ",\n  \"unitsPerPixel\": " << mapping.UnitsPerPixel() << ",\n"
         << "  \"cells\": { \"minX\": " << mapping.minCellX << ", \"minY\": " << mapping.minCellY << ", \"maxX\": " << mapping.maxCellX << ", \"maxY\": " << mapping.maxCellY << " },\n"
         << "  \"origin\": { \"x\": " << static_cast<long long>(mapping.OriginX()) << ", \"y\": " << static_cast<long long>(mapping.OriginY()) << " },\n"
         << "  \"mapping\": \"pixelX = (worldX - origin.x) / unitsPerPixel, pixelY = (origin.y - worldY) / unitsPerPixel\",\n"
         << "  " << sourcesJson << "\n}\n";
    json.close();
    const double written = Seconds(writeStart);
    std::printf("radar_map: painted %dx%d in %.1f s, wrote it in %.1f s (%.1f s in all): %s\n", mapping.Width(), mapping.Height(), painted, written, Seconds(start), pngPath.string().c_str());
    return 0;
}
