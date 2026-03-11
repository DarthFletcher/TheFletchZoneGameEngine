#include "Graphics.h"
#include "Engine.h"
#include <chrono>
#include <thread>

// TEMPORARY CONTRACT RESTORATION LAYER
// Functions in this compilation unit exist only to satisfy legacy link contracts
// during refactors. They should migrate back to their owning subsystem over time.

// --- Device loss / frame contract glue (Graphics core) --------------------------------

void Graphics::HandleDeviceLost(HWND hWnd)
{
    Logger::Log(LogLevel::Error, "HandleDeviceLost called. Attempting full Graphics reset.", "[DX12]");

    Shutdown();

    // After a GPU hang/removal, the driver may take a moment to recover.
    // Retry a few times with backoff; if it still fails, request app shutdown.
    constexpr int kMaxAttempts = 5;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        const int backoffMs = 250 * attempt;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));

        Logger::Log(LogLevel::Warning,
            std::format("[DeviceLost] Recreate attempt {}/{} (backoff={}ms)", attempt, kMaxAttempts, backoffMs),
            "[DX12]");

        if (Initialize(hWnd))
        {
            Logger::Log(LogLevel::Info, "[DeviceLost] Graphics successfully reinitialized.", "[DX12]");
            return;
        }
    }

    Logger::Log(LogLevel::Error, "[DeviceLost] Failed to recreate Graphics device after retries. Exiting.", "[DX12]");
    PostQuitMessage(0);
}

// --- Scene / RenderTarget ownership (scene RT management candidates) -----------------

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height)
{
    RequestSceneRenderTargetResize(width, height, ResizeSource::Unknown);
}

void Graphics::RequestSceneRenderTargetResize(UINT width, UINT height, ResizeSource source)
{
    // Phase 3D: record request only (no GPU work here).
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    pendingSceneRTW = width;
    pendingSceneRTH = height;
    pendingSceneRTResizeSource = source;
    pendingSceneRTResize = true;
}

void Graphics::ProcessDeferredReleases()
{
    // RESTORE_CANDIDATE: consider migrating into a dedicated Graphics_ResourceLifetime.cpp when splitting.
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
                    "DeferredRelease appears stuck: completedFence={} minQueuedFence={} queueSize={} (300 frames)",
                    completed, minFence, deferredReleases.size()), "[DX12]");
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
        Logger::Log(LogLevel::Warning, "Deferred release queue is very large; possible lifetime leak.", "[DX12]");
}

void Graphics::AbortFrame(const char* why)
{
    // RESTORE_CANDIDATE: should live next to frame lifecycle methods in Graphics.cpp.
    Logger::Log(LogLevel::Error, std::format("AbortFrame: {}", why ? why : "(unknown)"), "[Core]");

    if (commandListOpen && commandList)
        (void)commandList->Close();

    commandListOpen = false;
    frameStarted = false;
    ImGuiFrameStarted = false;
}

// --- Swapchain / backbuffer ownership (resize + RTV creation candidates) --------------

