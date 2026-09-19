// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "common/types.h"

namespace Input {
class GameControllers;
}

namespace Frontend {

enum class WindowSystemType : u8 {
    Headless,
    Windows,
    X11,
    Wayland,
    Metal,
};

struct WindowSystemInfo {
    // Connection to a display server. This is used on X11 and Wayland platforms.
    void* display_connection = nullptr;

    // Render surface. This is a pointer to the native window handle, which depends
    // on the platform. e.g. HWND for Windows, Window for X11. If the surface is
    // set to nullptr, the video backend will run in headless mode.
    void* render_surface = nullptr;

    // Scale of the render surface. For hidpi systems, this will be >1.
    float render_surface_scale = 1.0f;

    // Window system type. Determines which graphics presentation API is used.
    WindowSystemType type = WindowSystemType::Headless;
};

class Window {
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual s32 GetWidth() const = 0;
    [[nodiscard]] virtual s32 GetHeight() const = 0;
    [[nodiscard]] virtual bool IsOpen() const = 0;
    [[nodiscard]] virtual WindowSystemInfo GetWindowInfo() const = 0;

    // Opaque handle owned by the frontend implementation. SDL returns SDL_Window*;
    // a UWP host may return nullptr because it does not use the SDL ImGui backend.
    [[nodiscard]] virtual void* GetFrontendHandle() const = 0;

    virtual void SetIcon(std::span<const u8> png_data) = 0;
    virtual void WaitEvent() = 0;
    virtual void InitTimers() = 0;
    virtual void RequestKeyboard() = 0;
    virtual void ReleaseKeyboard() = 0;
};

using WindowFactory = std::function<std::unique_ptr<Window>(
    s32 width, s32 height, Input::GameControllers* controllers, std::string_view window_title)>;

// Temporary process-wide access for HLE input and video initialization. The type is
// frontend-neutral so a host can provide an SDL, UWP, or headless implementation.
extern Window* g_window;

} // namespace Frontend
