#include "hud/Overlay.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <ShlObj.h>
#include <wincodec.h>

#include <CommonStates.h>
#include <DirectXHelpers.h>
#include <Effects.h>
#include <PrimitiveBatch.h>
#include <ScreenGrab.h>
#include <SpriteBatch.h>
#include <VertexTypes.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>

namespace AhShitRim::Overlay
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        constexpr std::size_t kPresentSlot = 8;  // IDXGISwapChain::Present in its vtable
        constexpr auto        kStale = std::chrono::milliseconds(250);

        PresentFn original = nullptr;

        // Main thread to Present, under the lock.
        std::mutex                            lock;
        std::vector<Quad>                     quads;
        std::vector<Shape>                    shapes;
        std::chrono::steady_clock::time_point submitted;
        std::vector<Image>                    pendingImages;
        bool                                  imagesPending = false;
        std::atomic<std::uint32_t>            screenWidth{ 0 }, screenHeight{ 0 };
        std::atomic<bool>                     screenshotWanted{ false };
        std::atomic<bool>                     screenshotSaved{ false };
        std::string                           screenshotFolder;  // under the lock

        // Present only.
        struct Texture
        {
            ComPtr<ID3D11ShaderResourceView> view;
            ComPtr<ID3D11SamplerState>       border;  // shapes past its edges, when it has a border colour
            std::uint32_t                    width = 0, height = 0;
        };
        using Vertex = DirectX::VertexPositionColorTexture;
        ComPtr<ID3D11Device>                 device;
        ComPtr<ID3D11DeviceContext>          context;
        std::unique_ptr<DirectX::SpriteBatch>  batch;
        std::unique_ptr<DirectX::CommonStates> states;
        std::unique_ptr<DirectX::PrimitiveBatch<Vertex>> prims;  // the shapes
        std::unique_ptr<DirectX::BasicEffect>  effect;
        ComPtr<ID3D11InputLayout>            primLayout;
        std::vector<Texture>                 textures;  // [0] = white
        std::vector<Quad>                    drawing;
        std::vector<Shape>                   drawingShapes;

        DirectX::XMFLOAT4 ColourOf(std::uint32_t a_colour)
        {
            return { (a_colour & 0xFF) / 255.0f, ((a_colour >> 8) & 0xFF) / 255.0f, ((a_colour >> 16) & 0xFF) / 255.0f, ((a_colour >> 24) & 0xFF) / 255.0f };
        }

        Texture MakeTexture(std::uint32_t a_width, std::uint32_t a_height, const std::uint8_t* a_rgba, std::uint32_t a_border = 0)
        {
            Texture              out;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = a_width;
            desc.Height = a_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{ a_rgba, a_width * 4, 0 };
            ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
                SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &out.view))) {
                out.width = a_width;
                out.height = a_height;
            }
            if (a_border) {
                const auto         colour = ColourOf(a_border);
                D3D11_SAMPLER_DESC sampler{};
                sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
                sampler.MaxAnisotropy = 1;
                sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
                sampler.BorderColor[0] = colour.x;
                sampler.BorderColor[1] = colour.y;
                sampler.BorderColor[2] = colour.z;
                sampler.BorderColor[3] = colour.w;
                sampler.MaxLOD = D3D11_FLOAT32_MAX;
                device->CreateSamplerState(&sampler, &out.border);
            }
            return out;
        }

        bool Ready(IDXGISwapChain* a_swapChain)
        {
            if (!batch) {
                if (FAILED(a_swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(device.GetAddressOf())))) {
                    return false;
                }
                device->GetImmediateContext(&context);
                batch = std::make_unique<DirectX::SpriteBatch>(context.Get());
                states = std::make_unique<DirectX::CommonStates>(device.Get());
                prims = std::make_unique<DirectX::PrimitiveBatch<Vertex>>(context.Get());
                effect = std::make_unique<DirectX::BasicEffect>(device.Get());
                effect->SetTextureEnabled(true);
                effect->SetVertexColorEnabled(true);
                if (FAILED(DirectX::CreateInputLayoutFromEffect<Vertex>(device.Get(), effect.get(), primLayout.ReleaseAndGetAddressOf()))) {
                    spdlog::warn("overlay: no input layout for shapes, they won't be drawn");
                }
                const std::uint8_t white[4] = { 255, 255, 255, 255 };
                textures.assign(1, MakeTexture(1, 1, white));
            }
            if (imagesPending) {
                textures.resize(1);
                for (const auto& image : pendingImages) {
                    textures.push_back(image.rgba.size() >= static_cast<std::size_t>(image.width) * image.height * 4 && image.width && image.height ?
                                           MakeTexture(image.width, image.height, image.rgba.data(), image.border) :
                                           Texture{});
                }
                pendingImages.clear();
                imagesPending = false;
            }
            return true;
        }

        // The Direct3D state SpriteBatch changes, as Skyrim left it; Skyrim caches its own state and
        // must find it unchanged on its next frame (as Dear ImGui's DX11 backend does it).
        struct StateBackup
        {
            ID3D11DeviceContext*      ctx;
            UINT                      scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_RECT                scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            D3D11_VIEWPORT            viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            ID3D11RasterizerState*    rasterizer = nullptr;
            ID3D11BlendState*         blend = nullptr;
            FLOAT                     blendFactor[4]{};
            UINT                      sampleMask = 0;
            ID3D11DepthStencilState*  depth = nullptr;
            UINT                      stencilRef = 0;
            ID3D11RenderTargetView*   targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            ID3D11DepthStencilView*   depthView = nullptr;
            ID3D11ShaderResourceView* psResource = nullptr;
            ID3D11SamplerState*       psSampler = nullptr;
            ID3D11PixelShader*        ps = nullptr;
            ID3D11VertexShader*       vs = nullptr;
            ID3D11GeometryShader*     gs = nullptr;
            ID3D11HullShader*         hs = nullptr;
            ID3D11DomainShader*       ds = nullptr;
            ID3D11ClassInstance*      psInstances[256]{};
            ID3D11ClassInstance*      vsInstances[256]{};
            ID3D11ClassInstance*      gsInstances[256]{};
            UINT                      psInstanceCount = 256, vsInstanceCount = 256, gsInstanceCount = 256;
            ID3D11Buffer*             vsConstants = nullptr;
            ID3D11Buffer*             psConstants = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY  topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            ID3D11Buffer*             indexBuffer = nullptr;
            DXGI_FORMAT               indexFormat = DXGI_FORMAT_UNKNOWN;
            UINT                      indexOffset = 0;
            ID3D11Buffer*             vertexBuffer = nullptr;
            UINT                      vertexStride = 0, vertexOffset = 0;
            ID3D11InputLayout*        layout = nullptr;

            explicit StateBackup(ID3D11DeviceContext* a_context) :
                ctx(a_context)
            {
                ctx->RSGetScissorRects(&scissorCount, scissors);
                ctx->RSGetViewports(&viewportCount, viewports);
                ctx->RSGetState(&rasterizer);
                ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
                ctx->OMGetDepthStencilState(&depth, &stencilRef);
                ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depthView);
                ctx->PSGetShaderResources(0, 1, &psResource);
                ctx->PSGetSamplers(0, 1, &psSampler);
                ctx->PSGetShader(&ps, psInstances, &psInstanceCount);
                ctx->VSGetShader(&vs, vsInstances, &vsInstanceCount);
                ctx->GSGetShader(&gs, gsInstances, &gsInstanceCount);
                ctx->HSGetShader(&hs, nullptr, nullptr);
                ctx->DSGetShader(&ds, nullptr, nullptr);
                ctx->VSGetConstantBuffers(0, 1, &vsConstants);
                ctx->PSGetConstantBuffers(0, 1, &psConstants);
                ctx->IAGetPrimitiveTopology(&topology);
                ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
                ctx->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
                ctx->IAGetInputLayout(&layout);
            }

            ~StateBackup()
            {
                ctx->RSSetScissorRects(scissorCount, scissors);
                ctx->RSSetViewports(viewportCount, viewports);
                ctx->RSSetState(rasterizer);
                ctx->OMSetBlendState(blend, blendFactor, sampleMask);
                ctx->OMSetDepthStencilState(depth, stencilRef);
                ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView);
                ctx->PSSetShaderResources(0, 1, &psResource);
                ctx->PSSetSamplers(0, 1, &psSampler);
                ctx->PSSetShader(ps, psInstances, psInstanceCount);
                ctx->VSSetShader(vs, vsInstances, vsInstanceCount);
                ctx->GSSetShader(gs, gsInstances, gsInstanceCount);
                ctx->HSSetShader(hs, nullptr, 0);
                ctx->DSSetShader(ds, nullptr, 0);
                ctx->VSSetConstantBuffers(0, 1, &vsConstants);
                ctx->PSSetConstantBuffers(0, 1, &psConstants);
                ctx->IASetPrimitiveTopology(topology);
                ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
                ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
                ctx->IASetInputLayout(layout);

                // The getters added a reference to each.
                auto release = [](auto* a_object) {
                    if (a_object) {
                        a_object->Release();
                    }
                };
                release(rasterizer);
                release(blend);
                release(depth);
                for (auto* target : targets) {
                    release(target);
                }
                release(depthView);
                release(psResource);
                release(psSampler);
                release(ps);
                release(vs);
                release(gs);
                release(hs);
                release(ds);
                for (UINT i = 0; i < psInstanceCount; i++) release(psInstances[i]);
                for (UINT i = 0; i < vsInstanceCount; i++) release(vsInstances[i]);
                for (UINT i = 0; i < gsInstanceCount; i++) release(gsInstances[i]);
                release(vsConstants);
                release(psConstants);
                release(indexBuffer);
                release(vertexBuffer);
                release(layout);
            }
        };

        void Draw(IDXGISwapChain* a_swapChain)
        {
            {
                std::scoped_lock guard(lock);
                const bool fresh = std::chrono::steady_clock::now() - submitted < kStale;
                drawing.assign(fresh ? quads.begin() : quads.end(), quads.end());
                drawingShapes.assign(fresh ? shapes.begin() : shapes.end(), shapes.end());
                if (!Ready(a_swapChain)) {
                    return;
                }
            }
            ComPtr<ID3D11Texture2D> back;
            if (FAILED(a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.GetAddressOf())))) {
                return;
            }
            D3D11_TEXTURE2D_DESC desc{};
            back->GetDesc(&desc);
            screenWidth = desc.Width;
            screenHeight = desc.Height;
            if (drawing.empty()) {
                return;
            }
            ComPtr<ID3D11RenderTargetView> target;
            if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &target))) {
                return;
            }

            StateBackup backup(context.Get());
            context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
            const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f };
            context->RSSetViewports(1, &viewport);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            // Runs of quads go through SpriteBatch and runs of shapes through PrimitiveBatch, in the order they came.
            const auto    projection = DirectX::XMMatrixOrthographicOffCenterRH(0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 0.0f, 1.0f);
            bool          sprites = false, shaping = false;
            std::uint32_t shapeTexture = 0;
            const auto    endShapes = [&]() {
                if (shaping) {
                    prims->End();
                    shaping = false;
                }
            };
            for (const auto& quad : drawing) {
                if (quad.texture >= textures.size() || !textures[quad.texture].view) {
                    continue;
                }
                if (quad.shape >= 0) {
                    if (static_cast<std::size_t>(quad.shape) >= drawingShapes.size() || !primLayout) {
                        continue;
                    }
                    if (sprites) {
                        batch->End();
                        sprites = false;
                    }
                    const auto& shape = drawingShapes[quad.shape];
                    if (!shaping || shapeTexture != shape.texture) {
                        endShapes();
                        const auto& texture = textures[shape.texture];
                        effect->SetProjection(projection);
                        effect->SetTexture(texture.view.Get());
                        effect->Apply(context.Get());
                        ID3D11SamplerState* sampler = texture.border ? texture.border.Get() : states->LinearClamp();
                        context->PSSetSamplers(0, 1, &sampler);
                        context->IASetInputLayout(primLayout.Get());
                        context->OMSetBlendState(states->NonPremultiplied(), nullptr, 0xFFFFFFFF);
                        context->OMSetDepthStencilState(states->DepthNone(), 0);
                        context->RSSetState(states->CullNone());
                        prims->Begin();
                        shaping = true;
                        shapeTexture = shape.texture;
                    }
                    const auto colour = ColourOf(shape.colour);
                    Vertex     corners[4];
                    for (int i = 0; i < 4; i++) {
                        const auto& c = shape.corners[i];
                        corners[i] = Vertex(DirectX::XMFLOAT3(c.x, c.y, 0.0f), colour, DirectX::XMFLOAT2(c.u, c.v));
                    }
                    prims->DrawQuad(corners[0], corners[1], corners[2], corners[3]);
                    continue;
                }
                if (!sprites) {
                    endShapes();
                    batch->Begin(DirectX::SpriteSortMode_Deferred, states->NonPremultiplied(), states->LinearClamp(), states->DepthNone(), states->CullNone());
                    sprites = true;
                }
                const auto& texture = textures[quad.texture];
                // A quad with its corners the other way round (u1 < u0) is the texture mirrored: San Andreas draws the
                // four quarters of a scope that way.
                const bool  flipU = quad.u1 < quad.u0, flipV = quad.v1 < quad.v0;
                const float u0 = (std::min)(quad.u0, quad.u1), u1 = (std::max)(quad.u0, quad.u1);
                const float v0 = (std::min)(quad.v0, quad.v1), v1 = (std::max)(quad.v0, quad.v1);
                const RECT  source{ std::lround(u0 * texture.width), std::lround(v0 * texture.height), std::lround(u1 * texture.width),
                     std::lround(v1 * texture.height) };
                if (source.right <= source.left || source.bottom <= source.top || quad.x1 <= quad.x0 || quad.y1 <= quad.y0) {
                    continue;
                }
                const auto effects = static_cast<DirectX::SpriteEffects>((flipU ? DirectX::SpriteEffects_FlipHorizontally : 0) | (flipV ? DirectX::SpriteEffects_FlipVertically : 0));
                const DirectX::XMVECTORF32 colour{ { { (quad.colour & 0xFF) / 255.0f, ((quad.colour >> 8) & 0xFF) / 255.0f, ((quad.colour >> 16) & 0xFF) / 255.0f,
                    ((quad.colour >> 24) & 0xFF) / 255.0f } } };
                const DirectX::XMFLOAT2    scale{ (quad.x1 - quad.x0) / static_cast<float>(source.right - source.left),
                    (quad.y1 - quad.y0) / static_cast<float>(source.bottom - source.top) };
                if (quad.rotation == 0.0f) {
                    batch->Draw(texture.view.Get(), DirectX::XMFLOAT2(quad.x0, quad.y0), &source, colour, 0.0f, DirectX::XMFLOAT2(0.0f, 0.0f), scale, effects, 0.0f);
                } else {
                    // Turned about its middle: the origin is in the source's pixels, the position on the screen.
                    const DirectX::XMFLOAT2 origin{ 0.5f * static_cast<float>(source.right - source.left), 0.5f * static_cast<float>(source.bottom - source.top) };
                    batch->Draw(texture.view.Get(), DirectX::XMFLOAT2(0.5f * (quad.x0 + quad.x1), 0.5f * (quad.y0 + quad.y1)), &source, colour, quad.rotation, origin,
                        scale, effects, 0.0f);
                }
            }
            if (sprites) {
                batch->End();
            }
            endShapes();
        }

        // 2026-10-22_16-46-44.png (_2, _3 for more in the same second) in a_folder: a relative one under
        // the folder SkyrimSE.exe is in, Documents\My Games\AhShitRim\Screenshots when it's empty (as
        // PerkeleRim names and keeps its own). Empty when there's no folder to put it in.
        std::filesystem::path ScreenshotPath(const std::string& a_folder)
        {
            std::filesystem::path folder(a_folder);
            if (a_folder.empty()) {
                const auto games = MyGamesFolder();
                if (games.empty()) {
                    return {};
                }
                folder = games / "Screenshots";
            } else if (folder.is_relative()) {
                wchar_t exe[MAX_PATH]{};
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                folder = std::filesystem::path(exe).parent_path() / folder;
            }
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            const auto now = std::time(nullptr);
            std::tm    local{};
            localtime_s(&local, &now);
            const auto name = std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                local.tm_sec);
            auto path = folder / (name + ".png");
            for (int i = 2; std::filesystem::exists(path, error); i++) {
                path = folder / std::format("{}_{}.png", name, i);
            }
            return path;
        }

        // The finished back buffer, written as it is about to be shown. WIC encodes it here on the
        // render thread, so the frame it's taken on comes a little late.
        void Capture(IDXGISwapChain* a_swapChain)
        {
            ComPtr<ID3D11Device>        captureDevice;
            ComPtr<ID3D11DeviceContext> captureContext;
            ComPtr<ID3D11Texture2D>     back;
            if (FAILED(a_swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(captureDevice.GetAddressOf()))) ||
                FAILED(a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.GetAddressOf())))) {
                spdlog::warn("overlay: screenshot not taken, no back buffer");
                return;
            }
            captureDevice->GetImmediateContext(&captureContext);
            std::string folder;
            {
                std::scoped_lock guard(lock);
                folder = screenshotFolder;
            }
            const auto path = ScreenshotPath(folder);
            if (path.empty()) {
                spdlog::warn("overlay: screenshot not taken, no Documents folder");
                return;
            }
            // WIC needs COM on this thread; Skyrim may have it already, in its own mode.
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            // 24-bit: the back buffer's alpha is whatever Skyrim left in it.
            const HRESULT saved = DirectX::SaveWICTextureToFile(captureContext.Get(), back.Get(), GUID_ContainerFormatPng, path.c_str(), &GUID_WICPixelFormat24bppBGR);
            if (SUCCEEDED(com)) {
                CoUninitialize();
            }
            if (SUCCEEDED(saved)) {
                screenshotSaved = true;
                spdlog::info("overlay: screenshot saved as {}", path.string());
            } else {
                spdlog::warn("overlay: screenshot {} not saved (HRESULT {:08X})", path.string(), static_cast<std::uint32_t>(saved));
            }
        }

        HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
        {
            Draw(a_swapChain);
            if (screenshotWanted.exchange(false)) {
                Capture(a_swapChain);
            }
            return original(a_swapChain, a_syncInterval, a_flags);
        }
    }

    bool Install(void* a_swapChain)
    {
        if (original) {
            return true;
        }
        if (!a_swapChain) {
            return false;
        }
        auto** vtable = *reinterpret_cast<void***>(a_swapChain);
        DWORD  protection = 0;
        if (!VirtualProtect(&vtable[kPresentSlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection)) {
            return false;
        }
        original = reinterpret_cast<PresentFn>(vtable[kPresentSlot]);
        vtable[kPresentSlot] = reinterpret_cast<void*>(&Present);
        VirtualProtect(&vtable[kPresentSlot], sizeof(void*), protection, &protection);
        return true;
    }

    void SetImages(std::vector<Image> a_images)
    {
        std::scoped_lock guard(lock);
        pendingImages = std::move(a_images);
        imagesPending = true;
    }

    void Submit(std::vector<Quad> a_quads, std::vector<Shape> a_shapes)
    {
        std::scoped_lock guard(lock);
        quads = std::move(a_quads);
        shapes = std::move(a_shapes);
        submitted = std::chrono::steady_clock::now();
    }

    bool ScreenSize(float& a_width, float& a_height)
    {
        a_width = static_cast<float>(screenWidth.load());
        a_height = static_cast<float>(screenHeight.load());
        return a_width > 0.0f && a_height > 0.0f;
    }

    void Screenshot(std::string a_folder)
    {
        {
            std::scoped_lock guard(lock);
            screenshotFolder = std::move(a_folder);
        }
        screenshotWanted = true;
    }

    bool ScreenshotSaved()
    {
        return screenshotSaved.exchange(false);
    }

    std::filesystem::path MyGamesFolder()
    {
        PWSTR documents = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
            CoTaskMemFree(documents);
            return {};
        }
        auto folder = std::filesystem::path(documents) / "My Games" / "AhShitRim";
        CoTaskMemFree(documents);
        return folder;
    }

    bool ReadImage(const std::filesystem::path& a_path, Image& a_out)
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // Skyrim may have it already, in its own mode
        bool          read = false;
        {
            ComPtr<IWICImagingFactory>    factory;
            ComPtr<IWICBitmapDecoder>     decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter>   converter;
            UINT                          width = 0, height = 0;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(factory->CreateDecoderFromFilename(a_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) && width > 0 && height > 0 &&
                width <= 8192 && height <= 8192 &&  // a quarter of a gigabyte at most
                SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                a_out.width = width;
                a_out.height = height;
                a_out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
                read = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(a_out.rgba.size()), a_out.rgba.data()));
            }
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        if (!read) {
            a_out = {};
        }
        return read;
    }
}
