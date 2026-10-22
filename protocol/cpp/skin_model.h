// CJ's model as San Andreas sends it (ModelChunk messages, protocol v5), and each vehicle's (v6,
// its parts as bones; protocol/cpp/vehicle.h): one blob with the textures, meshes, skin weights and
// bone ids, cut into chunks. Header-only and free of game types:
// the ASI builds a blob with Serialize and cuts it with Chunks, Skyrim puts it back together with
// the Assembler and reads it with Parse. Modelled on PerkeleRim's protocol/cpp/model.h.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "../ahshitrim_protocol.h"

namespace AhShitRim::SkinModel
{
    // ---- Building a blob (San Andreas side) ----

    struct TextureIn
    {
        std::uint32_t             width = 0;
        std::uint32_t             height = 0;
        std::vector<std::uint8_t> rgba;  // width * height * 4, top row first
    };

    struct MeshIn
    {
        std::int32_t                      texture = -1;
        std::uint32_t                     flags = 0;  // Protocol::kSkinMesh*
        std::uint8_t                      color[4] = { 255, 255, 255, 255 };
        std::vector<Protocol::SkinVertex> vertices;
        std::vector<std::uint16_t>        indices;
    };

    struct ModelIn
    {
        std::string                name;
        std::uint32_t              kind = Protocol::kSkinModelBody;
        std::vector<std::uint32_t> boneIds;
        std::vector<TextureIn>     textures;
        std::vector<MeshIn>        meshes;
    };

