#pragma once
#include "imgui.h" // Always first

// 🎯 Extended ImGuiPlatformIO struct to include custom DX12 renderer hooks
struct ImGuiPlatformIO_Extended : public ImGuiPlatformIO
{
    // Custom Renderer function pointers
    bool (*Renderer_GetWindowFocus)(ImGuiViewport* viewport) = nullptr;
    bool (*Renderer_GetWindowMinimized)(ImGuiViewport* viewport) = nullptr;
    void (*Renderer_UpdateWindow)(ImGuiViewport* viewport) = nullptr;
};
