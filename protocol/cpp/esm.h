// Skyrim's plugin files (.esm, .esp, .esl), read in part: records and groups, their fields, compressed
// records, and the few record types the radar map generator needs (the plugin header's masters, cells,
// landscape, references). Header-only and game-free, so the protocol tests cover it.
//
// Layout: a file is the TES4 header record, then top-level groups. A record is a 24-byte header (type,
// data size, flags, form ID, version info) and its data: fields, each a 4-char type, a uint16 size and
// the bytes (an XXXX field gives the next field's size as uint32 when it doesn't fit). A compressed
// record's data is a uint32 decompressed size and a zlib stream. A group is a 24-byte header ("GRUP",
// the size including the header, a label, a type) and its records and groups.
//
// A world's cells: WRLD, then a group of type 1 (labelled with the world's form ID) holding the
// persistent cell and blocks (type 4) of sub-blocks (type 5) of CELL records, each followed by its
// children (type 6) with persistent (8) and temporary (9) groups holding LAND and REFR.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "inflate.h"

namespace AhShitRim::Esm
{
    inline constexpr std::size_t   kHeaderSize = 24;
    inline constexpr std::uint32_t kFlagMaster = 0x00000001;  // TES4
    inline constexpr std::uint32_t kFlagDeleted = 0x00000020;
    inline constexpr std::uint32_t kFlagLight = 0x00000200;  // TES4: an ESL-flagged plugin
    inline constexpr std::uint32_t kFlagInitiallyDisabled = 0x00000800;
    inline constexpr std::uint32_t kFlagCompressed = 0x00040000;

    inline constexpr std::int32_t kGroupTop = 0;
    inline constexpr std::int32_t kGroupWorldChildren = 1;
    inline constexpr std::int32_t kGroupExteriorBlock = 4;
    inline constexpr std::int32_t kGroupExteriorSubBlock = 5;
    inline constexpr std::int32_t kGroupCellChildren = 6;
    inline constexpr std::int32_t kGroupCellPersistent = 8;
    inline constexpr std::int32_t kGroupCellTemporary = 9;

    // A landscape record's vertex grid: 33 x 33 per cell, 128 units apart, row 0 the southern edge.
    inline constexpr int   kLandSide = 33;
    inline constexpr int   kLandVertices = kLandSide * kLandSide;
    inline constexpr float kCellSize = 4096.0f;
    inline constexpr float kVertexSpacing = kCellSize / (kLandSide - 1);
    // Texture layers apply per quadrant: 17 x 17 vertices, 0 south-west, 1 south-east, 2 north-west, 3 north-east.
    inline constexpr int kQuadrantSide = 17;

