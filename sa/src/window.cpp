#include "window.h"

#include "log.h"

#include <safetyhook.hpp>

#include <Windows.h>
#include <d3d9.h>

#include <cstring>
#include <format>

namespace AhShitRim::Window
{
    namespace
    {
        bool g_visible = false;
        void (*g_onShown)() = nullptr;
        HWND g_window = nullptr;
        WNDPROC g_originalProc = nullptr;

        // San Andreas's own "the window has the focus" flag (gta-reversed: ForegroundApp). Its game
        // loop sleeps instead of running while it is false.
        bool& ForegroundApp = *reinterpret_cast<bool*>(0x8D621C);
        // psSelectDevice skips the user's video mode with UseDefaultVM set and opens GcurSelVM, and
        // RenderWare's video mode 0 is its windowed one.
        std::int32_t& UseDefaultVM = *reinterpret_cast<std::int32_t*>(0xC920FC);
        std::int32_t& GcurSelVM = *reinterpret_cast<std::int32_t*>(0x8D6220);

        // Replaces one imported function in the exe's import table; returns the original.
        void* PatchImport(const char* a_dll, const char* a_function, void* a_replacement)
        {
            auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
                if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), a_dll) != 0 || !desc->OriginalFirstThunk) {
                    continue;
                }
                auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots) {
                    if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                        continue;
                    }
                    auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) != 0) {
                        continue;
                    }
                    DWORD old = 0;
                    ::VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                    void* original = reinterpret_cast<void*>(slots->u1.Function);
                    slots->u1.Function = reinterpret_cast<std::uintptr_t>(a_replacement);
                    ::VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
                    return original;
                }
            }
            Log(std::format("window: {}!{} is not imported by the exe, not patched", a_dll, a_function));
            return nullptr;
        }

        template <class T>
        T PatchVirtual(void* a_object, int a_index, T a_replacement)
        {
            void** vtable = *static_cast<void***>(a_object);
            DWORD  old = 0;
            ::VirtualProtect(&vtable[a_index], sizeof(void*), PAGE_READWRITE, &old);
            auto original = reinterpret_cast<T>(vtable[a_index]);
            vtable[a_index] = reinterpret_cast<void*>(a_replacement);
            ::VirtualProtect(&vtable[a_index], sizeof(void*), old, &old);
            return original;
        }

        // San Andreas asks for its fullscreen video mode; Direct3D gets a windowed copy of that, and
        // Present scales the back buffer into the window's client area. RenderWare keeps its own
        // parameters untouched: it reads them back, and in windowed mode it would size its camera
        // to the window and fail.
        D3DPRESENT_PARAMETERS Windowed(const D3DPRESENT_PARAMETERS& a_params)
        {
            D3DPRESENT_PARAMETERS params = a_params;
            params.Windowed = TRUE;
            params.FullScreen_RefreshRateInHz = 0;
            params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            return params;
        }

        // ---- D3D9 ----

        using ResetFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
        using CreateDeviceFn = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
        
        ResetFn           g_reset = nullptr;
        CreateDeviceFn    g_createDevice = nullptr;
        SafetyHookInline  g_direct3DCreate9;

        HRESULT WINAPI Reset(IDirect3DDevice9* a_device, D3DPRESENT_PARAMETERS* a_params)
        {
            if (!a_params) {
                return g_reset(a_device, a_params);
            }
            auto params = Windowed(*a_params);
            return g_reset(a_device, &params);
        }

        HRESULT WINAPI CreateDevice(IDirect3D9* a_d3d, UINT a_adapter, D3DDEVTYPE a_type, HWND a_focus, DWORD a_flags,
            D3DPRESENT_PARAMETERS* a_params, IDirect3DDevice9** a_device)
        {
            if (!a_params) {
                return g_createDevice(a_d3d, a_adapter, a_type, a_focus, a_flags, a_params, a_device);
            }
            auto          params = Windowed(*a_params);
            const HRESULT hr = g_createDevice(a_d3d, a_adapter, a_type, a_focus, a_flags, &params, a_device);
            if (SUCCEEDED(hr) && a_device && *a_device) {
                if (!g_reset) {
                    g_reset = PatchVirtual(*a_device, 16, &Reset);  // IDirect3DDevice9::Reset
                }
                Log(std::format("window: Direct3D device created windowed, back buffer {}x{}", params.BackBufferWidth, params.BackBufferHeight));
            } else {
                Log(std::format("window: CreateDevice failed (0x{:08X})", static_cast<std::uint32_t>(hr)));
            }
            return hr;
        }

        IDirect3D9* WINAPI Direct3DCreate9(UINT a_sdk)
        {
            IDirect3D9* d3d = g_direct3DCreate9.stdcall<IDirect3D9*>(a_sdk);
            if (d3d && !g_createDevice) {
                g_createDevice = PatchVirtual(d3d, 16, &CreateDevice);  // IDirect3D9::CreateDevice
            }
            return d3d;
        }

        // ---- Window ----

        LRESULT CALLBACK WndProc(HWND a_window, UINT a_msg, WPARAM a_w, LPARAM a_l)
        {
            switch (a_msg) {
            case WM_ACTIVATE:
                if (LOWORD(a_w) == WA_INACTIVE) {
                    ForegroundApp = true;
                    return 0;  // San Andreas would pause its sounds and its loop
                }
                break;
            case WM_ACTIVATEAPP:
                return 0;  // would clear the pads and restore the gamma
            case WM_KILLFOCUS:
            case WM_SETFOCUS:
                ForegroundApp = true;
                return 0;  // the focus coming back would open San Andreas's menu
            default:
                break;
            }
            return ::CallWindowProcA(g_originalProc, a_window, a_msg, a_w, a_l);
        }

        using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
        ShowWindowFn g_showWindow = nullptr;

        BOOL WINAPI ShowWindowHook(HWND a_window, int a_cmd)
        {
            if (g_window || !a_window || ::GetParent(a_window)) {
                return g_showWindow(a_window, a_cmd);
            }
            // San Andreas's main window, shown once by WinMain after the Direct3D device exists.
            g_window = a_window;
            g_originalProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrA(a_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
            ::SetWindowLongPtrA(a_window, GWL_STYLE, WS_OVERLAPPEDWINDOW);
            RECT rect{ 0, 0, kVisibleWidth, kVisibleHeight };
            ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
            ::SetWindowPos(a_window, HWND_BOTTOM, 40, 40, rect.right - rect.left, rect.bottom - rect.top,
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ::SetWindowTextA(a_window, "AhShitRim: San Andreas");
            ForegroundApp = true;
            Log(g_visible ? "window: shown in a small window, without the focus (-ahshitrim-visible)" : "window: minimized");
            const BOOL result = g_showWindow(a_window, g_visible ? SW_SHOWNOACTIVATE : SW_SHOWMINNOACTIVE);
            if (g_onShown) {
                g_onShown();
            }
            return result;
        }

        BOOL WINAPI SetCursorPosHook(int, int)
        {
            return TRUE;
        }
    }

    void Install(bool a_visible)
    {
        g_visible = a_visible;
        UseDefaultVM = 1;
        GcurSelVM = 0;
        // RenderWare's driver doesn't import it by name, so its export is hooked in d3d9.dll itself.
        if (HMODULE d3d9 = ::LoadLibraryW(L"d3d9.dll")) {
            if (void* create = reinterpret_cast<void*>(::GetProcAddress(d3d9, "Direct3DCreate9"))) {
                g_direct3DCreate9 = safetyhook::create_inline(create, reinterpret_cast<void*>(&Direct3DCreate9));
            }
        }
        g_showWindow = reinterpret_cast<ShowWindowFn>(PatchImport("user32.dll", "ShowWindow", reinterpret_cast<void*>(&ShowWindowHook)));
        PatchImport("user32.dll", "SetCursorPos", reinterpret_cast<void*>(&SetCursorPosHook));
        if (!g_direct3DCreate9 || !g_showWindow) {
            Log("window: could not patch San Andreas's window; it will start fullscreen");
        }
    }

    bool Visible()
    {
        return g_visible;
    }

    void OnShown(void (*a_callback)())
    {
        g_onShown = a_callback;
    }
}
