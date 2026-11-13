#pragma once

// San Andreas's window: windowed instead of fullscreen, minimized (or, with -ahshitrim-visible, a
// small window that never takes the focus), and never paused for losing the focus.
//
// - RenderWare opens its windowed video mode (0) instead of the one in San Andreas's settings, and
//   Direct3DCreate9 is hooked so every device it creates or resets is windowed and without vsync
//   (AhShitRim caps the frame rate itself).
// - ShowWindow is patched for San Andreas's main window: a normal overlapped window, minimized or
//   shown without activating it, once; later calls leave it as it is.
// - It never comes to the front (its input comes from Skyrim over the link): CreateWindowExA makes
//   it without WS_VISIBLE and with WS_EX_NOACTIVATE, SetFocus, SetActiveWindow,
//   SetForegroundWindow and BringWindowToTop on it do nothing, and WinMain's
//   SPI_SETFOREGROUNDLOCKTIMEOUT 0 (which would let any program take the foreground) is skipped.
// - SetCursorPos is a no-op for San Andreas, so it never moves the cursor under Skyrim.
// - The window procedure is wrapped: focus loss (WM_ACTIVATE, WM_ACTIVATEAPP, WM_KILLFOCUS) never
//   reaches San Andreas, which would otherwise pause its game loop, pause its sounds and open its
//   menu when the focus comes back.
namespace AhShitRim::Window
{
    inline constexpr int kVisibleWidth = 800;
    inline constexpr int kVisibleHeight = 600;

    void Install(bool a_visible);  // at ASI load, before WinMain
    bool Visible();
    void OnShown(void (*a_callback)());  // called when WinMain first shows the window, before its game loop
}
