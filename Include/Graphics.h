#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <combaseapi.h>
#include <chrono>

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

extern HWND mainHwnd;
extern ID3D12DescriptorHeap* g_SRVHeap;

struct FrameRenderBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
};

class Graphics {
public:
    void EnsureValidCommandQueue();
    Graphics();
    ~Graphics();

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
    std::vector<UINT64> mainFenceValues;

    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> commandAllocators;

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

    UINT64 lastSignaledFenceValue = 0;

    bool InitializeImGui(HWND hWnd);
    void CacheImGuiResources(ID3D12Device* inDevice, ID3D12DescriptorHeap* inHeap);
    void OnImGuiReady();
    void SetupImGuiFontsAndScaling(HWND hWnd);
    void ReloadImGuiFont(float fontSize);
    void PostImGuiInitFixes();
    bool SafeResetAllocator(ID3D12CommandAllocator* allocator, bool commandListIsOpen);

    void FlushGPU();
    void WaitForGPU(HWND hWnd);
    void ValidateCommandAllocator();
    void RecoverFromFenceError();
    void CheckFenceState();
    void RecoverFromAllocatorError(HWND hWnd);

    void ListAvailableGPUs();
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue.Get(); }
    ID3D12CommandAllocator* GetCommandAllocatorRaw() const { return commandAllocator.Get(); }

    static D3D12_CPU_DESCRIPTOR_HANDLE AllocateSRV();

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& GetImguiHeap() { return imguiHeap; }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetCommandList() const { return commandList; }

    bool IsUploadReady() const;

    // Scene viewport render target (offscreen texture) exposed to UI.
    void EnsureSceneRenderTarget(UINT width, UINT height);
    void RenderSceneToTarget();
    ImTextureID GetSceneImGuiTextureID() const { return sceneImGuiTextureID; }
    ImVec2 GetSceneRenderTargetSize() const { return ImVec2((float)sceneRTWidth, (float)sceneRTHeight); }

private:
    HRESULT CreateDX12Device();
    void ExecuteCommandLists(std::vector<ID3D12CommandList*>& commandLists);
    void CreateRenderTargetViews();
    void CreateCommandInterfaces();
    void CreateSwapChain(HWND hWnd, UINT width, UINT height);
    bool ResizeSwapChainBuffers(UINT bufferCount, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
    void HandleResize(HWND hWnd);

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
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> fontAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> fontCommandList;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> imguiCommandQueue;

    UINT currentBackBufferIndex = 0;
    UINT rtvDescriptorSize = 0;
    UINT64 fenceValue = 0;
    UINT64 fenceValues[3] = {};
    static HANDLE fenceEvent;

    static const int FrameRenderBufferCount = 3;
    FrameRenderBuffer FrameRenderBuffers[FrameRenderBufferCount];

    Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedGPU;
    std::vector<std::wstring> gpuList;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiHeap;

    static constexpr UINT numFramesInFlight = 2;
    void OnResize(UINT, UINT);

    float pendingFontSizeReload = 0.0f;
    inline static bool lockResolutionOnMaximize = true;

    ID3D12Device* imguiDevice = nullptr;
    ID3D12DescriptorHeap* imguiSrvHeap = nullptr;

    // Offscreen Scene panel render target (RTV+SRV)
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneRenderTarget;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneRtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle = {};

    // SRV lives in the ImGui shader-visible heap; keep handles + ImTextureID for ImGui::Image
    D3D12_CPU_DESCRIPTOR_HANDLE sceneSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneSrvGpu = {};
    ImTextureID sceneImGuiTextureID = (ImTextureID)0;

    UINT sceneRTWidth = 0;
    UINT sceneRTHeight = 0;
    D3D12_RESOURCE_STATES sceneRTState = D3D12_RESOURCE_STATE_COMMON;
};
