#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "imgui.h"

// -----------------------------------------------------------------------------
// 🎮 Fully Custom Dear ImGui DirectX 12 Backend - TheFletchZone Edition
// Author: TheFletchZone Game Engine
// Version: v1.0 Custom Standalone Backend (June 2025 build)
// -----------------------------------------------------------------------------
    
// Add this near the top of the file, after includes but before any usage || DON'T UNCOMMENT THESE LINES IF ALREADY DEFINED ELSEWHERE || Will prevent redefinition errors || Only define if not already defined || Delete if defining elsewhere
//#ifndef IMGUI_IMPL_DX12_NUM_FRAMES_IN_FLIGHT
//#define IMGUI_IMPL_DX12_NUM_FRAMES_IN_FLIGHT 3
//#endif

// ============================================================================
// 🎮 Custom Dear ImGui DirectX 12 Backend (TheFletchZone Edition)
// Author: TheFletchZone
// Description: Fully standalone ImGui DX12 renderer backend
// Supports: Full GPU control, persistent buffers, viewport support, font upload
// ============================================================================

using Microsoft::WRL::ComPtr;

// Forward Declarations
struct ImGui_ImplDX12_Data;
struct ImGui_ImplDX12_ViewportData;

//=======================================================
// Dummy Renderer Data — placeholder for main viewport
//=======================================================
struct DummyRendererData
{
    int placeholder = 0; // just to make it non-empty
    UINT32 Tag = 0xF00D; // Add this line to provide the Tag member for sanity checks
};

struct ImGui_ImplDX12_ViewportData
{
    static constexpr UINT BufferCount = 3;

    HWND Hwnd = nullptr;

    // Core DX12 objects
    Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain;

    // Compatibility with code paths that track per-viewport swapchain indexing.
    UINT BackBufferCount = BufferCount;
    UINT CurrentBackBufferIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocators[BufferCount];
    Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
    Microsoft::WRL::ComPtr<ID3D12Device> Device;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RTVHeap;

    // Render targets
    Microsoft::WRL::ComPtr<ID3D12Resource> RenderTargets[BufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[BufferCount] = {};

    // Fence tracking
    HANDLE FenceEvent = nullptr;
    UINT64 FenceValues[BufferCount] = {};
    UINT64 LastCompletedFenceValues[BufferCount] = {};

    // Dynamic vertex/index buffers (modernized)
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;

    void* MappedVertexBuffer = nullptr;
    void* MappedIndexBuffer = nullptr;
    UINT64 VertexBufferSize = 0;
    UINT64 IndexBufferSize = 0;

    // 🔍 New diagnostic fields
    UINT64 FrameCounter = 0;          // Number of frames rendered
    UINT64 LastRenderDurationNs = 0;  // Last render time
    bool IsActive = false;            // Whether viewport is alive
    bool CommandListOpen = false;     // Whether command list is open

    // Frame tracking
    UINT FrameIndex = 0;

    // FIX: Add FenceValue for compatibility with code using vd->FenceValue
    UINT64 FenceValue = 0;

    // FIX: Add SwapChainFlags for compatibility with code using vd->SwapChainFlags
    UINT SwapChainFlags = 0;

    UINT LastRenderedFrame = 0;  // 🆕 Add this field

    // NEW: per-backbuffer tracked states
    D3D12_RESOURCE_STATES CurrentStates[BufferCount] = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };

    // 🔹 Track last fence submitted for this viewport
    UINT64 LastSubmittedFenceValue = 0;

    // FIX: Move FrameResources struct outside and add as a member array
    struct FrameResources
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
        UINT64 VertexBufferSize = 0;
        UINT64 IndexBufferSize = 0;
        ImDrawVert* VertexCpuPtr = nullptr; // <-- Add this line
        ImDrawIdx* IndexCpuPtr = nullptr; // <-- Add this line
        Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // <-- Add this line
        UINT64 FenceValue = 0; // <-- Already present or add if missing
        HANDLE FenceEvent = nullptr; // <-- Add this line
    };

    FrameResources FrameResources[BufferCount]; // <-- Add this line

    ImVec2 LastSize = ImVec2(0, 0);

    bool DeviceRemoved = false;

    bool IsDummy = false;

    // --- FIX: Add IsMainViewport member ---
    bool IsMainViewport = false;

    ImGui_ImplDX12_ViewportData() {}
    ~ImGui_ImplDX12_ViewportData()
    {
        if (FenceEvent)
            CloseHandle(FenceEvent);
    }
};

