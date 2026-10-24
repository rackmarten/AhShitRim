// The radar's map of Skyrim: a flat picture of the Tamriel worldspace in San Andreas's radar style,
// generated from the player's own Skyrim plugins (protocol/tools/radar_map.cpp writes it out). Nothing
// of it is ever committed: the repo holds only this recipe.
//
// Gathering: every exterior cell's landscape (heights, and the ground textures blended per vertex,
// classified by name), its water height, and the references to road pieces, buildings and trees,
// across the load order (a later plugin's cell, landscape or reference replaces an earlier one).
//
// Painting, at 32 pixels per cell (one pixel per landscape vertex, 128 units): ground classes in San
// Andreas's radar colours, smoothed into flat patches; water wherever the ground is below the cell's
// water; forest where trees stand close; light city ground around buildings, which are white blocks;
// and roads as clean dark lines of one width: the road pieces' footprints thinned to a centre line,
// short spurs pruned, then drawn again at a fixed width with soft-edged corners. Dirt paths painted
// on the ground get the same treatment, thinner and brown.
//
// Header-only and game-free, so the protocol tests cover the classification, the mapping and the
// line smoothing.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "esm.h"

namespace AhShitRim::RadarMap
{
    // Bump when the painting changes, so maps made by an older generator are made again.
    inline constexpr int kGeneratorVersion = 1;
    inline constexpr int kPixelsPerCell = 32;
    inline constexpr int kTreeHeight = 500;  // units: shorter "trees" don't make a forest
    inline constexpr float kLargestBuilding = 6.0e6f;  // square units (about 2450 x 2450)

    enum class Ground : std::uint8_t
    {
        Water,
        Grass,
        Forest,
        Tundra,
        Dirt,
        Rock,
        Snow,
        Sand,
        Path,
        Count
    };
    inline constexpr int kGroundCount = static_cast<int>(Ground::Count);

    struct Rgb
    {
        std::uint8_t r, g, b;
        constexpr bool operator==(const Rgb&) const = default;
    };

    // San Andreas's radar colours, sampled from its own radar tiles (radar00..143.txd in gta3.img).
    namespace Palette
    {
        inline constexpr Rgb kWater{ 0x73, 0x8a, 0xad };
        inline constexpr Rgb kGrass{ 0x73, 0x8a, 0x3a };     // the light green of fields and lawns
        inline constexpr Rgb kForest{ 0x39, 0x6c, 0x2d };    // the dark green of woods
        inline constexpr Rgb kTundra{ 0x6a, 0x74, 0x55 };    // the olive of scrubland
        inline constexpr Rgb kDirt{ 0x99, 0x86, 0x72 };      // the desert's brown
        inline constexpr Rgb kRock{ 0x93, 0x8f, 0x88 };      // the bare grey of quarries, a touch browner
        inline constexpr Rgb kSnow{ 0xd4, 0xd6, 0xd6 };      // not in San Andreas: a pale grey under the white of buildings
        inline constexpr Rgb kSand{ 0xcd, 0xc4, 0xa7 };
        inline constexpr Rgb kPath{ 0x80, 0x70, 0x5c };      // the brown of dirt tracks
        inline constexpr Rgb kCity{ 0x9c, 0x9a, 0x9c };      // the grey ground of towns
        inline constexpr Rgb kRoad{ 0x24, 0x24, 0x24 };
        inline constexpr Rgb kBuilding{ 0xff, 0xff, 0xff };
    }

    inline Rgb ColourOf(Ground a_ground)
    {
        switch (a_ground) {
        case Ground::Water: return Palette::kWater;
        case Ground::Grass: return Palette::kGrass;
        case Ground::Forest: return Palette::kForest;
        case Ground::Tundra: return Palette::kTundra;
        case Ground::Dirt: return Palette::kDirt;
        case Ground::Rock: return Palette::kRock;
        case Ground::Snow: return Palette::kSnow;
        case Ground::Sand: return Palette::kSand;
        case Ground::Path: return Palette::kPath;
        default: return Palette::kTundra;
        }
    }

