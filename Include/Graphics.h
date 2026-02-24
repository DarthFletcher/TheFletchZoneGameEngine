#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <combaseapi.h>
#include <chrono>
#include <array>

#include "Utils.h"
#include "HintMacros.h"
#include "Logger.h"

#include <format>
#include <typeinfo>

// ✅ ImGui Headers
#include "imgui.h"
#include "imgui_impl_win32_custom.h"
#include "imgui_impl_dx12_custom.h"
#include <future>
#include <algorithm>

#include "SceneCamera.h"
#include "EditorCommon.h"

// Phase 4A: engine-owned mesh container
#include "Mesh.h"

// Phase 3A: scene render scaffolding (logs only).
#include "Scene.h"

extern HWND mainHwnd;
extern ID3D12DescriptorHeap* g_SRVHeap;

// Forward declaration (definition in `Mesh.h`).

struct FrameRenderBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
};

// Editor setting (kept in Graphics module for now)
inline ViewMode g_EditorViewMode = ViewMode::Mode3D;

struct PickRay
{
    DirectX::XMFLOAT3 origin;
    DirectX::XMFLOAT3 dir;
};

class Entity;

// Temporary bridge until a real Scene/World is wired into rendering.
// Implemented in `Src/Main.cpp`.
const std::vector<Entity*>& GetSceneEntitiesForRendering();

// Phase 3D: resize authority.
enum class ResizeSource : uint8_t
{
    Window,
    DPI,
    User,
    Engine,
    Unknown,
};

class Graphics {
public:
    void EnsureValidCommandQueue();
    Graphics();
    ~Graphics();

    struct CBAllocation
    {
        D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0;
        uint8_t* CpuPtr = nullptr;
    };

    CBAllocation AllocateFrameCB(size_t size);

    void ToggleVSync(bool enable);
    static bool lockResolutionWhenMaximized;
    std::future<void> LoadTextureAsync(std::string filePath);
    void CheckMSAAQuality();
    void InitializeMSAA();
    void CheckDeviceHealth();
    void AdjustResolutionBasedOnFPS(float currentFPS);
    void LogFPS();
    void UploadImGuiFontTexture();
    static bool vsyncEnabled;
    static bool allowTearing;
    static bool imguiInitialized;
    static bool imguiPlatformInitialized;

    static Graphics& GetInstance();

    static constexpr unsigned int kGraphicsAbiSignature = 0x47525831; // 'GRX1'

    D3D12_CPU_DESCRIPTOR_HANDLE imguiFontCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE imguiFontGPU = {};