    constexpr std::uint32_t Tag(const char (&a_tag)[5])
    {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_tag[0])) | static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_tag[1])) << 8 |
               static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_tag[2])) << 16 | static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_tag[3])) << 24;
    }

    namespace detail
    {
        inline std::uint32_t U32(const std::uint8_t* a_p) { return a_p[0] | (a_p[1] << 8) | (a_p[2] << 16) | (static_cast<std::uint32_t>(a_p[3]) << 24); }
        inline std::uint16_t U16(const std::uint8_t* a_p) { return static_cast<std::uint16_t>(a_p[0] | (a_p[1] << 8)); }
        inline std::int16_t  I16(const std::uint8_t* a_p) { return static_cast<std::int16_t>(U16(a_p)); }
        inline float         F32(const std::uint8_t* a_p)
        {
            const std::uint32_t bits = U32(a_p);
            float               value;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }
    }

    // A record or a group at one level of the file.
    struct Entry
    {
        bool                group = false;
        std::uint32_t       type = 0;       // record type, or "GRUP"
        std::uint32_t       label = 0;      // group: label (a type tag, form ID or block coordinates)
        std::int32_t        groupType = 0;  // group
        std::uint32_t       flags = 0;      // record
        std::uint32_t       formId = 0;     // record, as stored (its top byte indexes the plugin's masters)
        const std::uint8_t* data = nullptr;  // record data (maybe compressed) or the group's contents
        std::size_t         size = 0;
    };

    // Calls a_visit(const Entry&) for each record and group in [a_begin, a_end), not descending into
    // groups. False when an entry runs past the end.
    template <class Visit>
    bool ForEach(const std::uint8_t* a_begin, const std::uint8_t* a_end, Visit&& a_visit)
    {
        const std::uint8_t* at = a_begin;
        while (at < a_end) {
            if (static_cast<std::size_t>(a_end - at) < kHeaderSize) {
                return false;
            }
            Entry entry;
            entry.type = detail::U32(at);
            const std::uint32_t size = detail::U32(at + 4);
            if (entry.type == Tag("GRUP")) {
                if (size < kHeaderSize || size > static_cast<std::size_t>(a_end - at)) {
                    return false;
                }
                entry.group = true;
                entry.label = detail::U32(at + 8);
                entry.groupType = static_cast<std::int32_t>(detail::U32(at + 12));
                entry.data = at + kHeaderSize;
                entry.size = size - kHeaderSize;
                at += size;
            } else {
                if (size > static_cast<std::size_t>(a_end - at) - kHeaderSize) {
                    return false;
                }
                entry.flags = detail::U32(at + 8);
                entry.formId = detail::U32(at + 12);
                entry.data = at + kHeaderSize;
                entry.size = size;
                at += kHeaderSize + size;
            }
            a_visit(static_cast<const Entry&>(entry));
        }
        return true;
    }

    // A record's fields, decompressed when it's compressed. Empty when it fails to decompress.
    struct RecordData
    {
        std::vector<std::uint8_t> inflated;
        const std::uint8_t*       data = nullptr;
        std::size_t               size = 0;
        bool                      ok = false;
    };

    inline RecordData Fields(const Entry& a_record)
    {
        RecordData out;
        if ((a_record.flags & kFlagCompressed) == 0) {
            out.data = a_record.data;
            out.size = a_record.size;
            out.ok = true;
            return out;
        }
        if (a_record.size < 4) {
            return out;
        }
        const std::uint32_t expected = detail::U32(a_record.data);
        auto                inflated = Inflate::Zlib(a_record.data + 4, a_record.size - 4, expected);
        if (!inflated || inflated->size() != expected) {
            return out;
        }
        out.inflated = std::move(*inflated);
        out.data = out.inflated.data();
        out.size = out.inflated.size();
        out.ok = true;
        return out;
    }

    // Calls a_visit(type, data, size) for each field. False when a field runs past the end.
    template <class Visit>
    bool ForEachField(const std::uint8_t* a_data, std::size_t a_size, Visit&& a_visit)
    {
        std::size_t   at = 0;
        std::uint32_t bigSize = 0;
        bool          big = false;
        while (at < a_size) {
            if (a_size - at < 6) {
                return false;
            }
            const std::uint32_t type = detail::U32(a_data + at);
            std::uint32_t       size = detail::U16(a_data + at + 4);
            at += 6;
            if (big) {
                size = bigSize;
                big = false;
            }
            if (size > a_size - at) {
                return false;
            }
            if (type == Tag("XXXX") && size == 4) {
                bigSize = detail::U32(a_data + at);
                big = true;
            } else {
                a_visit(type, a_data + at, static_cast<std::size_t>(size));
            }
            at += size;
        }
        return true;
    }

    inline std::string ZString(const std::uint8_t* a_data, std::size_t a_size)
    {
        std::size_t length = 0;
        while (length < a_size && a_data[length] != 0) {
            ++length;
        }
        return std::string(reinterpret_cast<const char*>(a_data), length);
    }

    // The TES4 header: its flags and the master files its form IDs index.
    struct PluginHeader
    {
        std::uint32_t            flags = 0;
        std::vector<std::string> masters;
    };

    inline std::optional<PluginHeader> ReadHeader(const std::uint8_t* a_data, std::size_t a_size)
    {
        if (a_size < kHeaderSize || detail::U32(a_data) != Tag("TES4")) {
            return std::nullopt;
        }
        std::optional<PluginHeader> out;
        ForEach(a_data, a_data + std::min(a_size, kHeaderSize + detail::U32(a_data + 4)), [&](const Entry& a_record) {
            out.emplace();
            out->flags = a_record.flags;
            const auto fields = Fields(a_record);
            ForEachField(fields.data, fields.size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
                if (a_type == Tag("MAST")) {
                    out->masters.push_back(ZString(a_field, a_fieldSize));
                }
            });
        });
        return out;
    }

    // Where the top-level groups' records start, after the header record.
    inline std::size_t FirstGroup(const std::uint8_t* a_data, std::size_t a_size)
    {
        if (a_size < kHeaderSize) {
            return a_size;
        }
        return std::min(a_size, kHeaderSize + detail::U32(a_data + 4));
    }

    struct Cell
    {
        bool                 hasGrid = false;
        std::int32_t         x = 0, y = 0;  // XCLC
        std::optional<float> waterHeight;   // XCLW; nothing means the world's default
        std::uint16_t        flags = 0;     // DATA
    };

    inline Cell ReadCell(const std::uint8_t* a_data, std::size_t a_size)
    {
        Cell out;
        ForEachField(a_data, a_size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
            if (a_type == Tag("XCLC") && a_fieldSize >= 8) {
                out.hasGrid = true;
                out.x = static_cast<std::int32_t>(detail::U32(a_field));
                out.y = static_cast<std::int32_t>(detail::U32(a_field + 4));
            } else if (a_type == Tag("XCLW") && a_fieldSize >= 4) {
                out.waterHeight = detail::F32(a_field);
            } else if (a_type == Tag("DATA") && a_fieldSize >= 1) {
                out.flags = a_fieldSize >= 2 ? detail::U16(a_field) : a_field[0];
            }
        });
        return out;
    }

    inline constexpr std::uint16_t kUseParentLand = 0x0001;

    struct World
    {
        std::string                  editorId;
        std::optional<std::uint32_t> parent;  // WNAM, as stored
        std::uint16_t                parentUse = 0;  // PNAM: what it takes from its parent (kUseParentLand, ...)
        float                        defaultLandHeight = 0.0f, defaultWaterHeight = 0.0f;  // DNAM
    };

    inline World ReadWorld(const std::uint8_t* a_data, std::size_t a_size)
    {
        World out;
        ForEachField(a_data, a_size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
            if (a_type == Tag("EDID")) {
                out.editorId = ZString(a_field, a_fieldSize);
            } else if (a_type == Tag("WNAM") && a_fieldSize >= 4) {
                out.parent = detail::U32(a_field);
            } else if (a_type == Tag("PNAM") && a_fieldSize >= 2) {
                out.parentUse = detail::U16(a_field);
            } else if (a_type == Tag("DNAM") && a_fieldSize >= 8) {
                out.defaultLandHeight = detail::F32(a_field);
                out.defaultWaterHeight = detail::F32(a_field + 4);
            }
        });
        return out;
    }

    struct TextureLayer
    {
        std::uint32_t                                         texture = 0;  // LTEX form ID as stored, 0 for the default
        int                                                   quadrant = 0;
        int                                                   layer = -1;   // -1 for the base texture
        std::array<float, kQuadrantSide * kQuadrantSide>      opacity{};    // per quadrant vertex; all 1 for a base
    };

    struct Land
    {
        bool                                hasHeights = false;
        std::array<float, kLandVertices>    heights{};  // world units, [row * 33 + column]
        std::vector<TextureLayer>           layers;     // bases (BTXT) and blended layers (ATXT + VTXT), in file order
    };

    // VHGT: a float offset, then 33 x 33 signed bytes: each row's first value adds to the previous row's
    // first, each other value to its left neighbour; all in units of 8.
    inline void DecodeHeights(const std::uint8_t* a_field, std::size_t a_size, std::array<float, kLandVertices>& a_out)
    {
        if (a_size < 4 + kLandVertices) {
            return;
        }
        float rowStart = detail::F32(a_field);
        for (int row = 0; row < kLandSide; ++row) {
            rowStart += static_cast<std::int8_t>(a_field[4 + row * kLandSide]);
            float value = rowStart;
            a_out[row * kLandSide] = value * 8.0f;
            for (int column = 1; column < kLandSide; ++column) {
                value += static_cast<std::int8_t>(a_field[4 + row * kLandSide + column]);
                a_out[row * kLandSide + column] = value * 8.0f;
            }
        }
    }

    inline Land ReadLand(const std::uint8_t* a_data, std::size_t a_size)
    {
        Land          out;
        TextureLayer* lastAlpha = nullptr;
        ForEachField(a_data, a_size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
            if (a_type == Tag("VHGT")) {
                if (a_fieldSize >= 4 + kLandVertices) {
                    DecodeHeights(a_field, a_fieldSize, out.heights);
                    out.hasHeights = true;
                }
            } else if ((a_type == Tag("BTXT") || a_type == Tag("ATXT")) && a_fieldSize >= 8) {
                TextureLayer layer;
                layer.texture = detail::U32(a_field);
                layer.quadrant = a_field[4] & 3;
                if (a_type == Tag("BTXT")) {
                    layer.opacity.fill(1.0f);
                } else {
                    layer.layer = detail::I16(a_field + 6);
                }
                out.layers.push_back(layer);
                lastAlpha = a_type == Tag("ATXT") ? &out.layers.back() : nullptr;
            } else if (a_type == Tag("VTXT") && lastAlpha) {
                // 8 bytes each: a uint16 position in the quadrant (row * 17 + column), 2 unused, float opacity.
                for (std::size_t at = 0; at + 8 <= a_fieldSize; at += 8) {
                    const std::uint16_t position = detail::U16(a_field + at);
                    if (position < lastAlpha->opacity.size()) {
                        lastAlpha->opacity[position] = std::clamp(detail::F32(a_field + at + 4), 0.0f, 1.0f);
                    }
                }
                lastAlpha = nullptr;
            }
        });
        return out;
    }

    // A placed reference: its base object, position, rotation (radians) and scale.
    struct Reference
    {
        std::uint32_t         base = 0;  // NAME, as stored
        std::array<float, 3>  position{};
        std::array<float, 3>  rotation{};
        float                 scale = 1.0f;
    };

    inline Reference ReadReference(const std::uint8_t* a_data, std::size_t a_size)
    {
        Reference out;
        ForEachField(a_data, a_size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
            if (a_type == Tag("NAME") && a_fieldSize >= 4) {
                out.base = detail::U32(a_field);
            } else if (a_type == Tag("DATA") && a_fieldSize >= 24) {
                for (int i = 0; i < 3; ++i) {
                    out.position[i] = detail::F32(a_field + i * 4);
                    out.rotation[i] = detail::F32(a_field + 12 + i * 4);
                }
            } else if (a_type == Tag("XSCL") && a_fieldSize >= 4) {
                out.scale = detail::F32(a_field);
            }
        });
        return out;
    }

    // A base object's editor ID, model and object bounds (OBND: x1, y1, z1, x2, y2, z2).
    struct BaseObject
    {
        std::string                   editorId;
        std::string                   model;
        std::array<std::int16_t, 6>   bounds{};
    };

    inline BaseObject ReadBaseObject(const std::uint8_t* a_data, std::size_t a_size)
    {
        BaseObject out;
        ForEachField(a_data, a_size, [&](std::uint32_t a_type, const std::uint8_t* a_field, std::size_t a_fieldSize) {
            if (a_type == Tag("EDID")) {
                out.editorId = ZString(a_field, a_fieldSize);
            } else if (a_type == Tag("MODL")) {
                out.model = ZString(a_field, a_fieldSize);
            } else if (a_type == Tag("OBND") && a_fieldSize >= 12) {
                for (int i = 0; i < 6; ++i) {
                    out.bounds[i] = detail::I16(a_field + i * 2);
                }
            }
        });
        return out;
    }
}