    inline std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        for (auto& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            } else if (c == '/') {
                c = '\\';
            }
        }
        return out;
    }

    // A landscape texture's ground class, from its editor ID and diffuse texture (LTEX EDID, and its
    // TXST's TX00), e.g. "LTundra01" and "Landscape\tundra01.dds". Mods' textures mostly follow the
    // same names; anything unknown is tundra, the commonest ground.
    inline Ground ClassifyLandTexture(std::string_view a_editorId, std::string_view a_texture = {})
    {
        const std::string name = Lower(a_editorId) + "|" + Lower(a_texture);
        const auto        has = [&](std::string_view a_part) { return name.find(a_part) != std::string::npos; };
        if (has("path") || has("cobble") || has("road")) {
            return Ground::Path;
        }
        if (has("snow") || has("ice") || has("glacier") || has("frost")) {
            return Ground::Snow;
        }
        if (has("beach") || has("sand") || has("oceanfloor") || has("riverbottom") || has("riverbed")) {
            return Ground::Sand;
        }
        if (has("pineforest") || has("leaves") || has("forestfloor")) {
            return Ground::Forest;
        }
        if (has("rock") || has("cliff") || has("mountain") || has("gravel") || has("mineral") || has("stone")) {
            return Ground::Rock;
        }
        if (has("dirt") || has("mud") || has("ash")) {
            return Ground::Dirt;
        }
        if (has("tundra") || has("marsh") || has("lichen")) {
            return Ground::Tundra;
        }
        if (has("grass") || has("moss") || has("scrub") || has("field") || has("meadow")) {
            return Ground::Grass;
        }
        return Ground::Tundra;
    }

    enum class Feature : std::uint8_t
    {
        None,
        Road,
        Building,
        Town,  // town ground: the cities' terrain, foundations and distant versions
        Tree
    };

    // What a placed static or tree draws on the map, from its model path: road pieces and bridges are
    // roads, anything under Architecture a building, but for the cities' terrain, foundations and
    // distant (LOD) versions, which are town ground; trees are trees, but not the shrubs and plants
    // Skyrim also makes trees.
    inline Feature ClassifyModel(std::string_view a_model)
    {
        std::string model = Lower(a_model);
        if (model.starts_with("meshes\\")) {
            model.erase(0, 7);
        }
        const auto has = [&](std::string_view a_part) { return model.find(a_part) != std::string::npos; };
        if (model.starts_with("landscape\\roads\\") || model.starts_with("landscape\\bridges\\") || has("\\wrterrain\\wrmainroad")) {
            return Feature::Road;
        }
        if (model.starts_with("landscape\\trees\\")) {
            return Feature::Tree;
        }
        if (model.starts_with("architecture\\")) {
            return has("lod") || has("terrain") || has("land") || has("base") ? Feature::Town : Feature::Building;
        }
        return Feature::None;
    }

    // World coordinates to map pixels: the image spans whole cells, minCell to maxCell inclusive,
    // north up. Pixel (0, 0)'s top-left corner is world (OriginX, OriginY).
    struct Mapping
    {
        int minCellX = 0, minCellY = 0, maxCellX = -1, maxCellY = -1;
        int pixelsPerCell = kPixelsPerCell;

        int    Width() const { return (maxCellX - minCellX + 1) * pixelsPerCell; }
        int    Height() const { return (maxCellY - minCellY + 1) * pixelsPerCell; }
        double UnitsPerPixel() const { return Esm::kCellSize / pixelsPerCell; }
        double OriginX() const { return minCellX * static_cast<double>(Esm::kCellSize); }
        double OriginY() const { return (maxCellY + 1) * static_cast<double>(Esm::kCellSize); }

        std::pair<double, double> WorldToPixel(double a_x, double a_y) const
        {
            return { (a_x - OriginX()) / UnitsPerPixel(), (OriginY() - a_y) / UnitsPerPixel() };
        }

        std::pair<double, double> PixelToWorld(double a_px, double a_py) const
        {
            return { OriginX() + a_px * UnitsPerPixel(), OriginY() - a_py * UnitsPerPixel() };
        }
    };

    // ---- Gathering ----

    struct LandCell
    {
        int                                   x = 0, y = 0;
        std::array<float, Esm::kLandVertices> heights{};
        std::array<Ground, Esm::kLandVertices> ground{};
    };

    struct Placed
    {
        Feature              feature = Feature::None;
        float                x = 0, y = 0, rotation = 0, scale = 1;  // rotation about z, radians
        std::array<float, 4> bounds{};                                // x1, y1, x2, y2 before rotation and scale
    };

    struct World
    {
        std::vector<LandCell>                          land;
        std::unordered_map<std::uint64_t, float>       water;  // by cell, where the cell sets its own
        float                                          defaultWater = 0.0f;
        float                                          defaultLand = 0.0f;
        std::vector<Placed>                            placed;
        bool                                           found = false;  // the world was in the load order
    };

    inline std::uint64_t CellKey(int a_x, int a_y) { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(a_x)) << 32 | static_cast<std::uint32_t>(a_y); }

    // Each landscape vertex's ground: the quadrant's base texture, then its blended layers in order,
    // each fading what's under it by its opacity; the class with the most weight wins.
    inline void BlendGround(const Esm::Land& a_land, const std::unordered_map<std::uint64_t, Ground>& a_textures, std::uint64_t (*a_resolve)(std::uint32_t, const void*), const void* a_context,
        std::array<Ground, Esm::kLandVertices>& a_out, Ground a_default = Ground::Dirt)
    {
        const auto classOf = [&](std::uint32_t a_texture) {
            if (a_texture == 0) {
                return a_default;
            }
            const auto found = a_textures.find(a_resolve(a_texture, a_context));
            return found != a_textures.end() ? found->second : a_default;
        };
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            std::vector<const Esm::TextureLayer*> layers;
            const Esm::TextureLayer*              base = nullptr;
            for (const auto& layer : a_land.layers) {
                if (layer.quadrant != quadrant) {
                    continue;
                }
                if (layer.layer < 0) {
                    base = &layer;
                } else {
                    layers.push_back(&layer);
                }
            }
            std::stable_sort(layers.begin(), layers.end(), [](const auto* a, const auto* b) { return a->layer < b->layer; });
            const Ground baseClass = base ? classOf(base->texture) : a_default;
            std::vector<Ground> layerClasses;
            for (const auto* layer : layers) {
                layerClasses.push_back(classOf(layer->texture));
            }
            const int rowOffset = quadrant >= 2 ? Esm::kQuadrantSide - 1 : 0;
            const int columnOffset = (quadrant & 1) ? Esm::kQuadrantSide - 1 : 0;
            for (int row = 0; row < Esm::kQuadrantSide; ++row) {
                for (int column = 0; column < Esm::kQuadrantSide; ++column) {
                    std::array<float, kGroundCount> weights{};
                    weights[static_cast<int>(baseClass)] = 1.0f;
                    for (std::size_t i = 0; i < layers.size(); ++i) {
                        const float opacity = layers[i]->opacity[row * Esm::kQuadrantSide + column];
                        if (opacity <= 0.0f) {
                            continue;
                        }
                        for (auto& weight : weights) {
                            weight *= 1.0f - opacity;
                        }
                        weights[static_cast<int>(layerClasses[i])] += opacity;
                    }
                    const auto best = std::max_element(weights.begin(), weights.end()) - weights.begin();
                    a_out[(row + rowOffset) * Esm::kLandSide + column + columnOffset] = static_cast<Ground>(best);
                }
            }
        }
    }

    // Reads plugins in load order into a World. A form ID resolves through the plugin's own masters
    // to a load-order-wide key: the file's index in the load order, then the ID's low 24 bits.
    class Builder
    {
    public:
        explicit Builder(std::string a_world = "Tamriel") :
            _worldName(Lower(a_world)) {}

        // False when the file isn't a plugin or is cut short (what was read before that is kept).
        bool AddPlugin(std::string_view a_name, const std::uint8_t* a_data, std::size_t a_size)
        {
            const auto header = Esm::ReadHeader(a_data, a_size);
            if (!header) {
                return false;
            }
            Plugin plugin;
            plugin.index = static_cast<std::uint32_t>(_files.size());
            for (const auto& master : header->masters) {
                const auto found = std::find(_files.begin(), _files.end(), Lower(master));
                // A missing master keeps a slot of its own, so its records resolve to nothing loaded.
                plugin.masters.push_back(found != _files.end() ? static_cast<std::uint32_t>(found - _files.begin()) : 0xFFFFu);
            }
            _files.push_back(Lower(a_name));
            _plugin = &plugin;
            _cells.clear();
            const std::uint8_t* end = a_data + a_size;
            const bool          ok = Esm::ForEach(a_data + Esm::FirstGroup(a_data, a_size), end, [&](const Esm::Entry& a_top) {
                if (!a_top.group || a_top.groupType != Esm::kGroupTop) {
                    return;
                }
                if (a_top.label == Esm::Tag("TXST") || a_top.label == Esm::Tag("LTEX") || a_top.label == Esm::Tag("STAT") || a_top.label == Esm::Tag("TREE")) {
                    Esm::ForEach(a_top.data, a_top.data + a_top.size, [&](const Esm::Entry& a_record) { BaseRecord(a_top.label, a_record); });
                } else if (a_top.label == Esm::Tag("WRLD")) {
                    Worlds(a_top);
                }
            });
            _plugin = nullptr;
            return ok;
        }

        World Finish()
        {
            World out;
            out.found = _worldKey != 0;
            out.defaultWater = _defaultWater;
            out.defaultLand = _defaultLand;
            out.land.reserve(_land.size());
            for (auto& [key, cell] : _land) {
                out.land.push_back(std::move(cell));
            }
            std::sort(out.land.begin(), out.land.end(), [](const LandCell& a, const LandCell& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
            out.water = std::move(_water);
            out.placed.reserve(_refs.size());
            for (auto& [key, placed] : _refs) {
                out.placed.push_back(placed);
            }
            return out;
        }

        // Counters for the generator's log.
        std::size_t references() const { return _refs.size(); }

    private:
        struct Plugin
        {
            std::uint32_t              index = 0;
            std::vector<std::uint32_t> masters;
        };

        struct Base
        {
            Feature              feature = Feature::None;
            std::array<float, 4> bounds{};
        };

        std::uint64_t Resolve(std::uint32_t a_formId) const
        {
            const std::uint32_t master = a_formId >> 24;
            const std::uint32_t file = master < _plugin->masters.size() ? _plugin->masters[master] : _plugin->index;
            return static_cast<std::uint64_t>(file) << 32 | (a_formId & 0x00FFFFFFu);
        }

        static std::uint64_t ResolveThunk(std::uint32_t a_formId, const void* a_self) { return static_cast<const Builder*>(a_self)->Resolve(a_formId); }

        void BaseRecord(std::uint32_t a_type, const Esm::Entry& a_record)
        {
            if (a_record.group || (a_record.flags & Esm::kFlagDeleted)) {
                return;
            }
            const auto fields = Esm::Fields(a_record);
            if (!fields.ok) {
                return;
            }
            const std::uint64_t key = Resolve(a_record.formId);
            if (a_type == Esm::Tag("TXST")) {
                Esm::ForEachField(fields.data, fields.size, [&](std::uint32_t a_field, const std::uint8_t* a_data, std::size_t a_size) {
                    if (a_field == Esm::Tag("TX00")) {
                        _textureSets[key] = Esm::ZString(a_data, a_size);
                    }
                });
            } else if (a_type == Esm::Tag("LTEX")) {
                std::string   editorId;
                std::uint32_t textureSet = 0;
                Esm::ForEachField(fields.data, fields.size, [&](std::uint32_t a_field, const std::uint8_t* a_data, std::size_t a_size) {
                    if (a_field == Esm::Tag("EDID")) {
                        editorId = Esm::ZString(a_data, a_size);
                    } else if (a_field == Esm::Tag("TNAM") && a_size >= 4) {
                        textureSet = Esm::detail::U32(a_data);
                    }
                });
                const auto found = textureSet ? _textureSets.find(Resolve(textureSet)) : _textureSets.end();
                _textures[key] = ClassifyLandTexture(editorId, found != _textureSets.end() ? std::string_view(found->second) : std::string_view());
            } else {
                const auto object = Esm::ReadBaseObject(fields.data, fields.size);
                Base       base;
                base.feature = ClassifyModel(object.model);
                // Saplings and logs aren't woods.
                if (base.feature == Feature::Tree && object.bounds[5] - object.bounds[2] < kTreeHeight) {
                    base.feature = Feature::None;
                }
                base.bounds = { static_cast<float>(object.bounds[0]), static_cast<float>(object.bounds[1]), static_cast<float>(object.bounds[3]), static_cast<float>(object.bounds[4]) };
                if (base.feature != Feature::None) {
                    _bases[key] = base;
                } else {
                    _bases.erase(key);
                }
            }
        }

        void Worlds(const Esm::Entry& a_top)
        {
            Esm::ForEach(a_top.data, a_top.data + a_top.size, [&](const Esm::Entry& a_entry) {
                if (!a_entry.group) {
                    if (a_entry.type != Esm::Tag("WRLD")) {
                        return;
                    }
                    const auto fields = Esm::Fields(a_entry);
                    if (!fields.ok) {
                        return;
                    }
                    const auto          world = Esm::ReadWorld(fields.data, fields.size);
                    const std::uint64_t key = Resolve(a_entry.formId);
                    if (Lower(world.editorId) == _worldName) {
                        _worldKey = key;
                        _defaultWater = world.defaultWaterHeight;
                        _defaultLand = world.defaultLandHeight;
                    }
                    // Only the worlds standing on their parent's landscape (the walled cities) share its
                    // place; the rest (caves, Sovngarde, test worlds) are elsewhere in all but name.
                    if (world.parent && (world.parentUse & Esm::kUseParentLand)) {
                        _parents[key] = Resolve(*world.parent);
                    } else {
                        _parents.erase(key);
                    }
                    return;
                }
                if (a_entry.groupType != Esm::kGroupWorldChildren || _worldKey == 0) {
                    return;
                }
                const std::uint64_t key = Resolve(a_entry.label);
                const auto          parent = _parents.find(key);
                const bool          main = key == _worldKey;
                // Child worlds (the walled cities) share the world's coordinates: their buildings and
                // roads belong on the map, their landscape doesn't (the world's own is under them).
                if (!main && (parent == _parents.end() || parent->second != _worldKey)) {
                    return;
                }
                WorldChildren(a_entry.data, a_entry.data + a_entry.size, main);
            });
        }

        void WorldChildren(const std::uint8_t* a_begin, const std::uint8_t* a_end, bool a_main)
        {
            Esm::ForEach(a_begin, a_end, [&](const Esm::Entry& a_entry) {
                if (!a_entry.group) {
                    if (a_entry.type == Esm::Tag("CELL")) {
                        CellRecord(a_entry, a_main);
                    }
                    return;
                }
                switch (a_entry.groupType) {
                case Esm::kGroupExteriorBlock:
                case Esm::kGroupExteriorSubBlock:
                    WorldChildren(a_entry.data, a_entry.data + a_entry.size, a_main);
                    break;
                case Esm::kGroupCellChildren:
                    {
                        const auto cell = _cells.find(a_entry.label);
                        CellChildren(a_entry.data, a_entry.data + a_entry.size, a_main && cell != _cells.end() ? &cell->second : nullptr);
                        break;
                    }
                default:
                    break;
                }
            });
        }

        void CellRecord(const Esm::Entry& a_record, bool a_main)
        {
            const auto fields = Esm::Fields(a_record);
            if (!fields.ok) {
                return;
            }
            const auto cell = Esm::ReadCell(fields.data, fields.size);
            if (!cell.hasGrid) {
                return;  // the persistent cell: its references are still read, by position
            }
            _cells[a_record.formId] = { cell.x, cell.y };
            if (!a_main) {
                return;
            }
            const auto key = CellKey(cell.x, cell.y);
            // Most cells store the largest float, which means the world's default (the sea level);
            // a hugely negative one means no water.
            if (!cell.waterHeight || *cell.waterHeight >= 1.0e9f) {
                _water.erase(key);
            } else {
                _water[key] = std::max(*cell.waterHeight, -1.0e9f);
            }
        }

        void CellChildren(const std::uint8_t* a_begin, const std::uint8_t* a_end, const std::pair<int, int>* a_grid)
        {
            Esm::ForEach(a_begin, a_end, [&](const Esm::Entry& a_entry) {
                if (a_entry.group) {
                    if (a_entry.groupType == Esm::kGroupCellPersistent || a_entry.groupType == Esm::kGroupCellTemporary) {
                        CellChildren(a_entry.data, a_entry.data + a_entry.size, a_grid);
                    }
                    return;
                }
                if (a_entry.type == Esm::Tag("LAND") && a_grid) {
                    LandRecord(a_entry, *a_grid);
                } else if (a_entry.type == Esm::Tag("REFR")) {
                    ReferenceRecord(a_entry);
                }
            });
        }

        void LandRecord(const Esm::Entry& a_record, std::pair<int, int> a_grid)
        {
            const auto key = CellKey(a_grid.first, a_grid.second);
            if (a_record.flags & Esm::kFlagDeleted) {
                _land.erase(key);
                return;
            }
            const auto fields = Esm::Fields(a_record);
            if (!fields.ok) {
                return;
            }
            const auto land = Esm::ReadLand(fields.data, fields.size);
            if (!land.hasHeights) {
                return;
            }
            LandCell cell;
            cell.x = a_grid.first;
            cell.y = a_grid.second;
            cell.heights = land.heights;
            BlendGround(land, _textures, &Builder::ResolveThunk, this, cell.ground, Ground::Rock);
            _land[key] = std::move(cell);
        }

        void ReferenceRecord(const Esm::Entry& a_record)
        {
            const std::uint64_t key = Resolve(a_record.formId);
            if (a_record.flags & (Esm::kFlagDeleted | Esm::kFlagInitiallyDisabled)) {
                _refs.erase(key);
                return;
            }
            const auto fields = Esm::Fields(a_record);
            if (!fields.ok) {
                return;
            }
            const auto ref = Esm::ReadReference(fields.data, fields.size);
            const auto base = _bases.find(Resolve(ref.base));
            if (base == _bases.end()) {
                _refs.erase(key);
                return;
            }
            Placed placed;
            placed.feature = base->second.feature;
            placed.x = ref.position[0];
            placed.y = ref.position[1];
            placed.rotation = ref.rotation[2];
            placed.scale = ref.scale;
            placed.bounds = base->second.bounds;
            _refs[key] = placed;
        }

        std::string                                     _worldName;
        std::vector<std::string>                        _files;
        Plugin*                                         _plugin = nullptr;
        std::uint64_t                                   _worldKey = 0;
        float                                           _defaultWater = 0.0f, _defaultLand = 0.0f;
        std::unordered_map<std::uint64_t, std::uint64_t> _parents;
        std::unordered_map<std::uint64_t, std::string>  _textureSets;
        std::unordered_map<std::uint64_t, Ground>       _textures;
        std::unordered_map<std::uint64_t, Base>         _bases;
        std::unordered_map<std::uint32_t, std::pair<int, int>> _cells;  // this plugin's cells, by form ID as stored
        std::unordered_map<std::uint64_t, LandCell>     _land;
        std::unordered_map<std::uint64_t, float>        _water;
        std::unordered_map<std::uint64_t, Placed>       _refs;
    };

    // ---- Rasters ----

    template <class T>
    struct Raster
    {
        int            width = 0, height = 0;
        std::vector<T> pixels;

        Raster() = default;
        Raster(int a_width, int a_height, T a_fill = T{}) :
            width(a_width), height(a_height), pixels(static_cast<std::size_t>(a_width) * a_height, a_fill) {}

        T&       at(int a_x, int a_y) { return pixels[static_cast<std::size_t>(a_y) * width + a_x]; }
        const T& at(int a_x, int a_y) const { return pixels[static_cast<std::size_t>(a_y) * width + a_x]; }
        bool     inside(int a_x, int a_y) const { return a_x >= 0 && a_y >= 0 && a_x < width && a_y < height; }
    };

    using Mask = Raster<std::uint8_t>;

    // A box blur of radius a_radius along x then y (the mean over a (2r+1)^2 window, edges clamped).
    inline void BoxBlur(Raster<float>& a_raster, int a_radius)
    {
        if (a_radius <= 0 || a_raster.pixels.empty()) {
            return;
        }
        const int          w = a_raster.width, h = a_raster.height;
        std::vector<float> line(static_cast<std::size_t>(std::max(w, h)));
        const float        scale = 1.0f / (2 * a_radius + 1);
        const auto         pass = [&](int a_count, int a_length, auto&& a_get, auto&& a_set) {
            for (int i = 0; i < a_count; ++i) {
                float sum = 0.0f;
                for (int k = -a_radius; k <= a_radius; ++k) {
                    sum += a_get(i, std::clamp(k, 0, a_length - 1));
                }
                for (int j = 0; j < a_length; ++j) {
                    line[j] = sum * scale;
                    sum += a_get(i, std::min(j + a_radius + 1, a_length - 1)) - a_get(i, std::max(j - a_radius, 0));
                }
                for (int j = 0; j < a_length; ++j) {
                    a_set(i, j, line[j]);
                }
            }
        };
        pass(
            h, w, [&](int y, int x) { return a_raster.at(x, y); }, [&](int y, int x, float v) { a_raster.at(x, y) = v; });
        pass(
            w, h, [&](int x, int y) { return a_raster.at(x, y); }, [&](int x, int y, float v) { a_raster.at(x, y) = v; });
    }

    inline Raster<float> ToFloat(const Mask& a_mask)
    {
        Raster<float> out(a_mask.width, a_mask.height);
        for (std::size_t i = 0; i < a_mask.pixels.size(); ++i) {
            out.pixels[i] = a_mask.pixels[i] ? 1.0f : 0.0f;
        }
        return out;
    }

    // Rounds a mask's edges and closes pinholes: blur, then keep what's at least a_threshold.
    inline void Soften(Mask& a_mask, int a_radius, float a_threshold = 0.5f)
    {
        auto soft = ToFloat(a_mask);
        BoxBlur(soft, a_radius);
        for (std::size_t i = 0; i < soft.pixels.size(); ++i) {
            a_mask.pixels[i] = soft.pixels[i] >= a_threshold ? 1 : 0;
        }
    }

    // Fills a rectangle (local bounds x1, y1, x2, y2, scaled, rotated by a_rotation about its origin
    // and placed at world (a_x, a_y)) into the mask, at every pixel whose centre it covers.
    inline void FillRotatedBox(Mask& a_mask, const Mapping& a_mapping, double a_x, double a_y, double a_rotation, double a_scale, const std::array<float, 4>& a_bounds)
    {
        // Skyrim's z rotation turns clockwise seen from above.
        const double c = std::cos(a_rotation), s = std::sin(a_rotation);
        const auto   toWorld = [&](double a_lx, double a_ly) {
            return std::pair{ a_x + (a_lx * c + a_ly * s) * a_scale, a_y + (-a_lx * s + a_ly * c) * a_scale };
        };
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        for (const auto& [lx, ly] : { std::pair{ a_bounds[0], a_bounds[1] }, std::pair{ a_bounds[2], a_bounds[1] }, std::pair{ a_bounds[0], a_bounds[3] }, std::pair{ a_bounds[2], a_bounds[3] } }) {
            const auto [wx, wy] = toWorld(lx, ly);
            const auto [px, py] = a_mapping.WorldToPixel(wx, wy);
            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(minX))), x1 = std::min(a_mask.width - 1, static_cast<int>(std::ceil(maxX)));
        const int y0 = std::max(0, static_cast<int>(std::floor(minY))), y1 = std::min(a_mask.height - 1, static_cast<int>(std::ceil(maxY)));
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const auto [wx, wy] = a_mapping.PixelToWorld(px + 0.5, py + 0.5);
                const double dx = (wx - a_x) / a_scale, dy = (wy - a_y) / a_scale;
                const double lx = dx * c - dy * s, ly = dx * s + dy * c;  // the inverse rotation
                if (lx >= a_bounds[0] && lx <= a_bounds[2] && ly >= a_bounds[1] && ly <= a_bounds[3]) {
                    a_mask.at(px, py) = 1;
                }
            }
        }
    }

    // Stamps a filled disc of radius a_radius pixels at every set pixel of a_lines into a_out.
    inline void Stamp(const Mask& a_lines, double a_radius, Mask& a_out)
    {
        const int                        r = static_cast<int>(std::ceil(a_radius));
        std::vector<std::pair<int, int>> disc;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= a_radius * a_radius + 0.25) {
                    disc.emplace_back(dx, dy);
                }
            }
        }
        for (int y = 0; y < a_lines.height; ++y) {
            for (int x = 0; x < a_lines.width; ++x) {
                if (!a_lines.at(x, y)) {
                    continue;
                }
                for (const auto& [dx, dy] : disc) {
                    if (a_out.inside(x + dx, y + dy)) {
                        a_out.at(x + dx, y + dy) = 1;
                    }
                }
            }
        }
    }

    namespace detail
    {
        // The 8 neighbours clockwise from north: P2..P9 in Zhang and Suen's paper.
        inline constexpr std::array<int, 8> kDx{ 0, 1, 1, 1, 0, -1, -1, -1 };
        inline constexpr std::array<int, 8> kDy{ -1, -1, 0, 1, 1, 1, 0, -1 };

        inline int Neighbours(const Mask& a_mask, int a_x, int a_y, std::array<std::uint8_t, 8>& a_out)
        {
            int count = 0;
            for (int i = 0; i < 8; ++i) {
                const int x = a_x + kDx[i], y = a_y + kDy[i];
                a_out[i] = a_mask.inside(x, y) && a_mask.at(x, y) ? 1 : 0;
                count += a_out[i];
            }
            return count;
        }
    }

    // Zhang-Suen thinning: peels a mask down to one-pixel centre lines, keeping it connected.
    inline void Thin(Mask& a_mask)
    {
        std::vector<std::size_t> remove;
        for (bool changed = true; changed;) {
            changed = false;
            for (int step = 0; step < 2; ++step) {
                remove.clear();
                for (int y = 0; y < a_mask.height; ++y) {
                    for (int x = 0; x < a_mask.width; ++x) {
                        if (!a_mask.at(x, y)) {
                            continue;
                        }
                        std::array<std::uint8_t, 8> p;
                        const int                   b = detail::Neighbours(a_mask, x, y, p);
                        if (b < 2 || b > 6) {
                            continue;
                        }
                        int transitions = 0;
                        for (int i = 0; i < 8; ++i) {
                            transitions += p[i] == 0 && p[(i + 1) % 8] == 1;
                        }
                        if (transitions != 1) {
                            continue;
                        }
                        // p[0] north, p[2] east, p[4] south, p[6] west.
                        const bool ok = step == 0 ? (!(p[0] && p[2] && p[4]) && !(p[2] && p[4] && p[6])) : (!(p[0] && p[2] && p[6]) && !(p[0] && p[4] && p[6]));
                        if (ok) {
                            remove.push_back(static_cast<std::size_t>(y) * a_mask.width + x);
                        }
                    }
                }
                for (const auto i : remove) {
                    a_mask.pixels[i] = 0;
                }
                changed = changed || !remove.empty();
            }
        }
    }

    // Removes branches of a one-pixel skeleton shorter than a_length pixels that end in nothing (the
    // stubs thinning leaves at a blob's corners), and lone pieces shorter than a_lone.
    inline void Prune(Mask& a_mask, int a_length, int a_lone)
    {
        using Point = std::pair<int, int>;
        // The set neighbours of a pixel not on the branch walked so far.
        const auto candidates = [&](int a_x, int a_y, const std::vector<Point>& a_branch) {
            std::vector<Point> out;
            for (int i = 0; i < 8; ++i) {
                const Point next{ a_x + detail::kDx[i], a_y + detail::kDy[i] };
                if (a_mask.inside(next.first, next.second) && a_mask.at(next.first, next.second) && std::find(a_branch.begin(), a_branch.end(), next) == a_branch.end()) {
                    out.push_back(next);
                }
            }
            return out;
        };
        // Two neighbours side by side are one way on (a staircase step), not a fork; nor is one.
        const auto oneWay = [](const std::vector<Point>& a_points) {
            for (const auto& a : a_points) {
                for (const auto& b : a_points) {
                    if (std::abs(a.first - b.first) + std::abs(a.second - b.second) > 1) {
                        return false;
                    }
                }
            }
            return true;
        };
        const int limit = std::max(a_length, a_lone);
        for (int y = 0; y < a_mask.height; ++y) {
            for (int x = 0; x < a_mask.width; ++x) {
                if (!a_mask.at(x, y)) {
                    continue;
                }
                std::vector<Point> branch{ { x, y } };
                auto               next = candidates(x, y, branch);
                if (next.empty()) {
                    if (a_lone > 1) {
                        a_mask.at(x, y) = 0;  // a speck
                    }
                    continue;
                }
                if (next.size() > 2 || !oneWay(next)) {
                    continue;  // not an end
                }
                // Walk from this end to a fork or the other end.
                bool junction = false;
                while (static_cast<int>(branch.size()) <= limit) {
                    if (next.empty()) {
                        break;  // the other end
                    }
                    if (!oneWay(next)) {
                        junction = true;
                        break;
                    }
                    // On a staircase, step along the side rather than across the corner.
                    const Point at = branch.back();
                    Point       step = next.front();
                    for (const auto& candidate : next) {
                        if (candidate.first == at.first || candidate.second == at.second) {
                            step = candidate;
                        }
                    }
                    branch.push_back(step);
                    next = candidates(step.first, step.second, branch);
                }
                const int length = static_cast<int>(branch.size());
                if ((junction && length < a_length) || (!junction && length < a_lone && length <= limit)) {
                    for (const auto& [bx, by] : branch) {
                        a_mask.at(bx, by) = 0;
                    }
                }
            }
        }
    }

    // A ragged road mask to a clean line: close small gaps, thin to the centre line, prune stubs,
    // draw it again a_width pixels wide and soften the corners.
    inline Mask SmoothLines(const Mask& a_mask, double a_width, int a_close = 1, int a_spur = 6, int a_lone = 8)
    {
        Mask lines = a_mask;
        if (a_close > 0) {
            Soften(lines, a_close, 0.2f);
        }
        Thin(lines);
        Prune(lines, a_spur, a_lone);
        Mask out(a_mask.width, a_mask.height);
        Stamp(lines, a_width / 2.0, out);
        Soften(out, 1);
        return out;
    }

    // ---- Painting ----

    struct Options
    {
        double roadWidth = 3.5;      // pixels
        double pathWidth = 2.0;
        float  steepSlope = 1.2f;    // height change per unit of distance above which bare ground is rock
        float  forestDensity = 0.015f;  // trees per pixel, averaged over the forest radius
        int    forestRadius = 5;
        int    patchRadius = 3;      // the ground's smoothing into flat patches
    };

    struct Image
    {
        Mapping          mapping;
        Raster<Rgb>      pixels;
    };

    inline Mapping MappingFor(const World& a_world)
    {
        Mapping mapping;
        if (a_world.land.empty()) {
            return mapping;
        }
        mapping.minCellX = mapping.maxCellX = a_world.land.front().x;
        mapping.minCellY = mapping.maxCellY = a_world.land.front().y;
        for (const auto& cell : a_world.land) {
            mapping.minCellX = std::min(mapping.minCellX, cell.x);
            mapping.maxCellX = std::max(mapping.maxCellX, cell.x);
            mapping.minCellY = std::min(mapping.minCellY, cell.y);
            mapping.maxCellY = std::max(mapping.maxCellY, cell.y);
        }
        return mapping;
    }

    inline Image Paint(const World& a_world, const Mapping& a_mapping, const Options& a_options = {})
    {
        const int w = a_mapping.Width(), h = a_mapping.Height();
        Image     image{ a_mapping, Raster<Rgb>(w, h) };
        if (w <= 0 || h <= 0) {
            return image;
        }
        constexpr int pp = kPixelsPerCell;

        // Ground per pixel from its landscape vertex; water where it's under the cell's water.
        Raster<Ground> ground(w, h, Ground::Water);
        for (const auto& cell : a_world.land) {
            const auto  found = a_world.water.find(CellKey(cell.x, cell.y));
            const float water = found != a_world.water.end() ? found->second : a_world.defaultWater;
            const int   left = (cell.x - a_mapping.minCellX) * pp;
            const int   top = (a_mapping.maxCellY - cell.y) * pp;
            for (int row = 0; row < pp; ++row) {
                for (int column = 0; column < pp; ++column) {
                    const int   v = row * Esm::kLandSide + column;
                    const float height = cell.heights[v];
                    Ground      g = cell.ground[v];
                    if (height < water) {
                        g = Ground::Water;
                    } else if (g != Ground::Snow && g != Ground::Path && g != Ground::Sand) {
                        const float dx = (cell.heights[v + 1] - height) / Esm::kVertexSpacing;
                        const float dy = (cell.heights[v + Esm::kLandSide] - height) / Esm::kVertexSpacing;
                        if (dx * dx + dy * dy > a_options.steepSlope * a_options.steepSlope) {
                            g = Ground::Rock;
                        }
                    }
                    // Row 0 is the cell's southern edge, the image's rows run north to south.
                    ground.at(left + column, top + pp - 1 - row) = g;
                }
            }
        }

        // Trees per pixel, averaged: forest where they stand close.
        Raster<float> trees(w, h);
        Mask          roads(w, h), buildings(w, h), town(w, h);
        for (const auto& placed : a_world.placed) {
            if (placed.feature == Feature::Tree) {
                const auto [px, py] = a_mapping.WorldToPixel(placed.x, placed.y);
                if (px >= 0 && py >= 0 && px < w && py < h) {
                    trees.at(static_cast<int>(px), static_cast<int>(py)) += 1.0f;
                }
            } else if (placed.feature == Feature::Road) {
                // The pieces are about 970 units across with their soft verges; the road is half that.
                auto bounds = placed.bounds;
                const float middle = (bounds[0] + bounds[2]) / 2, half = std::min((bounds[2] - bounds[0]) / 2, 260.0f);
                bounds[0] = middle - half;
                bounds[2] = middle + half;
                FillRotatedBox(roads, a_mapping, placed.x, placed.y, placed.rotation, placed.scale, bounds);
            } else if (placed.feature == Feature::Building || placed.feature == Feature::Town) {
                // Huge pieces (a city's rock, its whole district) are town ground, not a block; so are
                // slivers thinner than a pixel or two.
                const float sx = (placed.bounds[2] - placed.bounds[0]) * placed.scale, sy = (placed.bounds[3] - placed.bounds[1]) * placed.scale;
                if (std::max(sx, sy) > 8000) {
                    continue;  // window glows and the like, spread over a whole city and beyond
                }
                const bool block = placed.feature == Feature::Building && sx >= 192 && sy >= 192 && sx * sy <= kLargestBuilding && std::max(sx, sy) <= 4000;
                FillRotatedBox(block ? buildings : town, a_mapping, placed.x, placed.y, placed.rotation, placed.scale, placed.bounds);
            }
        }
        BoxBlur(trees, a_options.forestRadius);

        // Flat patches: each class's share of the square around a pixel, the biggest wins. Paths are
        // lines of their own, drawn later, so they don't count; forest takes over open ground.
        Mask paths(w, h);
        {
            std::array<Raster<float>, kGroundCount> share;
            for (auto& s : share) {
                s = Raster<float>(w, h);
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Ground g = ground.at(x, y);
                    if (g == Ground::Path) {
                        paths.at(x, y) = 1;
                        continue;
                    }
                    if ((g == Ground::Grass || g == Ground::Tundra || g == Ground::Dirt) && trees.at(x, y) >= a_options.forestDensity) {
                        g = Ground::Forest;
                    }
                    // Water counts double so narrow rivers survive; scattered rock gives way to what's around it.
                    share[static_cast<int>(g)].at(x, y) = g == Ground::Water ? 2.0f : g == Ground::Rock ? 0.75f : 1.0f;
                }
            }
            for (auto& s : share) {
                BoxBlur(s, a_options.patchRadius);
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    int   best = static_cast<int>(Ground::Dirt);
                    float most = 0.0f;
                    for (int g = 0; g < kGroundCount; ++g) {
                        if (share[g].at(x, y) > most) {
                            most = share[g].at(x, y);
                            best = g;
                        }
                    }
                    ground.at(x, y) = static_cast<Ground>(best);
                }
            }
        }

        // Town ground around buildings.
        Mask city = buildings;
        Soften(city, 4, 0.12f);
        for (std::size_t i = 0; i < city.pixels.size(); ++i) {
            city.pixels[i] |= town.pixels[i];
        }

        const Mask roadLines = SmoothLines(roads, a_options.roadWidth, 1, 6, 6);
        const Mask pathLines = SmoothLines(paths, a_options.pathWidth, 1, 8, 12);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Ground g = ground.at(x, y);
                Rgb          colour = ColourOf(g);
                if (city.at(x, y) && g != Ground::Water) {
                    colour = Palette::kCity;
                }
                if (pathLines.at(x, y) && g != Ground::Water && !city.at(x, y)) {
                    colour = Palette::kPath;
                }
                if (roadLines.at(x, y)) {
                    colour = Palette::kRoad;
                }
                if (buildings.at(x, y)) {
                    colour = Palette::kBuilding;
                }
                image.pixels.at(x, y) = colour;
            }
        }
        return image;
    }
}
