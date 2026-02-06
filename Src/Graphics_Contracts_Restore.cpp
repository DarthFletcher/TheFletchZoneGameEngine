#include "Graphics.h"
#include "Engine.h"

// TEMPORARY CONTRACT RESTORATION LAYER
// Functions in this compilation unit exist only to satisfy legacy link contracts
// during refactors. They should migrate back to their owning subsystem over time.

// Keep this file LIMITED to true ABI/legacy shims.

void Graphics::HandleDeviceLost(HWND hWnd)
{
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        Logger::Log(LogLevel::Error, "HandleDeviceLost legacy shim hit (should be rare)", "[DX12]");
    }

    Shutdown();
    (void)Initialize(hWnd);
}

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height)
{
    RequestSceneRenderTargetResize(width, height, ResizeSource::Unknown);
}

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height, ResizeSource source)
{
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    pendingSceneRTW = width;
    pendingSceneRTH = height;
    pendingSceneRTResizeSource = source;
    pendingSceneRTResize = true;
}