void Graphics::CreateRenderTargetViews()
{
    // RESTORE_CANDIDATE: once Graphics.cpp is split, migrate into a Graphics_Swapchain.cpp unit.
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
        Logger::Log(LogLevel::Error, std::format("CreateRenderTargetViews: CreateDescriptorHeap failed HR=0x{:08X}", (UINT)hr), "[DX12]");
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
            Logger::Log(LogLevel::Error, std::format("CreateRenderTargetViews: GetBuffer({}) failed HR=0x{:08X}", i, (UINT)hr), "[DX12]");
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

void Graphics::ProcessPendingSceneRenderTargetResize()
{
    // RESTORE_CANDIDATE: should live alongside scene RT ownership (Graphics.cpp or a future Graphics_Scene.cpp).
    if (!pendingSceneRTResize)
        return;

    const UINT w = pendingSceneRTW;
    const UINT h = pendingSceneRTH;
    const ResizeSource src = pendingSceneRTResizeSource;

    pendingSceneRTResize = false;
    pendingSceneRTResizeSource = ResizeSource::Unknown;

    EnsureSceneRenderTarget(w, h);

    Logger::Log(LogLevel::Info, std::format("[Resize] Applied SceneRT w={} h={} source={}", w, h,
        (src == ResizeSource::Window) ? "Window" :
        (src == ResizeSource::DPI) ? "DPI" :
        (src == ResizeSource::User) ? "User" :
        (src == ResizeSource::Engine) ? "Engine" : "Unknown"), "[DX12]");
}

void Graphics::HandleResize(HWND hWnd)
{
    // RESTORE_CANDIDATE: once resize ownership is fully centralized, remove this indirection.
    // Existing engine uses deferred resize requests and applies them at the top of BeginFrame.
    // `ApplyPendingResize()` contains the hasPresentedOnce gating.
    ApplyPendingResize(hWnd);
}

void Graphics::ProcessPendingFontReload()
{
    // Contract restoration: this symbol is referenced by BeginFrame().
    // Font reload behavior remains owned by Graphics.cpp; if the implementation was moved,
    // keep this as a no-op until consolidated.
}

// NOTE: Some builds may compile `Graphics.cpp` variants where resize implementations are excluded.
// Provide minimal link-contract restorations here to keep the engine buildable.

void Graphics::RequestResize(UINT width, UINT height)
{
    RequestResize(width, height, ResizeSource::Unknown);
}

void Graphics::RequestResize(UINT width, UINT height, ResizeSource source)
{
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    pendingWidth = width;
    pendingHeight = height;
    pendingResizeSource = source;
    pendingResize = true;
}

void Graphics::ApplyPendingResize(HWND hWnd)
{
    if (!pendingResize)
        return;

    // Contract: some drivers reject ResizeBuffers before the first successful Present.
    // BeginFrame already gates this, but enforce it here too.
    if (!hasPresentedOnce)
        return;

    const UINT w = pendingWidth;
    const UINT h = pendingHeight;
    const ResizeSource src = pendingResizeSource;

    // Clear request early to avoid re-entrancy loops (we'll re-request on failure).
    pendingResize = false;
    pendingResizeSource = ResizeSource::Unknown;

    if (!hWnd || !IsWindow(hWnd))
        return;

    if (w == 0 || h == 0)
        return;

    if (IsIconic(hWnd))
        return;

    if (!device || !swapChain || !commandQueue || !fence)
        return;

    // Never resize while recording.
    if (commandListOpen)
        return;

    Logger::Log(LogLevel::Info, std::format(
        "[Resize] Applying SwapChain w={} h={} source={}",
        w, h,
        (src == ResizeSource::Window) ? "Window" :
        (src == ResizeSource::DPI) ? "DPI" :
        (src == ResizeSource::User) ? "User" :
        (src == ResizeSource::Engine) ? "Engine" : "Unknown"), "[DX12]");

    // Ensure GPU idle before releasing and resizing.
    FlushGPU();

    // Release backbuffer refs and RTV heap before ResizeBuffers.
    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i)
        backBuffers[i].Reset();
    rtvHeap.Reset();

    const UINT flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const HRESULT hr = swapChain->ResizeBuffers(
        NUM_BACK_BUFFERS,
        w,
        h,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        flags);

    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ ResizeBuffers failed HR=0x{:08X} (w={} h={})",
            (UINT)hr, w, h), "[DX12]");

        // Keep a pending request so we can retry next frame.
        pendingWidth = w;
        pendingHeight = h;
        pendingResizeSource = src;
        pendingResize = true;
        return;
    }

    screenWidth = (int)w;
    screenHeight = (int)h;

    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    CreateRenderTargetViews();

    // Phase 4A/4B: keep the scene render target in sync with the swapchain size.
    // This is required so `RenderSceneToTarget()` can run during splash/editor.
    EnsureSceneRenderTarget(w, h);
}