// -----------------------------------------------------------------------------
// 🛠 DX12 Initialization Info Struct
// -----------------------------------------------------------------------------
struct ImGui_ImplDX12_InitInfo
{
    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* CommandQueue = nullptr;
    ID3D12GraphicsCommandList* CommandList = nullptr;
    ID3D12DescriptorHeap* SrvDescriptorHeap = nullptr;
    ID3D12DescriptorHeap* RTVDescriptorHeap = nullptr;       // ← 🆕 For viewport render targets
	UINT RTVDescriptorSize = 64;                     // ← 🆕 Size of RTV descriptor
    DXGI_FORMAT RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT NumFramesInFlight = 3; // Or define a macro like IMGUI_NUM_FRAMES_IN_FLIGHT

    IDXGIFactory4* DxgiFactory = nullptr; // Optional but useful if you’re managing swap chains

    // Optional: engine-owned main swapchain (used to bind the correct RTV for the main viewport)
    IDXGISwapChain3* SwapChain = nullptr;

    D3D12_CPU_DESCRIPTOR_HANDLE FontSrvCpuDescHandle = {}; // ✅ Optional, used if font already uploaded
    D3D12_GPU_DESCRIPTOR_HANDLE FontSrvGpuDescHandle = {};

    // Descriptor allocation callbacks
    void (*SrvDescriptorAllocFn)(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE*, D3D12_GPU_DESCRIPTOR_HANDLE*) = nullptr;
    void (*SrvDescriptorFreeFn)(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) = nullptr;

    bool VsyncEnabled = true;
    bool AllowTearing = false;
};

// -----------------------------------------------------------------------------
// 🔧 Backend Interface
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplDX12_Init(ImGui_ImplDX12_InitInfo* info);
IMGUI_IMPL_API ImGui_ImplDX12_InitInfo* ImGui_ImplDX12_GetInitInfo();
// 🔥 Backend Data Query (expose this!)
IMGUI_IMPL_API ImGui_ImplDX12_Data* ImGui_ImplDX12_GetBackendData();
IMGUI_IMPL_API ID3D12Device* ImGui_ImplDX12_GetDevice();
IMGUI_IMPL_API ID3D12CommandQueue* ImGui_ImplDX12_GetCommandQueue(); // 🆕 expose command queue
IMGUI_IMPL_API ID3D12DescriptorHeap* ImGui_ImplDX12_GetSrvHeap();
IMGUI_IMPL_API void ImGui_ImplDX12_Shutdown();
IMGUI_IMPL_API void ImGui_ImplDX12_NewFrame();
IMGUI_IMPL_API void ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data, ID3D12GraphicsCommandList* cmd_list);
IMGUI_IMPL_API void ImGui_ImplDX12_InstallPlatformHooks();
IMGUI_IMPL_API bool ImGui_ImplDX12_Recover();
IMGUI_IMPL_API bool ImGui_ImplDX12_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX12_InvalidateDeviceObjects();
IMGUI_IMPL_API bool ImGui_ImplDX12_ReloadShaders();
IMGUI_IMPL_API void ImGui_ImplDX12_InvalidateFontUploadObjects();
void ImGui_ImplDX12_SetupRenderState(ImDrawData* draw_data, ID3D12GraphicsCommandList* command_list, ID3D12DescriptorHeap* heap);

// ============================================================================================
// ♻️ Recovery / Device Loss API (TheFletchZone) & 🎯 TheFletchZone Device Loss Recovery Hooks
// ============================================================================================
IMGUI_IMPL_API void ImGui_ImplDX12_BeginRecovery();
IMGUI_IMPL_API bool ImGui_ImplDX12_RecreatePipelineObjects();
IMGUI_IMPL_API bool ImGui_ImplDX12_RecreateFontTextures();
IMGUI_IMPL_API bool ImGui_ImplDX12_RecreateSwapChains();
IMGUI_IMPL_API void ImGui_ImplDX12_EndRecovery();