    namespace Detail
    {
        template <class T>
        void Put(std::vector<std::uint8_t>& a_out, const T& a_value)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&a_value);
            a_out.insert(a_out.end(), bytes, bytes + sizeof(T));
        }

        inline void PutBytes(std::vector<std::uint8_t>& a_out, const void* a_data, std::size_t a_size)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(a_data);
            a_out.insert(a_out.end(), bytes, bytes + a_size);
        }
    }

    inline std::vector<std::uint8_t> Serialize(const ModelIn& a_model)
    {
        std::vector<std::uint8_t> out;
        Protocol::SkinModelHeader header{};
        std::memcpy(header.name, a_model.name.data(), (std::min)(a_model.name.size(), sizeof(header.name) - 1));
        header.textureCount = static_cast<std::uint32_t>(a_model.textures.size());
        header.meshCount = static_cast<std::uint32_t>(a_model.meshes.size());
        header.boneCount = static_cast<std::uint32_t>(a_model.boneIds.size());
        header.kind = a_model.kind;
        Detail::Put(out, header);
        Detail::PutBytes(out, a_model.boneIds.data(), a_model.boneIds.size() * sizeof(std::uint32_t));
        for (const auto& texture : a_model.textures) {
            Detail::Put(out, Protocol::SkinTexture{ texture.width, texture.height, Protocol::kSkinTextureRgba8, 0 });
            Detail::PutBytes(out, texture.rgba.data(), texture.rgba.size());
        }
        for (const auto& mesh : a_model.meshes) {
            Protocol::SkinMesh m{};
            m.texture = mesh.texture;
            m.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
            m.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            m.flags = mesh.flags;
            std::memcpy(m.color, mesh.color, sizeof(m.color));
            Detail::Put(out, m);
            Detail::PutBytes(out, mesh.vertices.data(), mesh.vertices.size() * sizeof(Protocol::SkinVertex));
            Detail::PutBytes(out, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint16_t));
        }
        return out;
    }

    // The ModelChunk payloads (ModelChunkHeader + up to kModelChunkMax bytes) carrying a_blob.
    inline std::vector<std::vector<std::uint8_t>> Chunks(std::uint32_t a_model, const std::vector<std::uint8_t>& a_blob)
    {
        std::vector<std::vector<std::uint8_t>> chunks;
        const auto    total = static_cast<std::uint32_t>(a_blob.size());
        std::uint32_t at = 0;
        do {
            const std::uint32_t       size = (std::min)(Protocol::kModelChunkMax, total - at);
            std::vector<std::uint8_t> chunk;
            chunk.reserve(sizeof(Protocol::ModelChunkHeader) + size);
            Detail::Put(chunk, Protocol::ModelChunkHeader{ a_model, total, at, size });
            Detail::PutBytes(chunk, a_blob.data() + at, size);
            chunks.push_back(std::move(chunk));
            at += size;
        } while (at < total);
        return chunks;
    }

    // ---- Reading a blob (Skyrim side) ----

    struct Texture
    {
        std::uint32_t       width;
        std::uint32_t       height;
        const std::uint8_t* rgba;  // width * height * 4, top row first
    };

    struct Mesh
    {
        std::int32_t        texture;
        std::uint32_t       flags;
        std::uint8_t        color[4];
        const std::uint8_t* vertices;  // vertexCount SkinVertex, unaligned: read with Vertex
        std::uint32_t       vertexCount;
        const std::uint8_t* indices;   // indexCount little-endian uint16
        std::uint32_t       indexCount;

        Protocol::SkinVertex Vertex(std::uint32_t a_i) const
        {
            Protocol::SkinVertex v;
            std::memcpy(&v, vertices + static_cast<std::size_t>(a_i) * sizeof(v), sizeof(v));
            return v;
        }

        std::uint16_t Index(std::uint32_t a_i) const
        {
            return static_cast<std::uint16_t>(indices[a_i * 2] | (indices[a_i * 2 + 1] << 8));
        }
    };

    struct Model
    {
        std::string                name;
        std::uint32_t              kind = Protocol::kSkinModelBody;
        std::vector<std::uint32_t> boneIds;
        std::vector<Texture>       textures;
        std::vector<Mesh>          meshes;
    };

    // The model in a blob, pointing into it; nothing when anything is out of bounds, a vertex names
    // a bone the model doesn't have or an index a vertex the mesh doesn't have.
    inline std::optional<Model> Parse(const std::uint8_t* a_blob, std::size_t a_size)
    {
        std::size_t at = 0;
        auto        take = [&](void* a_out, std::size_t a_bytes) {
            if (a_size - at < a_bytes) {
                return false;
            }
            std::memcpy(a_out, a_blob + at, a_bytes);
            at += a_bytes;
            return true;
        };

        Protocol::SkinModelHeader header{};
        if (!a_blob || !take(&header, sizeof(header)) || header.boneCount == 0 || header.boneCount > Protocol::kBodyBoneMax ||
            header.kind > Protocol::kSkinModelWeapon) {
            return std::nullopt;
        }
        Model model;
        model.name.assign(header.name, strnlen(header.name, sizeof(header.name)));
        model.kind = header.kind;
        model.boneIds.resize(header.boneCount);
        if (!take(model.boneIds.data(), header.boneCount * sizeof(std::uint32_t))) {
            return std::nullopt;
        }
        for (std::uint32_t i = 0; i < header.textureCount; i++) {
            Protocol::SkinTexture texture{};
            if (!take(&texture, sizeof(texture)) || texture.format != Protocol::kSkinTextureRgba8 || texture.width == 0 ||
                texture.height == 0 || texture.width > Protocol::kModelTextureSizeMax || texture.height > Protocol::kModelTextureSizeMax) {
                return std::nullopt;
            }
            const std::size_t bytes = static_cast<std::size_t>(texture.width) * texture.height * 4;
            if (a_size - at < bytes) {
                return std::nullopt;
            }
            model.textures.push_back({ texture.width, texture.height, a_blob + at });
            at += bytes;
        }
        for (std::uint32_t i = 0; i < header.meshCount; i++) {
            Protocol::SkinMesh mesh{};
            if (!take(&mesh, sizeof(mesh)) || mesh.vertexCount > Protocol::kModelMeshVertexMax || mesh.indexCount % 3 != 0 ||
                mesh.indexCount / 3 > Protocol::kModelMeshVertexMax ||
                mesh.texture < -1 || mesh.texture >= static_cast<std::int32_t>(header.textureCount)) {
                return std::nullopt;
            }
            const std::size_t vertexBytes = static_cast<std::size_t>(mesh.vertexCount) * sizeof(Protocol::SkinVertex);
            const std::size_t indexBytes = static_cast<std::size_t>(mesh.indexCount) * 2;
            if (a_size - at < vertexBytes + indexBytes) {
                return std::nullopt;
            }
            Mesh out{ mesh.texture, mesh.flags, { mesh.color[0], mesh.color[1], mesh.color[2], mesh.color[3] }, a_blob + at,
                mesh.vertexCount, a_blob + at + vertexBytes, mesh.indexCount };
            at += vertexBytes + indexBytes;
            for (std::uint32_t k = 0; k < out.vertexCount; k++) {
                const auto v = out.Vertex(k);
                for (int b = 0; b < 4; b++) {
                    if (!std::isfinite(v.weights[b]) || (v.weights[b] > 0.0f && v.bones[b] >= header.boneCount)) {
                        return std::nullopt;
                    }
                }
                for (int c = 0; c < 3; c++) {
                    if (!std::isfinite(v.position[c]) || !std::isfinite(v.normal[c])) {
                        return std::nullopt;
                    }
                }
            }
            for (std::uint32_t k = 0; k < out.indexCount; k++) {
                if (out.Index(k) >= out.vertexCount) {
                    return std::nullopt;
                }
            }
            model.meshes.push_back(out);
        }
        return at == a_size ? std::optional<Model>(std::move(model)) : std::nullopt;
    }

    // Triangles over the same three corners as another one. San Andreas gives a face two sides (every
    // face of a weapon) as two triangles back to back, turned and lit opposite ways, and lets backface
    // culling hide the one turned away; drawn two-sided, the pair fights over the depth buffer and each
    // triangle comes out lit or black.
    inline std::uint32_t Twins(const Mesh& a_mesh)
    {
        using Corner = std::array<float, 3>;
        std::vector<std::array<Corner, 3>> triangles;
        triangles.reserve(a_mesh.indexCount / 3);
        for (std::uint32_t t = 0; t + 2 < a_mesh.indexCount; t += 3) {
            std::array<Corner, 3> triangle;
            for (std::uint32_t k = 0; k < 3; k++) {
                const auto v = a_mesh.Vertex(a_mesh.Index(t + k));
                triangle[k] = { v.position[0], v.position[1], v.position[2] };
            }
            std::sort(triangle.begin(), triangle.end());
            triangles.push_back(triangle);
        }
        std::sort(triangles.begin(), triangles.end());
        std::uint32_t twins = 0;
        for (std::size_t i = 1; i < triangles.size(); i++) {
            if (triangles[i] == triangles[i - 1]) {
                twins++;
            }
        }
        return twins;
    }

    // Whether a mesh is drawn one-sided: San Andreas's two-sided faces, nearly every triangle in a twin pair (a weapon's).
    // A mesh with only a few twins (v19: the parachute's canopy) stays two-sided: drawn one-sided, its single faces went
    // missing from behind.
    inline bool OneSided(const Mesh& a_mesh)
    {
        const std::uint32_t triangles = a_mesh.indexCount / 3;
        return triangles > 0 && Twins(a_mesh) * 2 * 10 >= triangles * 8;  // at least 80% of the triangles paired
    }

    // Puts blobs back together from ModelChunk payloads (PerkeleRim's Model::Assembler).
    class Assembler
    {
    public:
        enum class Result
        {
            Partial,   // more chunks to come
            Complete,  // Blob() holds a whole model until the next Add
            Dropped,   // malformed or out of order; the model being received is gone
        };

        Result Add(const std::uint8_t* a_payload, std::uint32_t a_size)
        {
            Protocol::ModelChunkHeader chunk{};
            if (!a_payload || a_size < sizeof(chunk)) {
                return Drop();
            }
            std::memcpy(&chunk, a_payload, sizeof(chunk));
            if (chunk.size != a_size - sizeof(chunk) || chunk.size > Protocol::kModelChunkMax || chunk.totalSize > Protocol::kModelSizeMax ||
                chunk.offset + static_cast<std::uint64_t>(chunk.size) > chunk.totalSize) {
                return Drop();
            }
            if (chunk.offset == 0) {
                _model = chunk.model;
                _total = chunk.totalSize;
                _blob.clear();
                _blob.reserve(chunk.totalSize);
                _receiving = true;
            } else if (!_receiving || chunk.model != _model || chunk.totalSize != _total || chunk.offset != _blob.size()) {
                return Drop();
            }
            _blob.insert(_blob.end(), a_payload + sizeof(chunk), a_payload + a_size);
            if (_blob.size() < _total) {
                return Result::Partial;
            }
            _receiving = false;
            return Result::Complete;
        }

        std::uint32_t                    Id() const { return _model; }
        const std::vector<std::uint8_t>& Blob() const { return _blob; }

    private:
        Result Drop()
        {
            _receiving = false;
            _blob.clear();
            return Result::Dropped;
        }

        std::vector<std::uint8_t> _blob;
        std::uint32_t             _model = 0;
        std::uint32_t             _total = 0;
        bool                      _receiving = false;
    };
}