    D3D12_RESOURCE_STATES currentStates[3] = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };

    UINT backBufferCount = 0;

    struct FrameContext
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 fenceValue = 0;
    };

    static constexpr UINT kBackBufferCount = 3;

    std::array<FrameContext, kBackBufferCount> frames{};

    void OnResize(HWND hWnd, UINT width, UINT height);

    ID3D12Device* GetDevice() const;
    ID3D12Device* GetImGuiDevice() const;
    ID3D12DescriptorHeap* GetImGuiSrvHeap() const;

    HWND GetHWND() const { return hWnd; }

    bool show_console = true;
    bool show_inspector = true;

    UINT lastPresentedFrame = (UINT)-1;

    bool Initialize(HWND hWnd);
    void Shutdown();
    void ResetDevice();
    void SafeReleaseResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, bool logNullRelease);

    template <typename T>
    void SafeReleaseComPtr(const char* name, Microsoft::WRL::ComPtr<T>& resource, bool logNullRelease)
    {
        if (resource) {
            uintptr_t address = reinterpret_cast<uintptr_t>(resource.Get());
            Logger::Log(LogLevel::Info, std::format("[DX12] [RELEASE] 🔄 [{}] Releasing COM Object at 0x{:X}", name, address));
            resource.Reset();
            Logger::Log(LogLevel::Info, std::format("[DX12] [OK] ✅ [{}] COM Object Released Successfully.", name));
        }
        else if (logNullRelease) {
            Logger::Log(LogLevel::Debug, std::format("[DX12] [INFO] ℹ️ [{}] SafeReleaseComPtr: NULL pointer of type '{}'.", name, typeid(T).name()));
        }
    }

    void HandleDeviceLost(HWND hWnd);
    void CheckDeviceStatus(HWND hWnd);

    void Render(HWND hWnd);
    void Present(HWND hWnd);
    void SignalFence();
    void WaitForFrame(UINT frameIndex);
    void BeginDockSpace();
    void BeginFrame(HWND hWnd);
    void EndFrame(HWND hWnd);

    void CheckFrameHealth();

    // Frame contract: forbid GPU resource creation during `Render()`.
    void AssertNotInRender(const char* reason);

    UINT64 lastSignaledFenceValue = 0;

    bool InitializeImGui(HWND hWnd);
    void CacheImGuiResources(ID3D12Device* inDevice, ID3D12DescriptorHeap* inHeap);
    void OnImGuiReady();
    void SetupImGuiFontsAndScaling(HWND hWnd);

    // Wrapper macro for call-site tracing.
    void ReloadImGuiFont(float fontSize);
    void ReloadImGuiFontImpl(float fontSize, const char* callerFile, int callerLine);

    void ProcessPendingFontReload();

    void PostImGuiInitFixes();
    bool SafeResetAllocator(ID3D12CommandAllocator* allocator, bool commandListIsOpen);

    void FlushGPU();
    void WaitForGPU(HWND hWnd);
    void ValidateCommandAllocator();
    void RecoverFromFenceError();
    void CheckFenceState();
    void RecoverFromAllocatorError(HWND hWnd);

    // Phase 1C: single helper for all fence waits
    void WaitForFenceValue(UINT64 value);

    void ListAvailableGPUs();
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
    ID3D12CommandAllocator* GetCommandAllocatorRaw() const { return commandAllocator.Get(); }

    static D3D12_CPU_DESCRIPTOR_HANDLE AllocateSRV();

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& GetImguiHeap() { return imguiHeap; }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetCommandList() const { return commandList; }

    bool IsUploadReady() const;

    // Scene viewport render target (offscreen texture) exposed to UI.
    void EnsureSceneRenderTarget(UINT width, UINT height);
    void RequestSceneRenderTargetResize(UINT width, UINT height);
    void RequestSceneRenderTargetResize(UINT width, UINT height, ResizeSource source);
    void ProcessPendingSceneRenderTargetResize();
    void RenderSceneToTarget();
    ImTextureID GetSceneImGuiTextureID() const { return sceneImGuiTextureID; }
    ImVec2 GetSceneRenderTargetSize() const { return ImVec2((float)sceneRTWidth, (float)sceneRTHeight); }

    // Editor access
    SceneCamera& GetSceneCamera() { return sceneCamera; }

    // View mode (editor setting)
    ViewMode GetViewMode() const { return g_EditorViewMode; }
    void SetViewMode(ViewMode mode) { g_EditorViewMode = mode; }

    // Diagnostics helpers (read-only)
    UINT GetBackBufferIndex() const { return currentBackBufferIndex; }
    UINT GetSwapChainWidth() const { return (UINT)screenWidth; }
    UINT GetSwapChainHeight() const { return (UINT)screenHeight; }

    UINT GetSceneRTWidth() const { return sceneRTWidth; }
    UINT GetSceneRTHeight() const { return sceneRTHeight; }
    bool IsSceneRTPendingResize() const { return pendingSceneRTResize; }
    UINT GetPendingSceneRTWidth() const { return pendingSceneRTW; }
    UINT GetPendingSceneRTHeight() const { return pendingSceneRTH; }

    // Deferred main swapchain resize (called from WM_SIZE). Applied at top of BeginFrame.
    void RequestResize(UINT width, UINT height);
    void RequestResize(UINT width, UINT height, ResizeSource source);

    // Scene picking helpers
    PickRay ComputeScenePickRay(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize) const;
    bool TryPickSceneGridY0(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize, DirectX::XMFLOAT3& outHitPos, PickRay* outRay) const;
    bool TryPickSceneGridZ0(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize, DirectX::XMFLOAT3& outHitPos, PickRay* outRay) const;

