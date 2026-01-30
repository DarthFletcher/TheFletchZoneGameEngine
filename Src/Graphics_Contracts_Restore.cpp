#include "Graphics.h"
#include "Engine.h"

// This compilation unit exists to provide definitions for Graphics methods
// that are declared in `Graphics.h` but may not be present in `Graphics.cpp`
// (for example, after large refactors/experiments). Keeping these definitions
// centralized prevents linker breakage.

void Graphics::RequestResize(UINT width, UINT height)
{
    pendingWidth = width;
    pendingHeight = height;
    pendingResize = true;
}

void Graphics::HandleDeviceLost(HWND hWnd)
{
    Logger::Log(LogLevel::Error, "? HandleDeviceLost called. Attempting full Graphics reset.");
    Shutdown();
    (void)Initialize(hWnd);
}

void Graphics::ApplyPendingResize(HWND hWnd)
{
    if (!pendingResize)
        return;

    if (commandListOpen)
        return;

    if (!swapChain || !device)
        return;

    if (pendingWidth == 0 || pendingHeight == 0)
        return;

    FlushGPU();

    // Phase 1A: defer release of old backbuffers (future-proof even with FlushGPU).
    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i)
        EnqueueDeferredRelease(backBuffers[i]);

    DXGI_SWAP_CHAIN_DESC desc{};
    const HRESULT hrDesc = swapChain->GetDesc(&desc);
    if (FAILED(hrDesc))
    {
        Logger::Log(LogLevel::Error, std::format("? ApplyPendingResize: swapChain->GetDesc failed HR=0x{:08X}", (UINT)hrDesc));
        return;
    }

    const HRESULT hr = swapChain->ResizeBuffers(NUM_BACK_BUFFERS, pendingWidth, pendingHeight, desc.BufferDesc.Format, desc.Flags);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("? ApplyPendingResize: ResizeBuffers failed HR=0x{:08X}", (UINT)hr));
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            HandleDeviceLost(hWnd);
        return;
    }

    screenWidth = (int)pendingWidth;
    screenHeight = (int)pendingHeight;
    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    CreateRenderTargetViews();

    pendingResize = false;
}

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height)
{
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    pendingSceneRTW = width;
    pendingSceneRTH = height;
    pendingSceneRTResize = true;
}

void Graphics::ProcessPendingSceneRenderTargetResize()
{
    if (!pendingSceneRTResize)
        return;

    if (commandListOpen)
        return;

    pendingSceneRTResize = false;
    EnsureSceneRenderTarget(pendingSceneRTW, pendingSceneRTH);
}

void Graphics::ProcessDeferredReleases()
{
    if (deferredReleases.empty() || !fence)
        return;

    const UINT64 completed = fence->GetCompletedValue();

    size_t releasedThisFrame = 0;

    size_t write = 0;
    for (size_t read = 0; read < deferredReleases.size(); ++read)
    {
        if (deferredReleases[read].fenceValue <= completed)
        {
            deferredReleases[read].object.Reset();
            ++releasedThisFrame;
        }
        else
        {
            deferredReleases[write++] = std::move(deferredReleases[read]);
        }
    }

    deferredReleases.resize(write);

#ifndef NDEBUG
    drStats.released += releasedThisFrame;

    if (!deferredReleases.empty())
    {
        UINT64 minFence = UINT64_MAX;
        for (const auto& it : deferredReleases)
            minFence = (std::min)(minFence, it.fenceValue);

        if (minFence > completed)
        {
            drStats.stuckFrames++;
            if (drStats.stuckFrames == 300)
            {
                Logger::Log(LogLevel::Warning, std::format(
                    "?? DeferredRelease appears stuck: completedFence={} minQueuedFence={} queueSize={} (300 frames)",
                    completed, minFence, deferredReleases.size()));
            }
        }
        else
        {
            drStats.stuckFrames = 0;
        }

        drStats.oldestFence = minFence;
    }
    else
    {
        drStats.stuckFrames = 0;
        drStats.oldestFence = 0;
    }
#endif

    if (deferredReleases.size() > 5000)
        Logger::Log(LogLevel::Warning, "?? Deferred release queue is very large; possible lifetime leak.");
}
