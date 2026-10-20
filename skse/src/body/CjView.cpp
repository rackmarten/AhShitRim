#include "body/CjView.h"

#include <DirectXPackedVector.h>

#include "body.h"
#include "skin_model.h"

namespace AhShitRim::CjView
{
    namespace
    {
        namespace W32 = REX::W32;

        // A vanilla mesh whose first plain BSTriShape is cloned for every shape built here (as in
        // PerkeleRim): the game has no constructor we can call, and a clone comes out of the game's
        // own allocator. Its data, shader and texture are all replaced.
        constexpr const char* kTemplateModel = "Meshes\\Furniture\\Cart\\CartFurnStatic01.nif";

        // Vertex layout: full precision position + bitangent x, half UV, byte normal + bitangent y,
        // byte tangent + bitangent z (how Skyrim SE stores static meshes; PerkeleRim's ModelView).
        constexpr std::uint32_t kStride = 28;
        constexpr std::uint32_t kUvOffset = 16;
        constexpr std::uint32_t kNormalOffset = 20;
        constexpr std::uint32_t kTangentOffset = 24;

        // CJ fits in this sphere around his feet whatever he does (a jump, a roll).
        const RE::NiPoint3   kBoundCenter{ 0.0f, 0.0f, 70.0f };
        constexpr float      kBoundRadius = 160.0f;
        constexpr std::uint8_t kCutoutThreshold = 128;

        struct Shape
        {
            RE::NiPointer<RE::BSTriShape> shape;
            W32::ID3D11Buffer*            buffer = nullptr;  // dynamic: rewritten every frame
            std::uint32_t                 mesh = 0;          // index into Built::model.meshes
            std::vector<std::uint8_t>     vertices;          // packed, UVs filled in once
        };

        // A received model, built once: never destroyed (the game's destructors expect geometry it
        // built itself), only detached when a newer one replaces it.
        struct Built
        {
            std::uint32_t                    id = 0;
            std::vector<std::uint8_t>        blob;
            SkinModel::Model                 model;
            RE::NiPointer<RE::NiNode>        node;
            std::vector<Shape>               shapes;
        };

        struct State
        {
            SkinModel::Assembler                         assembler;
            bool                                         dropLogged = false;
            RE::NiPointer<RE::NiNode>                    templateModel;
            RE::BSTriShape*                              templateShape = nullptr;
            bool                                         templateFailed = false;
            std::vector<std::unique_ptr<Built>>          models;  // every one built; the last is current
            std::vector<RE::NiPointer<RE::NiSourceTexture>> textures;  // never destroyed, as in PerkeleRim
            RE::NiSourceTexture*                         flatNormal = nullptr;
            RE::NiPointer<RE::NiAlphaProperty>           cutout;
            W32::ID3D11DeviceContext*                    context = nullptr;
            bool                                         shown = false;
            bool                                         mapFailLogged = false;
            std::vector<Body::Matrix34>                  bones;
        } state;

        Built* Current() { return state.models.empty() ? nullptr : state.models.back().get(); }

        std::uint8_t Byte(float a_unit) { return static_cast<std::uint8_t>(std::clamp(std::lround((a_unit + 1.0f) * 127.5f), 0L, 255L)); }