#ifndef NDEBUG
    struct DeferredReleaseDebugStats
    {
        uint64_t enqueued = 0;
        uint64_t released = 0;
        size_t peakQueue = 0;

        UINT64 oldestFence = 0;
        int stuckFrames = 0;
    };

    DeferredReleaseDebugStats drStats;
    uint64_t frameCounter = 0;
#endif

    template<typename T>
    void EnqueueDeferredRelease(Microsoft::WRL::ComPtr<T>& obj)
    {
        if (!obj)
            return;

        DeferredReleaseItem item;
        const UINT64 stampedFence = (lastSignaledFenceValue != 0) ? lastSignaledFenceValue : fenceValue;
        item.fenceValue = stampedFence;
        item.object = obj;
        deferredReleases.push_back(std::move(item));

#ifndef NDEBUG
        drStats.enqueued++;
        drStats.peakQueue = (std::max)(drStats.peakQueue, deferredReleases.size());
        if (drStats.oldestFence == 0 || item.fenceValue < drStats.oldestFence)
            drStats.oldestFence = item.fenceValue;
#endif

        obj.Reset();
    }

    template<typename T>
    void EnqueueDeferredReleaseTagged(Microsoft::WRL::ComPtr<T>& obj, const char* tag)
    {
        if (!obj)
            return;

        DeferredReleaseItem item;
        const UINT64 stampedFence = (lastSignaledFenceValue != 0) ? lastSignaledFenceValue : fenceValue;
        item.fenceValue = stampedFence;
        item.object = obj;
        item.tag = tag;
        deferredReleases.push_back(std::move(item));

#ifndef NDEBUG
        drStats.enqueued++;
        drStats.peakQueue = (std::max)(drStats.peakQueue, deferredReleases.size());
        if (drStats.oldestFence == 0 || item.fenceValue < drStats.oldestFence)
            drStats.oldestFence = item.fenceValue;
#endif

        obj.Reset();
    }

    void ProcessDeferredReleases();

    const MeshData& GetCubeMesh() const { return m_cubeMesh; }

    // CP9-B: one-shot staged upload into a GPU-local DEFAULT buffer (uses uploadCommandList/uploadAllocator).
    void UploadBufferToDefault(ID3D12Resource* dstDefault, const void* srcData, size_t numBytes);