// -----------------------------------------------------------------------------
// 📦 Resource Management (Fonts, Textures, Descriptors)
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplDX12_CreateFontsTexture(ID3D12Device* device, ID3D12GraphicsCommandList* command_list);
IMGUI_IMPL_API void ImGui_ImplDX12_DestroyFontsTexture();

// -----------------------------------------------------------------------------
// 🪟 Multi-Viewport Renderer Hooks (Optional PlatformIO Hooks)
// -----------------------------------------------------------------------------
IMGUI_IMPL_API void ImGui_ImplDX12_CreateWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API HRESULT ImGui_ImplDX12_PresentViewport(ImGuiViewport* viewport, ImGui_ImplDX12_ViewportData* vd);
IMGUI_IMPL_API void ImGui_ImplDX12_DestroyWindow(ImGuiViewport* viewport);
void ImGui_ImplDX12_DestroyWindowEx(ImGuiViewport* viewport, bool fullDestroy); // <-- add this
IMGUI_IMPL_API void ImGui_ImplDX12_SetWindowSize(ImGuiViewport* viewport, ImVec2 size);
IMGUI_IMPL_API void ImGui_ImplDX12_RenderWindow(ImGuiViewport* viewport, void* render_arg);
IMGUI_IMPL_API void ImGui_ImplDX12_SwapBuffers(ImGuiViewport* viewport, void* render_arg);
IMGUI_IMPL_API void ImGui_ImplDX12_UpdateWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API bool ImGui_ImplDX12_GetWindowFocus(ImGuiViewport* viewport);
IMGUI_IMPL_API bool ImGui_ImplDX12_GetWindowMinimized(ImGuiViewport* viewport);
IMGUI_IMPL_API bool ImGui_ImplDX12_CreateSwapChainAndResources(ImGuiViewport* viewport, ImGui_ImplDX12_ViewportData* vd);

// -----------------------------------------------------------------------------
// 🧱 Vertex/Index Buffer Management (TheFletchZone)
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplDX12_CreateVertexBuffers(ImGui_ImplDX12_Data* bd, ImGui_ImplDX12_ViewportData* vd);


// =============================================================================
// INTERNAL BACKEND DATA STRUCTURES (for .cpp only)
// =============================================================================

#ifdef IMGUI_IMPL_DX12_CUSTOM_CPP   // <-- this will only be defined inside your .cpp file

constexpr UINT IMGUI_NUM_FRAMES_IN_FLIGHT = 3;
constexpr UINT IMGUI_VERTEX_BUFFER_SIZE = 500000;
constexpr UINT IMGUI_INDEX_BUFFER_SIZE = 1000000;

struct ImGui_ImplDX12_FrameResources
{
    ComPtr<ID3D12Resource> VertexBuffer;
    ComPtr<ID3D12Resource> IndexBuffer;
    ImDrawVert* VertexCpuPtr = nullptr;
    ImDrawIdx* IndexCpuPtr = nullptr;
};

struct ImGui_ImplDX12_Data
{
    ImGui_ImplDX12_InitInfo InitInfo;

    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;
    ComPtr<ID3D12DescriptorHeap> SrvDescHeap;

    ComPtr<ID3D12Resource> FontTexture;
    ComPtr<ID3D12Resource> FontUploadBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE FontSrvCpuDescHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE FontSrvGpuDescHandle = {};

    ImGui_ImplDX12_FrameResources FrameResources[IMGUI_NUM_FRAMES_IN_FLIGHT];
    UINT FrameIndex = 0;
};

struct ImGui_ImplDX12_ViewportData
{
    static constexpr UINT BufferCount = 3;

    HWND Hwnd = nullptr;
    ComPtr<IDXGISwapChain3> SwapChain;
    ComPtr<ID3D12CommandQueue> CommandQueue;
    ComPtr<ID3D12GraphicsCommandList> CommandList;
    ComPtr<ID3D12CommandAllocator> CommandAllocators[BufferCount];
    ComPtr<ID3D12Fence> Fence;
    HANDLE FenceEvent = nullptr;
    UINT64 FenceValues[BufferCount]{};

    ComPtr<ID3D12DescriptorHeap> RTVHeap;
    ComPtr<ID3D12Resource> RenderTargets[BufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE RTVHandles[BufferCount]{};
    UINT FrameIndex = 0;
};

#endif