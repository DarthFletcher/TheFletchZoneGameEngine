#include "Graphics.h"
#include "Engine.h"

// This compilation unit exists to provide definitions for Graphics methods
// that are declared in `Graphics.h` but may not be present in `Graphics.cpp`
// (for example, after large refactors/experiments). Keeping these definitions
// centralized prevents linker breakage.

void Graphics::HandleDeviceLost(HWND hWnd)
{
    Logger::Log(LogLevel::Error, "? HandleDeviceLost called. Attempting full Graphics reset.");
    Shutdown();
    (void)Initialize(hWnd);
}

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height)
{
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    pendingSceneRTW = width;
    pendingSceneRTH = height;
    pendingSceneRTResize = true;
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

void Graphics::AbortFrame(const char* why)
{
    Logger::Log(LogLevel::Error, std::format("?? AbortFrame: {}", why ? why : "(unknown)"));

    if (commandListOpen && commandList)
        (void)commandList->Close();

    commandListOpen = false;
    frameStarted = false;
    ImGuiFrameStarted = false;
}

void Graphics::CreateRenderTargetViews()
{
    if (!device || !swapChain)
        return;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = NUM_BACK_BUFFERS;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    rtvHeap.Reset();
    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr) || !rtvHeap)
    {
        Logger::Log(LogLevel::Error, std::format("? CreateRenderTargetViews: CreateDescriptorHeap failed HR=0x{:08X}", (UINT)hr));
        return;
    }

    const UINT rtvDescriptorSizeLocal = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvDescriptorSize = rtvDescriptorSizeLocal;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i)
    {
        backBuffers[i].Reset();
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i]));
        if (FAILED(hr) || !backBuffers[i])
        {
            Logger::Log(LogLevel::Error, std::format("? CreateRenderTargetViews: GetBuffer({}) failed HR=0x{:08X}", i, (UINT)hr));
            return;
        }

        device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSizeLocal);
    }
}

// (removed) Graphics::Present(HWND) implementation now lives in `Src/Graphics.cpp`.

// (removed) Graphics::SetupImGuiFontsAndScaling(HWND) implementation now lives in `Src/Graphics.cpp`.

// (removed) Graphics::ReloadImGuiFont(float) implementation now lives in `Src/Graphics.cpp`.

// (removed) Graphics::EnsureSceneRenderTarget(UINT, UINT) implementation now lives in `Src/Graphics.cpp`.