        bool LoadTemplate()
        {
            if (state.templateShape || state.templateFailed) {
                return state.templateShape != nullptr;
            }
            state.templateFailed = true;
            if (RE::BSModelDB::Demand(kTemplateModel, state.templateModel, {}) != RE::BSResource::ErrorCode::kNone || !state.templateModel) {
                SKSE::log::warn("cj view: template {} did not load", kTemplateModel);
                return false;
            }
            RE::BSVisit::TraverseScenegraphGeometries(state.templateModel.get(), [](RE::BSGeometry* a_geometry) {
                const auto* rtti = a_geometry->GetRTTI();
                if (rtti && std::string_view(rtti->GetName()) == "BSTriShape" && !a_geometry->GetGeometryRuntimeData().skinInstance) {
                    state.templateShape = a_geometry->AsTriShape();
                    return RE::BSVisit::BSVisitControl::kStop;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            if (!state.templateShape) {
                SKSE::log::warn("cj view: no plain BSTriShape in {}", kTemplateModel);
                return false;
            }
            state.templateFailed = false;
            return true;
        }

        // A GPU texture and its view wrapped in a NiSourceTexture the lighting shader can bind, put
        // together by hand (the game only makes them while loading a file), as in PerkeleRim.
        RE::NiSourceTexture* Wrap(W32::ID3D11Texture2D* a_texture, W32::ID3D11ShaderResourceView* a_view, std::uint32_t a_width,
            std::uint32_t a_height, std::uint32_t a_mips, const std::string& a_name)
        {
            auto* renderer = static_cast<RE::BSGraphics::Texture*>(RE::malloc(sizeof(RE::BSGraphics::Texture)));
            std::memset(renderer, 0, sizeof(*renderer));
            renderer->texture = a_texture;
            renderer->resourceView = a_view;
            renderer->width = static_cast<std::uint16_t>(a_width);
            renderer->height = static_cast<std::uint16_t>(a_height);
            renderer->mips = static_cast<std::uint8_t>(a_mips);
            renderer->refCount = 1;

            static REL::Relocation<std::uintptr_t> vtable{ RE::NiSourceTexture::VTABLE[0] };
            auto* source = static_cast<RE::NiSourceTexture*>(RE::malloc(sizeof(RE::NiSourceTexture)));
            std::memset(source, 0, sizeof(*source));
            *reinterpret_cast<std::uintptr_t*>(source) = vtable.address();
            source->name = a_name.c_str();
            source->rendererTexture = renderer;
            return source;
        }

        // Each mip level a 2x2 box filter of the one above.
        std::vector<std::vector<std::uint8_t>> Mips(std::uint32_t a_width, std::uint32_t a_height, const std::uint8_t* a_rgba)
        {
            std::vector<std::vector<std::uint8_t>> levels;
            levels.emplace_back(a_rgba, a_rgba + static_cast<std::size_t>(a_width) * a_height * 4);
            std::uint32_t w = a_width, h = a_height;
            while (w > 1 || h > 1) {
                const std::uint32_t       nw = (std::max)(1u, w / 2), nh = (std::max)(1u, h / 2);
                const auto&               src = levels.back();
                std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
                for (std::uint32_t y = 0; y < nh; y++) {
                    for (std::uint32_t x = 0; x < nw; x++) {
                        const std::uint32_t x0 = x * 2, x1 = (std::min)(x0 + 1, w - 1);
                        const std::uint32_t y0 = y * 2, y1 = (std::min)(y0 + 1, h - 1);
                        for (std::uint32_t c = 0; c < 4; c++) {
                            const std::uint32_t sum = src[(y0 * w + x0) * 4 + c] + src[(y0 * w + x1) * 4 + c] + src[(y1 * w + x0) * 4 + c] +
                                                      src[(y1 * w + x1) * 4 + c];
                            dst[(y * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                        }
                    }
                }
                levels.push_back(std::move(dst));
                w = nw;
                h = nh;
            }
            return levels;
        }

        RE::NiSourceTexture* MakeTexture(std::uint32_t a_width, std::uint32_t a_height, const std::uint8_t* a_rgba, const std::string& a_name)
        {
            auto* device = RE::BSGraphics::Renderer::GetDevice();
            if (!device) {
                return nullptr;
            }
            const auto               levels = Mips(a_width, a_height, a_rgba);
            W32::D3D11_TEXTURE2D_DESC desc{};
            desc.width = a_width;
            desc.height = a_height;
            desc.mipLevels = static_cast<std::uint32_t>(levels.size());
            desc.arraySize = 1;
            desc.format = W32::DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.sampleDesc = { 1, 0 };
            desc.usage = W32::D3D11_USAGE_IMMUTABLE;
            desc.bindFlags = W32::D3D11_BIND_SHADER_RESOURCE;
            std::vector<W32::D3D11_SUBRESOURCE_DATA> init;
            std::uint32_t                            w = a_width;
            for (const auto& level : levels) {
                init.push_back({ level.data(), w * 4, 0 });
                w = (std::max)(1u, w / 2);
            }
            W32::ID3D11Texture2D* texture = nullptr;
            if (!W32::SUCCESS(device->CreateTexture2D(&desc, init.data(), &texture)) || !texture) {
                SKSE::log::warn("cj view: CreateTexture2D {}x{} failed", a_width, a_height);
                return nullptr;
            }
            W32::ID3D11ShaderResourceView* view = nullptr;
            if (!W32::SUCCESS(device->CreateShaderResourceView(texture, nullptr, &view)) || !view) {
                texture->Release();
                SKSE::log::warn("cj view: CreateShaderResourceView failed");
                return nullptr;
            }
            auto* source = Wrap(texture, view, a_width, a_height, static_cast<std::uint32_t>(levels.size()), a_name);
            state.textures.emplace_back(source);
            return source;
        }

        // A normal map pointing straight out with alpha 0: Skyrim reads the specular mask from the
        // normal map's alpha, and its default one has it at full, so everything shone white in the
        // sun (PerkeleRim).
        RE::NiSourceTexture* FlatNormal()
        {
            if (!state.flatNormal) {
                std::vector<std::uint8_t> pixels;
                for (int i = 0; i < 16 * 16; i++) {
                    pixels.insert(pixels.end(), { 128, 128, 255, 0 });
                }
                state.flatNormal = MakeTexture(16, 16, pixels.data(), "AhShitRim\\FlatNormal");
            }
            return state.flatNormal;
        }

        // Alpha testing "greater than threshold" without blending (0x12EC, as vanilla NIFs), put
        // together by hand like the textures.
        RE::NiAlphaProperty* Cutout()
        {
            if (!state.cutout) {
                static REL::Relocation<std::uintptr_t> vtable{ RE::NiAlphaProperty::VTABLE[0] };
                auto* alpha = static_cast<RE::NiAlphaProperty*>(RE::malloc(sizeof(RE::NiAlphaProperty)));
                std::memset(alpha, 0, sizeof(*alpha));
                *reinterpret_cast<std::uintptr_t*>(alpha) = vtable.address();
                alpha->alphaFlags = 0x12EC;
                alpha->alphaThreshold = kCutoutThreshold;
                state.cutout.reset(alpha);
            }
            return state.cutout.get();
        }

        RE::BSGraphics::VertexDesc VertexDesc()
        {
            using V = RE::BSGraphics::Vertex;
            const std::uint64_t flags = V::VF_VERTEX | V::VF_UV | V::VF_NORMAL | V::VF_TANGENT | V::VF_FULLPREC;
            const std::uint64_t raw = (kStride / 4) | (flags << 44);
            RE::BSGraphics::VertexDesc desc;
            std::memcpy(&desc, &raw, sizeof(raw));
            desc.SetAttributeOffset(V::VA_TEXCOORD0, kUvOffset);
            desc.SetAttributeOffset(V::VA_NORMAL, kNormalOffset);
            desc.SetAttributeOffset(V::VA_BINORMAL, kTangentOffset);
            return desc;
        }

        // Any unit vector at right angles to a_n: with the flat normal map only the vertex normal
        // shades, so the tangent frame just has to be valid.
        Body::Vec3 Tangent(const Body::Vec3& a_n)
        {
            const Body::Vec3 axis = std::abs(a_n.z) < 0.9f ? Body::Vec3{ 0, 0, 1 } : Body::Vec3{ 1, 0, 0 };
            return Body::Normalize(Body::Cross(axis, a_n));
        }

        void Pack(std::uint8_t* a_out, const Body::Vec3& a_p, const Body::Vec3& a_n)
        {
            const auto  t = Tangent(a_n);
            const auto  b = Body::Cross(a_n, t);
            const float position[4] = { a_p.x, a_p.y, a_p.z, b.x };
            std::memcpy(a_out, position, sizeof(position));
            const std::uint8_t normal[4] = { Byte(a_n.x), Byte(a_n.y), Byte(a_n.z), Byte(b.y) };
            std::memcpy(a_out + kNormalOffset, normal, sizeof(normal));
            const std::uint8_t tangent[4] = { Byte(t.x), Byte(t.y), Byte(t.z), Byte(b.z) };
            std::memcpy(a_out + kTangentOffset, tangent, sizeof(tangent));
        }

        RE::BSLightingShaderProperty* MakeShader(RE::NiSourceTexture* a_diffuse)
        {
            auto& shaderState = *RE::BSGraphics::State::GetSingleton();
            auto* material = RE::BSLightingShaderMaterialBase::CreateMaterial<RE::BSLightingShaderMaterial>();
            material->diffuseTexture = RE::NiPointer<RE::NiSourceTexture>(a_diffuse ? a_diffuse : shaderState.GetRuntimeData().defaultTextureWhite.get());
            auto* normal = FlatNormal();
            material->normalTexture = normal ? RE::NiPointer<RE::NiSourceTexture>(normal) : shaderState.GetRuntimeData().defaultTextureNormalMap;
            auto* shader = RE::BSLightingShaderProperty::Create();
            shader->SetMaterial(material, true);  // the material is left alive: destroying it crashed PerkeleRim
            auto* own = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);
            own->diffuseTexture = material->diffuseTexture;
            own->normalTexture = material->normalTexture;
            own->materialAlpha = 1.0f;
            own->specularColorScale = 0.0f;
            using F = RE::BSShaderProperty::EShaderPropertyFlag;
            // Both sides drawn: CJ is a closed mesh, so the far side is behind the near one anyway, and
            // a winding San Andreas and Skyrim read differently can't turn him inside out.
            shader->flags.set(F::kReceiveShadows);
            shader->flags.set(F::kZBufferTest);
            shader->flags.set(F::kZBufferWrite);
            shader->flags.set(F::kTwoSided);
            return shader;
        }

        // A shape for one mesh, its vertex buffer dynamic so Show can pose it every frame.
        bool MakeShape(Built& a_built, std::uint32_t a_mesh, RE::NiSourceTexture* a_diffuse, Shape& a_out)
        {
            const auto& mesh = a_built.model.meshes[a_mesh];
            auto*       device = RE::BSGraphics::Renderer::GetDevice();
            if (!device || mesh.vertexCount == 0 || mesh.indexCount == 0) {
                return false;
            }
            a_out.mesh = a_mesh;
            a_out.vertices.assign(static_cast<std::size_t>(mesh.vertexCount) * kStride, 0);
            for (std::uint32_t i = 0; i < mesh.vertexCount; i++) {
                const auto v = mesh.Vertex(i);
                std::uint8_t* out = a_out.vertices.data() + static_cast<std::size_t>(i) * kStride;
                Pack(out, { v.position[0], v.position[1], v.position[2] }, Body::Normalize({ v.normal[0], v.normal[1], v.normal[2] }));
                const std::uint16_t uv[2] = { DirectX::PackedVector::XMConvertFloatToHalf(v.uv[0]), DirectX::PackedVector::XMConvertFloatToHalf(v.uv[1]) };
                std::memcpy(out + kUvOffset, uv, sizeof(uv));
            }
            const std::size_t vertexBytes = a_out.vertices.size();
            const std::size_t indexBytes = static_cast<std::size_t>(mesh.indexCount) * 2;
            auto*             rawVertices = static_cast<std::uint8_t*>(RE::malloc(vertexBytes));
            auto*             rawIndices = static_cast<std::uint16_t*>(RE::malloc(indexBytes));
            std::memcpy(rawVertices, a_out.vertices.data(), vertexBytes);
            for (std::uint32_t i = 0; i < mesh.indexCount; i++) {
                rawIndices[i] = mesh.Index(i);
            }

            W32::ID3D11Buffer*          vb = nullptr;
            W32::ID3D11Buffer*          ib = nullptr;
            W32::D3D11_BUFFER_DESC      vdesc{ static_cast<std::uint32_t>(vertexBytes), W32::D3D11_USAGE_DYNAMIC, W32::D3D11_BIND_VERTEX_BUFFER,
                     W32::D3D11_CPU_ACCESS_WRITE, 0, 0 };
            W32::D3D11_BUFFER_DESC      idesc{ static_cast<std::uint32_t>(indexBytes), W32::D3D11_USAGE_IMMUTABLE, W32::D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
            W32::D3D11_SUBRESOURCE_DATA vinit{ rawVertices, 0, 0 };
            W32::D3D11_SUBRESOURCE_DATA iinit{ rawIndices, 0, 0 };
            if (!W32::SUCCESS(device->CreateBuffer(&vdesc, &vinit, &vb)) || !W32::SUCCESS(device->CreateBuffer(&idesc, &iinit, &ib))) {
                if (vb) {
                    vb->Release();
                }
                RE::free(rawVertices);
                RE::free(rawIndices);
                SKSE::log::warn("cj view: CreateBuffer failed for mesh {}", a_mesh);
                return false;
            }
            auto* data = static_cast<RE::BSGraphics::TriShape*>(RE::malloc(sizeof(RE::BSGraphics::TriShape)));
            std::memset(data, 0, sizeof(*data));
            data->vertexBuffer = vb;
            data->indexBuffer = ib;
            data->vertexDesc = VertexDesc();
            data->refCount = 1;
            data->rawVertexData = rawVertices;
            data->rawIndexData = rawIndices;

            RE::NiPointer<RE::BSTriShape> shape(static_cast<RE::BSTriShape*>(state.templateShape->Clone()));
            shape->name = "AhShitRimCJ";
            shape->local = RE::NiTransform();
            auto& geometry = shape->GetGeometryRuntimeData();
            geometry.rendererData = data;
            geometry.vertexDesc = data->vertexDesc;
            geometry.skinInstance.reset();
            geometry.alphaProperty.reset((mesh.flags & (Protocol::kSkinMeshCutout | Protocol::kSkinMeshBlend)) != 0 ? Cutout() : nullptr);
            auto& counts = shape->GetTrishapeRuntimeData();
            counts.vertexCount = static_cast<std::uint16_t>(mesh.vertexCount);
            counts.triangleCount = static_cast<std::uint16_t>(mesh.indexCount / 3);
            auto& bound = shape->GetModelData().modelBound;
            bound.center = kBoundCenter;
            bound.radius = kBoundRadius;
            auto* shader = MakeShader(a_diffuse);
            geometry.shaderProperty.reset(shader);
            shader->SetupGeometry(shape.get());
            shader->FinishSetupGeometry(shape.get());
            a_out.shape = shape;
            a_out.buffer = vb;
            return true;
        }

        void Build(std::uint32_t a_id, const std::vector<std::uint8_t>& a_blob)
        {
            const auto start = std::chrono::steady_clock::now();
            if (!LoadTemplate()) {
                return;
            }
            auto built = std::make_unique<Built>();
            built->id = a_id;
            built->blob = a_blob;
            auto model = SkinModel::Parse(built->blob.data(), built->blob.size());
            if (!model) {
                SKSE::log::warn("cj view: model {:08X} is malformed ({} bytes)", a_id, a_blob.size());
                return;
            }
            built->model = std::move(*model);
            std::vector<RE::NiSourceTexture*> textures;
            for (std::size_t i = 0; i < built->model.textures.size(); i++) {
                const auto& t = built->model.textures[i];
                textures.push_back(MakeTexture(t.width, t.height, t.rgba, std::format("AhShitRim\\{:08X}\\{}", a_id, i)));
            }
            built->node.reset(RE::NiNode::Create(static_cast<std::uint16_t>(built->model.meshes.size())));
            built->node->name = "AhShitRimCJ";
            std::uint32_t vertices = 0, triangles = 0;
            for (std::uint32_t m = 0; m < built->model.meshes.size(); m++) {
                const auto& mesh = built->model.meshes[m];
                RE::NiSourceTexture* diffuse = nullptr;
                if (mesh.texture >= 0) {
                    diffuse = textures[mesh.texture];
                } else if (mesh.color[0] != 255 || mesh.color[1] != 255 || mesh.color[2] != 255) {
                    const std::uint8_t pixel[4] = { mesh.color[0], mesh.color[1], mesh.color[2], 255 };
                    diffuse = MakeTexture(1, 1, pixel, std::format("AhShitRim\\{:08X}\\color{}", a_id, m));
                }
                Shape shape;
                if (!MakeShape(*built, m, diffuse, shape)) {
                    continue;
                }
                built->node->AttachChild(shape.shape.get(), true);
                vertices += mesh.vertexCount;
                triangles += mesh.indexCount / 3;
                built->shapes.push_back(std::move(shape));
            }
            built->node->SetAppCulled(true);
            if (auto* old = Current(); old && old->node && old->node->parent) {
                old->node->parent->DetachChild(old->node.get());
            }
            state.shown = false;
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            SKSE::log::info("cj view: model {:08X} {} built in {:.1f} ms: {} shapes, {} vertices, {} triangles, {} textures, {} bones ({} bytes)", a_id,
                built->model.name, ms, built->shapes.size(), vertices, triangles, textures.size(), built->model.boneIds.size(), a_blob.size());
            state.models.push_back(std::move(built));
        }

        // The node the player's own 3D hangs in (the loaded cells' actor node, at world origin), so
        // CJ is lit and culled like the world around him (PerkeleRim's VehicleView).
        RE::NiNode* Scene()
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* root = player ? player->Get3D() : nullptr;
            if (root && root->parent) {
                return root->parent;
            }
            return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
        }

        W32::ID3D11DeviceContext* Context()
        {
            if (!state.context) {
                if (auto* device = RE::BSGraphics::Renderer::GetDevice()) {
                    device->GetImmediateContext(&state.context);  // kept for good
                }
            }
            return state.context;
        }
    }

    void OnChunk(const std::uint8_t* a_payload, std::uint32_t a_size)
    {
        switch (state.assembler.Add(a_payload, a_size)) {
        case SkinModel::Assembler::Result::Partial:
            state.dropLogged = false;
            return;
        case SkinModel::Assembler::Result::Dropped:
            if (!state.dropLogged) {
                state.dropLogged = true;
                SKSE::log::warn("cj view: model chunk out of order or malformed ({} bytes), model dropped", a_size);
            }
            return;
        case SkinModel::Assembler::Result::Complete:
            break;
        }
        if (auto* current = Current(); current && current->id == state.assembler.Id()) {
            SKSE::log::info("cj view: model {:08X} received again, already built", current->id);
            return;
        }
        Build(state.assembler.Id(), state.assembler.Blob());
    }

    std::uint32_t Model()
    {
        const auto* current = Current();
        return current && !current->shapes.empty() ? current->id : 0;
    }

    void Show(const Protocol::BodyState& a_body, const RE::NiPoint3& a_feet)
    {
        auto* built = Current();
        auto* context = Context();
        if (!built || built->id != a_body.model || a_body.boneCount != built->model.boneIds.size() || !context) {
            Hide();
            return;
        }
        auto* scene = Scene();
        if (scene && built->node->parent != scene) {
            if (built->node->parent) {
                built->node->parent->DetachChild(built->node.get());
            }
            scene->AttachChild(built->node.get(), true);
            SKSE::log::info("cj view: CJ attached under {}", scene->name.c_str());
        }

        state.bones.resize(a_body.boneCount);
        for (std::uint32_t i = 0; i < a_body.boneCount; i++) {
            state.bones[i] = Body::ToMatrix(a_body.bones[i]);
        }
        for (auto& shape : built->shapes) {
            const auto& mesh = built->model.meshes[shape.mesh];
            for (std::uint32_t i = 0; i < mesh.vertexCount; i++) {
                Body::Vec3 position, normal;
                Body::Skin(state.bones.data(), a_body.boneCount, mesh.Vertex(i), position, normal);
                Pack(shape.vertices.data() + static_cast<std::size_t>(i) * kStride, position, normal);
            }
            W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            if (!W32::SUCCESS(context->Map(shape.buffer, 0, W32::D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.data) {
                if (!state.mapFailLogged) {
                    state.mapFailLogged = true;
                    SKSE::log::warn("cj view: could not map CJ's vertex buffer");
                }
                continue;
            }
            std::memcpy(mapped.data, shape.vertices.data(), shape.vertices.size());
            context->Unmap(shape.buffer, 0);
        }

        built->node->local.translate = a_feet;
        built->node->local.rotate = RE::NiMatrix3();
        RE::NiUpdateData update{};
        built->node->Update(update);
        if (!state.shown) {
            state.shown = true;
            built->node->SetAppCulled(false);
            SKSE::log::info("cj view: CJ shown");
        }
    }

    void Hide()
    {
        auto* built = Current();
        if (state.shown && built && built->node) {
            built->node->SetAppCulled(true);
            SKSE::log::info("cj view: CJ hidden");
        }
        state.shown = false;
    }
}
