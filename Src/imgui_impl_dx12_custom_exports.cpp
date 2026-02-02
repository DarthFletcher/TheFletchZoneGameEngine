#include "imgui_impl_dx12_custom.h"
#include "Logger.h"

// These small exported wrappers are here to ensure the symbols that the engine expects
// are always present at link time.

IMGUI_IMPL_API void ImGui_ImplDX12_DestroyWindowEx(ImGuiViewport* viewport, bool /*fullDestroy*/)
{
    if (!viewport)
        return;

    // Avoid freeing GPU resources from this stub TU. The engine disables viewports.
    viewport->RendererUserData = nullptr;
}

IMGUI_IMPL_API HRESULT ImGui_ImplDX12_PresentViewport(ImGuiViewport* viewport, ImGui_ImplDX12_ViewportData* vd)
{
    if (!viewport || !vd || !vd->SwapChain)
        return E_INVALIDARG;

    if (vd->IsDummy)
        return S_OK;

    return vd->SwapChain->Present(0, 0);
}

IMGUI_IMPL_API void ImGui_ImplDX12_DestroyWindow(ImGuiViewport* viewport)
{
    ImGui_ImplDX12_DestroyWindowEx(viewport, true);
}

IMGUI_IMPL_API void ImGui_ImplDX12_BeginRecovery()
{
    Logger::Log(LogLevel::Info, "ImGui DX12 Recovery Begin", "ImGuiDX12");
}

IMGUI_IMPL_API bool ImGui_ImplDX12_RecreatePipelineObjects()
{
    Logger::Log(LogLevel::Info, "ImGui DX12 RecreatePipelineObjects: not implemented", "ImGuiDX12");
    return false;
}

IMGUI_IMPL_API bool ImGui_ImplDX12_RecreateFontTextures()
{
    Logger::Log(LogLevel::Info, "ImGui DX12 RecreateFontTextures: not implemented", "ImGuiDX12");
    return false;
}

IMGUI_IMPL_API bool ImGui_ImplDX12_RecreateSwapChains()
{
    Logger::Log(LogLevel::Info, "ImGui DX12 RecreateSwapChains: not implemented", "ImGuiDX12");
    return false;
}

IMGUI_IMPL_API void ImGui_ImplDX12_EndRecovery()
{
    Logger::Log(LogLevel::Info, "ImGui DX12 Recovery Complete", "ImGuiDX12");
}

IMGUI_IMPL_API void ImGui_ImplDX12_UpdateWindow(ImGuiViewport* viewport)
{
    (void)viewport;
}

IMGUI_IMPL_API bool ImGui_ImplDX12_GetWindowFocus(ImGuiViewport* viewport)
{
    if (!viewport)
        return false;

    HWND hwnd = (HWND)viewport->PlatformHandle;
    return hwnd && (::GetFocus() == hwnd);
}

IMGUI_IMPL_API bool ImGui_ImplDX12_GetWindowMinimized(ImGuiViewport* viewport)
{
    if (!viewport)
        return true;

    HWND hwnd = (HWND)viewport->PlatformHandle;
    if (!hwnd)
        return true;

    return ::IsIconic(hwnd) != FALSE;
}

bool ImGui_ImplDX12_CreateSwapChainAndResources(ImGuiViewport* viewport, ImGui_ImplDX12_ViewportData* vd)
{
    return false;
}

IMGUI_IMPL_API void ImGui_ImplDX12_SwapBuffers(ImGuiViewport* viewport, void* render_arg)
{
    if (!viewport)
        return;

    auto* vd = (ImGui_ImplDX12_ViewportData*)viewport->RendererUserData;
    if (!vd)
        return;

    (void)ImGui_ImplDX12_PresentViewport(viewport, vd);
    (void)render_arg;
}

IMGUI_IMPL_API void ImGui_ImplDX12_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    if (!viewport)
        return;

    // Engine already handles sizing; renderer just accepts the callback.
    (void)size;
}

// NOTE: `ImGui_ImplDX12_CreateWindow` is implemented in `imgui_impl_dx12_custom.cpp`.
// Keeping a second definition here causes LNK2005/LNK1169.
// Only declare it so this TU can still reference/link against the real implementation.
IMGUI_IMPL_API void ImGui_ImplDX12_CreateWindow(ImGuiViewport* viewport);

IMGUI_IMPL_API void ImGui_ImplDX12_RenderWindow(ImGuiViewport* viewport, void* render_arg)
{
    // Default PlatformIO path expects this symbol to exist.
    // The engine may render via `ImGui_ImplDX12_RenderDrawData()` directly.
    (void)viewport;
    (void)render_arg;
}