private:
    HRESULT CreateDX12Device();
    void ExecuteCommandLists(std::vector<ID3D12CommandList*>& commandLists);
    void CreateRenderTargetViews();
    void CreateCommandInterfaces();
    void CreateSwapChain(HWND hWnd, UINT width, UINT height);
    bool ResizeSwapChainBuffers(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
    void HandleResize(HWND hWnd);
    void ApplyPendingResize(HWND hWnd);

    // Graphics owns GPU geometry (Phase 4A)
    MeshData m_cubeMesh;

    bool fontUploaded = false;
    HWND hWnd = nullptr;
    bool commandListOpen = false;
    bool frameStarted = false;
    UINT64 currentFenceValue = 0;
    bool ImGuiFrameStarted = false;

    static std::chrono::high_resolution_clock::time_point frameStart;
    static double deltaTime;
    static double totalTime;
    static int frameCount;

    int screenWidth = 1920;
    int screenHeight = 1080;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers[3];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> strongQueue;
    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList;

    UINT currentBackBufferIndex = 0;
    UINT rtvDescriptorSize = 0;
    UINT64 fenceValue = 0;
    static HANDLE fenceEvent;

    static const int FrameRenderBufferCount = 3;
    FrameRenderBuffer FrameRenderBuffers[FrameRenderBufferCount];

    Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedGPU;
    std::vector<std::wstring> gpuList;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiHeap;

    static constexpr UINT numFramesInFlight = 2;
    void OnResize(UINT, UINT);

    // Deferred font reload (until after first Present and before command list recording).
    bool pendingFontReload = false;
    float pendingFontPixelSize = 0.0f;
    bool loggedDeferredFontBeforeFirstPresent = false;

    // Log once when a resize is requested before the first present (intentional defer).
    bool loggedDeferredResizeBeforeFirstPresent = false;

    ID3D12Device* imguiDevice = nullptr;
    ID3D12DescriptorHeap* imguiSrvHeap = nullptr;

    SceneCamera sceneCamera;

    UINT pendingWidth = 0;
    UINT pendingHeight = 0;
    bool pendingResize = false;
    ResizeSource pendingResizeSource = ResizeSource::Unknown;

    UINT pendingSceneRTW = 0;
    UINT pendingSceneRTH = 0;
    bool pendingSceneRTResize = false;
    ResizeSource pendingSceneRTResizeSource = ResizeSource::Unknown;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneRenderTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneRtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle = {};

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneDepth;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDsvHandle = {};
    D3D12_RESOURCE_STATES sceneDepthState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu = {};
    ImTextureID sceneImGuiTextureID = (ImTextureID)0;

    UINT sceneRTWidth = 0;
    UINT sceneRTHeight = 0;
    D3D12_RESOURCE_STATES sceneRTState = D3D12_RESOURCE_STATE_COMMON;

    struct SceneVertex
    {
        float pos[3];
        float color[4];
    };

    struct alignas(256) SceneCBData
    {
        float viewProj[16];
        float cameraPos[3];
        float gridFadeDist;
    };

    void EnsureScenePipeline();
    void EnsureSceneGeometry();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> sceneRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sceneTrianglePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> sceneGridPSO;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneTriangleVB;
    D3D12_VERTEX_BUFFER_VIEW sceneTriangleVBV = {};

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneGridVB;
    D3D12_VERTEX_BUFFER_VIEW sceneGridVBV = {};
    UINT sceneGridVertexCount = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneCB;
    SceneCBData sceneCBData = {};
    void* sceneCBPtr = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneAxesVB;
    D3D12_VERTEX_BUFFER_VIEW sceneAxesVBV = {};
    UINT sceneAxesVertexCount = 0;

    struct DeferredReleaseItem
    {
        UINT64 fenceValue = 0;
        Microsoft::WRL::ComPtr<IUnknown> object;
        const char* tag = nullptr;
    };

    std::vector<DeferredReleaseItem> deferredReleases;

    // Unwind helper: if a frame is in a bad state, never return leaving the command list open.
    void AbortFrame(const char* why);

    // Main swapchain has successfully presented at least once.
    bool hasPresentedOnce = false;

    void Dx12HealthCheck();

    enum class FramePhase : uint8_t { None, Begun, Rendered, Ended, Presented };
    FramePhase healthPhase = FramePhase::None;

    uint64_t healthFrameCounter = 0;
    uint64_t lastHealthFrameIndex = 0;
    bool hadLastHealthFrameIndex = false;

    uint64_t lastHealthFenceValue = 0;
    bool hadLastHealthFenceValue = false;

    uint64_t lastHealthCompletedFence = 0;
    bool hadLastHealthCompletedFence = false;

    size_t dx12InfoQueueLastIndex = 0;

    std::chrono::steady_clock::time_point healthThrottleLast{};

    bool logged_FrameIndexRegressed = false;
    bool logged_FenceValueRegressed = false;
    bool logged_CompletedFenceRegressed = false;

    bool logged_FrameOrder_RenderBeforeBegin = false;
    bool logged_CommandListOpenMismatch = false;

    bool logged_Dx12_DeviceRemoved = false;
    bool logged_Dx12_SwapchainBackBufferIndexOOR = false;
    bool logged_Dx12_RtvHeapNull = false;
    bool logged_Dx12_BackBufferNull = false;

    bool m_FrameActive = false;
    bool m_InsideRender = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_FrameCB;
    uint8_t* m_FrameCBMapped = nullptr;
    size_t m_FrameCBSize = 0;
    size_t m_FrameCBOffset = 0;

    // CP10-A: deferred wait for instance uploads (avoid same-frame stall)
    UINT64 m_InstanceUploadFenceValue = 0;
    bool   m_InstanceUploadInFlight = false;
};

// Call-site tracing helper. Use everywhere instead of direct `ReloadImGuiFont()`.
#ifndef RELOAD_IMGUI_FONT
#define RELOAD_IMGUI_FONT(sizePx) Graphics::GetInstance().ReloadImGuiFontImpl((sizePx), __FILE__, __LINE__)
#endif
