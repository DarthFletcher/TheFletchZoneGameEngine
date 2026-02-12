// ==========================================================================
// Custom Dear ImGui DirectX 12 Backend - TheFletchZone Edition
// Author: TheFletchZone
// Description: Fully custom standalone backend for DX12 rendering
// ============================================================================

//-----------------------------------------------------------------------------
// Preprocessor Config
// 
// Fixes std::min / std::max macro clash
//-----------------------------------------------------------------------------

#define CONXPERT
#define NOMINMAX

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------

#include "imgui_impl_dx12_custom.h" // for ImGui_ImplDX12_InitInfo, etc.
#include "imgui.h" // for ImGuiIO, ImDrawVert, etc.
#include <d3d12.h> // for ID3D12Device, ID3D12GraphicsCommandList, etc.
#include <dxgi1_6.h> // for IDXGIFactory6
#include <wrl.h> // for Microsoft::WRL::ComPtr
#include <wrl/client.h> // for Microsoft::WRL::ComPtr
#include <d3dcompiler.h> // for D3DCompile
#include <windows.h> // for HWND
#include <algorithm> // for std::min/std::max
#include <directx/d3dx12.h> // for CD3DX12_* helpers
#include "Logger.h" // for Logger::Log
#include <format> // for std::format 
#include <ImGui_ImplDX12_DescriptorHeap.h>
#include <Utils.h> // for Utils::HrToString
#include <comdef.h> // place at top if not already included
#include <filesystem>     // for std::filesystem
#include <HintMacros.h> // for DX_CHECK, IM_DELETE, etc.
#include <imgui_impl_win32_custom.h>
#include <sstream> // for std::ostringstream
#include <string_view>

using Microsoft::WRL::ComPtr; // for ComPtr shorthand

// Forward declare buffer helper used by resize helpers
static void CreateOrResizeBuffer(
    Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
    size_t& bufferSize,
    size_t newSize,
    ID3D12Device* device);

// Swapchain health logging helper
static void ImGui_ImplDX12_LogSwapChainHealth(const char* tag, IDXGISwapChain3* sc);

static void ImGui_ImplDX12_LogSwapChainHealth(const char* tag, IDXGISwapChain3* sc)
{
    if (!sc)
    {
        Logger::Log(LogLevel::Warning, std::format("🧪 SwapChainHealth[{}] | swapChain=null", tag ? tag : "<null>"));
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    HRESULT hrDesc = sc->GetDesc(&desc);
    const UINT idx = sc->GetCurrentBackBufferIndex();

    Logger::Log(LogLevel::Info, std::format(
        "🧪 SwapChainHealth[{}] | sc=0x{:X} GetDescHR=0x{:08X} idx={} buffers={} format={} windowed={} flags=0x{:X}",
        tag ? tag : "<null>",
        (uintptr_t)sc,
        (UINT)hrDesc,
        idx,
        desc.BufferCount,
        (int)desc.BufferDesc.Format,
        desc.Windowed ? 1 : 0,
        (UINT)desc.Flags));
}

//-----------------------------------------------------------------------------
// Parameters & Defines
// -----------------------------------------------------------------------------

#ifndef IMGUI_NUM_FRAMES_IN_FLIGHT
#define IMGUI_NUM_FRAMES_IN_FLIGHT 3
#endif
constexpr UINT IMGUI_VERTEX_BUFFER_SIZE = 500000;
constexpr UINT IMGUI_INDEX_BUFFER_SIZE = 1000000;

//-----------------------------------------------------------------------------
// Safety Macros
//-----------------------------------------------------------------------------

#ifndef IM_DELETE
#define IM_DELETE(_PTR) do { if (_PTR) { delete _PTR; _PTR = nullptr; } } while (0)
#endif

#ifndef ImGuiViewportFlags_Minimized
#define ImGuiViewportFlags_Minimized (1 << 1) // ImGuiViewportFlags_Minimized is not defined in some ImGui versions
#endif

// -----------------------------------------------------------------------------
// Lazy device-object creation gates (PSO/root signature). Fonts are engine-owned.
// -----------------------------------------------------------------------------
static bool g_DeviceObjectsCreated = false;
static bool g_DeviceObjectsCreateFailed = false;

// -----------------------------------------------------------------------------
// Internal Backend Data Structures
// -----------------------------------------------------------------------------

struct ImGui_ImplDX12_FrameResources
{
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;

    UINT64 FenceValue = 0;

    size_t VertexBufferSize = 0;
    size_t IndexBufferSize = 0;
    ImDrawVert* VertexCpuPtr = nullptr;
    ImDrawIdx* IndexCpuPtr = nullptr;
};


struct ImGui_ImplDX12_Data
{
    ImGui_ImplDX12_InitInfo InitInfo;

    // Core device + DXGI objects
    Microsoft::WRL::ComPtr<ID3D12Device> Device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineState;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SrvDescHeap;
    Microsoft::WRL::ComPtr<IDXGIFactory4> DxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> Fence;

    ImGui_ImplDX12_DescriptorHeap DescriptorHeapManager;

    // Font texture + descriptors
    Microsoft::WRL::ComPtr<ID3D12Resource> FontTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> FontUploadBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE FontSrvCpuDescHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE FontSrvGpuDescHandle = {};

    UINT FontSrvDescriptorIndex = 0;

    UINT NumFramesInFlight = 3; // <-- Add this line

    // Frame rendering
    ImGui_ImplDX12_FrameResources FrameResources[IMGUI_NUM_FRAMES_IN_FLIGHT];
    UINT FrameIndex = 0;
    UINT64 FenceValue = 0;
// RTV Management for Multi-Viewport Rendering
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RTVDescriptorHeap;
    UINT RTVDescriptorSize = 0;
    UINT NumRTVs = 0;

    // Add FenceEvent for compatibility with code using bd->FenceEvent
    HANDLE FenceEvent = nullptr;

    // --- FIX: Add VSyncEnabled member ---
    bool VSyncEnabled = true;
};


//=========================================================================
// SanitizeViewportSize
// Ensures viewport dimensions are always finite, positive, and GPU-safe
//=========================================================================
static ImVec2 SanitizeViewportSize(const ImVec2& in)
{
    ImVec2 out = in;

    // --- Step 1: Replace NaN / infinite / negative / extreme values ---
    if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
        out.x <= 0.0f || out.y <= 0.0f ||
        out.x > 1e6f || out.y > 1e6f)
    {
        Logger::Log(LogLevel::Warning, std::format(
            "ℹ️ SanitizeViewportSize: Invalid input (RAW=({:.6f}, {:.6f})) ? default (1280x720)",
            in.x, in.y));
        return ImVec2(1280.0f, 720.0f);
    }

    // --- Step 2: Clamp to safe DXGI-legal bounds ---
    constexpr float MIN_SIZE = 16.0f;     // minimum size for swapchain creation
    constexpr float MAX_SIZE = 8192.0f;   // hard GPU-safe ceiling

    const float clampedX = std::clamp(std::round(out.x), MIN_SIZE, MAX_SIZE);
    const float clampedY = std::clamp(std::round(out.y), MIN_SIZE, MAX_SIZE);

    if (clampedX != out.x || clampedY != out.y)
    {
        Logger::Log(LogLevel::Warning, std::format(
            "ℹ️ SanitizeViewportSize: RAW=({:.4f}, {:.4f}) ? CLAMPED=({:.0f}, {:.0f})",
            in.x, in.y, clampedX, clampedY));
    }

    out.x = clampedX;
    out.y = clampedY;

    // --- Step 3: Prevent zero or absurdly small heights (frame-3 bug) ---
    if (out.y < 64.0f)
    {
        Logger::Log(LogLevel::Warning, std::format(
            "ℹ️ SanitizeViewportSize: Height too small ({:.0f}) ? forcing 480", out.y));
        out.y = 480.0f;
    }

    // --- Step 4: Detect & freeze regression (prevents oscillation between bad values) ---
    static ImVec2 lastValid = ImVec2(1280.0f, 720.0f);
    if (!std::isfinite(lastValid.x) || !std::isfinite(lastValid.y))
        lastValid = ImVec2(1280.0f, 720.0f);

    if (out.x <= 0.0f || out.y <= 0.0f)
        out = lastValid;
    else
        lastValid = out;

    Logger::Log(LogLevel::Debug, std::format(
        "ℹ️ SanitizeViewportSize: FINAL=({:.0f}, {:.0f})", out.x, out.y));

    return out;
}



//=================================================
// Log Viewport State (for debugging)
//=================================================
static void ImGui_ImplDX12_LogViewportState(ImGuiViewport* viewport, const char* phase)
{
    if (!viewport)
    {
        Logger::Log(LogLevel::Error, std::format(
            "[Viewport:nullptr] [{}] | NULL viewport pointer passed to ImGui_ImplDX12_LogViewportState",
            phase));
        return;
    }

    auto* vd = (ImGui_ImplDX12_ViewportData*)viewport->RendererUserData;

    Logger::Log(LogLevel::Debug, std::format(
        "[Viewport:{}] [{}] | HWND=0x{:X} | Size={}x{} | vd={} | SwapChain={} | Fence={} | Frames={} | LastRender={}ns",
        viewport->ID,
        phase,
        (uintptr_t)viewport->PlatformHandle,
        (UINT)viewport->Size.x, (UINT)viewport->Size.y,
        (void*)vd,
        vd ? (void*)vd->SwapChain.Get() : nullptr,
        vd ? (void*)vd->Fence.Get() : nullptr,
        vd ? vd->FrameCounter : 0,
        vd ? vd->LastRenderDurationNs : 0
    ));
}

// -----------------------------------------------------------------------------
// ImGui Input Layout (TheFletchZone Edition)
// -----------------------------------------------------------------------------
D3D12_INPUT_ELEMENT_DESC inputLayout[]
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
};

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// ============================================================================
// ACCESSOR FUNCTIONS FOR SPLASH SCREEN AND OTHER EXTERNAL DX12 UTILITIES
// ============================================================================
ImGui_ImplDX12_Data* ImGui_ImplDX12_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX12_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

ImGui_ImplDX12_InitInfo* ImGui_ImplDX12_GetInitInfo()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    return bd ? &bd->InitInfo : nullptr;
}

ID3D12Device* ImGui_ImplDX12_GetDevice()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    return bd ? bd->Device.Get() : nullptr;
}

ID3D12CommandQueue* ImGui_ImplDX12_GetCommandQueue()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (bd && bd->CommandQueue)
        return bd->CommandQueue.Get();
    return nullptr;
}

ID3D12DescriptorHeap* ImGui_ImplDX12_GetSrvHeap()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    return bd ? bd->SrvDescHeap.Get() : nullptr;
}

// -----------------------------------------------------------------------------
// Local logging helper: strips leading "??" / "?" markers from messages
// -----------------------------------------------------------------------------
static std::string ImGuiDx12_CleanLogMessage(std::string_view msg)
{
    size_t i = 0;
    while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
        ++i;

    while (i < msg.size() && msg[i] == '?')
        ++i;

    while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
        ++i;

    return std::string(msg.substr(i));
}

static void ImGuiDx12_Log(LogLevel level, const std::string& message, const std::string& category = "ImGuiDX12")
{
    // Prefix per-message emoji to match the user's requested style, regardless of the logger's global prefixing.
    const char* emoji = "ℹ️";
    switch (level)
    {
    case LogLevel::Trace:   emoji = "🔍"; break;
    case LogLevel::Debug:   emoji = "🛠"; break;
    case LogLevel::Info:    emoji = "ℹ️"; break;
    case LogLevel::Success: emoji = "✅"; break;
    case LogLevel::Warning: emoji = "⚠️"; break;
    case LogLevel::Error:   emoji = "❌"; break;
    case LogLevel::Critical:emoji = "🚨"; break;
    case LogLevel::Verbose: emoji = "📢"; break;
    default:                emoji = "ℹ️"; break;
    }

    std::string cleaned = ImGuiDx12_CleanLogMessage(message);
    Logger::Log(level, std::string(emoji) + " " + cleaned, category);
}

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
static void ImGui_ImplDX12_ResizeBuffers(ImGui_ImplDX12_FrameResources& fr, size_t neededVB, size_t neededIB, ID3D12Device* device)
{
    if (!device)
        return;

    CreateOrResizeBuffer(fr.VertexBuffer, fr.VertexBufferSize, neededVB, device);
    CreateOrResizeBuffer(fr.IndexBuffer, fr.IndexBufferSize, neededIB, device);

    fr.VertexCpuPtr = nullptr;
    fr.IndexCpuPtr = nullptr;

    if (fr.VertexBuffer)
        DX_CHECK(fr.VertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&fr.VertexCpuPtr)));
    if (fr.IndexBuffer)
        DX_CHECK(fr.IndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&fr.IndexCpuPtr)));
}

// Per-viewport frame resources use UINT64 sizes. Provide a helper mirroring CreateOrResizeBuffer
// but matching the per-viewport size type.
static void CreateOrResizeBufferU64(
    Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
    UINT64& bufferSize,
    UINT64 newSize,
    ID3D12Device* device)
{
    if (!device)
        return;

    if (buffer && bufferSize >= newSize)
        return;

    const UINT64 alignedSize = (newSize + 0xFFFFull) & ~0xFFFFull;

    buffer.Reset();
    bufferSize = alignedSize;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer));

    if (FAILED(hr) || !buffer)
    {
        HRESULT removed = device ? device->GetDeviceRemovedReason() : E_FAIL;
        Logger::Log(LogLevel::Error, std::format(
            "❌ CreateCommittedResource (U64) failed. HR=0x{:08X} DeviceRemovedReason=0x{:08X} | RequestedSize={}",
            static_cast<unsigned int>(hr), static_cast<unsigned int>(removed), static_cast<unsigned long long>(newSize)));
        buffer.Reset();
        bufferSize = 0;
        return;
    }
}

//==========================================================================
// Fully Centralized PSO Build Helper Function (TheFletchZone Version)
//==========================================================================
static bool ImGui_ImplDX12_CreatePipeline(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    ID3DBlob* vertexShaderBlob,
    ID3DBlob* pixelShaderBlob,
    ComPtr<ID3D12PipelineState>& outPipelineState)
{
// Input layout matches ImDrawVert
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ImDrawVert, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ImDrawVert, uv),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, offsetof(ImDrawVert, col), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
// Standard ImGui UI blend state (non-premultiplied alpha)
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC& rt = blendDesc.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO; // ? Use ZERO here to avoid yellow tinting
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    Logger::Log(LogLevel::Info, std::format("ℹ️ Blend setup: SrcBlend={}, DestBlend={}, SrcAlpha={}, DestAlpha={}",
        (int)rt.SrcBlend, (int)rt.DestBlend, (int)rt.SrcBlendAlpha, (int)rt.DestBlendAlpha));
// Disable depth/stencil for UI
    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;
// PSO descriptor configuration
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    psoDesc.BlendState = blendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;

    // IMPORTANT: RTV format must match the swap chain/backbuffer format.
    // Using a mismatched format can lead to invalid PSO creation or GPU/device removal.
    DXGI_FORMAT rtFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData())
    {
        if (bd->InitInfo.RenderTargetFormat != DXGI_FORMAT_UNKNOWN)
            rtFormat = bd->InitInfo.RenderTargetFormat;
    }
    psoDesc.RTVFormats[0] = rtFormat;

    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPipelineState));
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to create GraphicsPipelineState. HR=0x{:08X} RTVFormat={}", (UINT)hr, (int)rtFormat));
        return false;
    }

    Logger::Log(LogLevel::Info, "ℹ️ PSO created successfully (Alpha Blending + No Depth).");
    return true;
}

//===================================
// Create or Resize Buffer Function
//===================================
static void CreateOrResizeBuffer(
    Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
    size_t& bufferSize,   // <-- tracked in frame resource
    size_t                                  newSize,
    ID3D12Device* device)
{
    if (buffer && bufferSize >= newSize)
        return;                               // big enough already

    // Round up to the next 64 KB to avoid frequent reallocations.
    const size_t alignedSize = (newSize + 0xFFFF) & ~0xFFFF;

    buffer.Reset();
    bufferSize = alignedSize;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer));

    if (FAILED(hr) || !buffer)
    {
        HRESULT removed = device ? device->GetDeviceRemovedReason() : E_FAIL;
        Logger::Log(LogLevel::Error, std::format(
            "❌ CreateCommittedResource failed. HR=0x{:08X} DeviceRemovedReason=0x{:08X} | RequestedSize={}",
            static_cast<unsigned int>(hr), static_cast<unsigned int>(removed), static_cast<unsigned long long>(newSize)));
        return;
    }
}

// -----------------------------------------------------------------------------
// Initialize Backend
// -----------------------------------------------------------------------------

bool ImGui_ImplDX12_Init(ImGui_ImplDX12_InitInfo* info)
{
    IM_ASSERT(info && info->Device && info->CommandQueue);

    ImGuiIO& io = ImGui::GetIO();
    Logger::Log(LogLevel::Info, std::format("ℹ️ BackendRendererUserData set: {}", static_cast<void*>(io.BackendRendererUserData)));
    IM_ASSERT(io.BackendRendererUserData == nullptr);

    auto* bd = IM_NEW(ImGui_ImplDX12_Data)();
    io.BackendRendererUserData = bd;
    io.BackendRendererName = "imgui_impl_dx12_custom";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    // Copy init info
    bd->InitInfo = *info;
    bd->Device = info->Device;
    bd->CommandQueue = info->CommandQueue;
    bd->DxgiFactory = info->DxgiFactory;

    // Cache engine-owned main swapchain pointer (optional)
    bd->InitInfo.SwapChain = info->SwapChain;
    ImGui_ImplDX12_LogSwapChainHealth("Init(Main)", bd->InitInfo.SwapChain);

    // IMPORTANT: Use the heap provided by the engine/app.
    // Overriding this with a privately-managed heap can invalidate descriptors
    // that the app (and fonts) still reference, leading to DEVICE_HUNG.
    bd->SrvDescHeap = info->SrvDescriptorHeap;

    bd->InitInfo.NumFramesInFlight = IMGUI_NUM_FRAMES_IN_FLIGHT;
    bd->InitInfo.RenderTargetFormat = info->RenderTargetFormat;
    bd->InitInfo.FontSrvCpuDescHandle = info->FontSrvCpuDescHandle;
    bd->InitInfo.FontSrvGpuDescHandle = info->FontSrvGpuDescHandle;
    bd->InitInfo.RTVDescriptorHeap = info->RTVDescriptorHeap;
    bd->InitInfo.RTVDescriptorSize = info->RTVDescriptorSize;

    // Set default RTV descriptor size and number of RTVs
    if (info->RTVDescriptorSize == 0)
    {
        Logger::Log(LogLevel::Warning, "ℹ️ RTVDescriptorSize not provided, using default value of 64 bytes.");
        bd->InitInfo.RTVDescriptorSize = 64; // Default RTV size
    }
    else
    {
        Logger::Log(LogLevel::Info, std::format("ℹ️ Using provided RTVDescriptorSize: {}", info->RTVDescriptorSize));
    }
    bd->InitInfo.RTVDescriptorSize = info->RTVDescriptorSize;
    bd->InitInfo.NumFramesInFlight = info->NumFramesInFlight;

    bd->FontSrvDescriptorIndex = 0;

    Logger::Log(LogLevel::Info, "ℹ️ ImGui DX12 Backend Initialization (TheFletchZone Edition)");
    Logger::Log(LogLevel::Info, std::format("ℹ️ Device: {}", static_cast<void*>(bd->Device.Get())));
    Logger::Log(LogLevel::Info, std::format("ℹ️ Command Queue: {}", static_cast<void*>(bd->CommandQueue.Get())));
    Logger::Log(LogLevel::Info, std::format("ℹ️ DXGI Factory: {}", static_cast<void*>(bd->DxgiFactory.Get())));

    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored Device: {}", static_cast<void*>(bd->Device.Get())));
    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored Command Queue: {}", static_cast<void*>(bd->CommandQueue.Get())));
    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored DXGI Factory: {}", static_cast<void*>(bd->DxgiFactory.Get())));

    // Font SRV handles
    if (info->FontSrvCpuDescHandle.ptr == 0 || info->FontSrvGpuDescHandle.ptr == 0)
        Logger::Log(LogLevel::Warning, "⚠️ Font descriptor handles not provided — font upload may fail!");

    bd->FontSrvCpuDescHandle = info->FontSrvCpuDescHandle;
    bd->FontSrvGpuDescHandle = info->FontSrvGpuDescHandle;

    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored CPU Font SRV Handle: {}", (void*)bd->FontSrvCpuDescHandle.ptr));
    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored GPU Font SRV Handle: {}", (void*)bd->FontSrvGpuDescHandle.ptr));

    // Keep the descriptor heap manager available for future use, but do NOT overwrite `bd->SrvDescHeap`.
    if (!bd->SrvDescHeap)
    {
        Logger::Log(LogLevel::Info, "ℹ️ No SRV heap provided, creating an internal ImGui SRV heap...");
        bd->DescriptorHeapManager.Initialize(
            bd->Device.Get(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            2048);
        bd->SrvDescHeap = bd->DescriptorHeapManager.GetHeap();
    }

    Logger::Log(LogLevel::Info, std::format("ℹ️ Using SRV descriptor heap: {}", static_cast<void*>(bd->SrvDescHeap.Get())));

    // Frame resources (zeroed)
    bd->FrameIndex = 0;
    for (UINT i = 0; i < IMGUI_NUM_FRAMES_IN_FLIGHT; ++i)
    {
        auto& fr = bd->FrameResources[i];
        fr.VertexBufferSize = 0;
        fr.IndexBufferSize = 0;
        fr.VertexBuffer.Reset();
        fr.IndexBuffer.Reset();
        fr.VertexCpuPtr = nullptr;
        fr.IndexCpuPtr = nullptr;
    }

    // ======================
    // Create Fence for Sync
    // ======================
    Logger::Log(LogLevel::Info, "ℹ️ Creating ImGui DX12 fence...");

    bd->FenceValue = 0;
    HRESULT hr = bd->Device->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&bd->Fence)
    );

    if (FAILED(hr) || !bd->Fence)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ Failed to create ImGui DX12 fence! HRESULT = 0x{:08X}", hr));
        return false;
    }

    Logger::Log(LogLevel::Info, "✅ ImGui DX12 fence created successfully.");

    // RTV heap for viewport rendering
    bd->RTVDescriptorHeap = info->RTVDescriptorHeap;
    if (!bd->RTVDescriptorHeap || bd->RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr == 0)
    {
        Logger::Log(LogLevel::Error, "❌ Invalid RTV Descriptor Heap passed into ImGui! Initialization aborted.");
        return false;
    }
    bd->RTVDescriptorSize = bd->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    bd->NumRTVs = 64;
    Logger::Log(LogLevel::Info, std::format("ℹ️ Stored RTV Descriptor Heap: {}", static_cast<void*>(bd->RTVDescriptorHeap.Get())));
    Logger::Log(LogLevel::Info, std::format("ℹ️ RTV Descriptor Size: {}", bd->RTVDescriptorSize));
    Logger::Log(LogLevel::Info, std::format("ℹ️ Max RTVs supported: {}", bd->NumRTVs));

    // Install platform hooks for multi-viewport
    ImGui_ImplDX12_InstallPlatformHooks();

    // NOTE: Do NOT call ImGui_ImplDX12_CreateDeviceObjects() here.
    // It may attempt to create/recreate the font texture, which requires a valid recording command list.
    // Font upload is deferred to the engine frame lifecycle (first BeginFrame/ImGui_ImplDX12_NewFrame).
    Logger::Log(LogLevel::Info, "ℹ️ ImGui DX12 Init: deferring CreateDeviceObjects/font upload until first frame.");

    Logger::Log(LogLevel::Info, "✅ ImGui DX12 Backend Initialized (TheFletchZone)");
    return true;
}

//=======================================
// Install PlatformHooks
//========================================
void ImGui_ImplDX12_InstallPlatformHooks()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    // Bind renderer hook callbacks
    platform_io.Renderer_CreateWindow = ImGui_ImplDX12_CreateWindow;
    platform_io.Renderer_DestroyWindow = ImGui_ImplDX12_DestroyWindow;
    platform_io.Renderer_SetWindowSize = ImGui_ImplDX12_SetWindowSize;
    platform_io.Renderer_RenderWindow = ImGui_ImplDX12_RenderWindow;
    platform_io.Renderer_SwapBuffers = ImGui_ImplDX12_SwapBuffers;
    //platform_io.Renderer_GetWindowFocus = ImGui_ImplDX12_GetWindowFocus;
    //platform_io.Renderer_GetWindowMinimized = ImGui_ImplDX12_GetWindowMinimized;
    //platform_io.Renderer_UpdateWindow = ImGui_ImplDX12_UpdateWindow;

    Logger::Log(LogLevel::Info, "✅ ImGui DX12 Platform Hooks installed");
}

//=====================================================
// Create Device Objects (FIXED ROOT SIGNATURE VERSION)
//=====================================================
bool ImGui_ImplDX12_CreateDeviceObjects()
{
    namespace fs = std::filesystem;

    Logger::Log(LogLevel::Info, "📂  Working Directory: " + fs::current_path().string());
    Logger::Log(LogLevel::Info, "📂  Looking for: shaders\\imgui_vs.hlsl");
    Logger::Log(LogLevel::Info, std::string("📂  File exists: ") + (fs::exists("shaders\\imgui_vs.hlsl") ? "true" : "false"));

    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (!bd || !bd->Device)
    {
        Logger::Log(LogLevel::Error, "❌ Backend data or D3D12 device is missing.");
        return false;
    }

    // NOTE: Device objects here mean pipeline state + root signature only.
    // Fonts are uploaded by the engine when a valid command list is available.

    HRESULT hr = S_OK;
    ComPtr<ID3DBlob> vertexShaderBlob;
    ComPtr<ID3DBlob> pixelShaderBlob;
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    // ----------------------------------------------------
    // 1. Release old pipeline resources
    // ----------------------------------------------------
    bd->RootSignature.Reset();
    bd->PipelineState.Reset();

    // ----------------------------------------------------
    // 2. Build Root Signature (b0 + t0 + static s0)
    // ----------------------------------------------------
    CD3DX12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_ROOT_PARAMETER1 rootParameters[2] = {};
    rootParameters[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);  // b0
    rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL); // t0

    CD3DX12_STATIC_SAMPLER_DESC staticSampler(
        0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f, 16,
        D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        0.0f, D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_PIXEL
    );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init_1_1(
        _countof(rootParameters), rootParameters,
        1, &staticSampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    hr = D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1_1,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr))
    {
        std::string errStr = errorBlob ? std::string((char*)errorBlob->GetBufferPointer()) : "No error blob.";
        Logger::Log(LogLevel::Error, "❌ Failed to serialize root signature: " + errStr);
        return false;
    }

    hr = bd->Device->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&bd->RootSignature)
    );

    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, "❌ Failed to create root signature.");
        return false;
    }

    Logger::Log(LogLevel::Info, "✅ Root Signature created successfully.");
    // ----------------------------------------------------
    // 3. Compile Shaders
    // ----------------------------------------------------
    {
        UINT compileFlags = 0;
    #if defined(_DEBUG)
        compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    #endif

        hr = D3DCompileFromFile(
            L"shaders\\imgui_vs.hlsl",
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main",
            "vs_5_0",
            compileFlags,
            0,
            &vertexShaderBlob,
            &errorBlob);
        if (FAILED(hr))
        {
            std::string errStr = errorBlob ? std::string((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize()) : "No error blob.";
            Logger::Log(LogLevel::Error, std::format("❌ VS compile failed HR=0x{:08X} | {}", (unsigned)hr, errStr));
            return false;
        }

        errorBlob.Reset();
        hr = D3DCompileFromFile(
            L"shaders\\imgui_ps.hlsl",
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "main",
            "ps_5_0",
            compileFlags,
            0,
            &pixelShaderBlob,
            &errorBlob);
        if (FAILED(hr))
        {
            std::string errStr = errorBlob ? std::string((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize()) : "No error blob.";
            Logger::Log(LogLevel::Error, std::format("❌ PS compile failed HR=0x{:08X} | {}", (unsigned)hr, errStr));
            return false;
        }
    }

    Logger::Log(LogLevel::Info, "✅ Shaders compiled successfully.");

    // ----------------------------------------------------
    // 4. Create Pipeline State Object
    // ----------------------------------------------------
    if (!ImGui_ImplDX12_CreatePipeline(
        bd->Device.Get(),
        bd->RootSignature.Get(),
        vertexShaderBlob.Get(),
        pixelShaderBlob.Get(),
        bd->PipelineState))
    {
        Logger::Log(LogLevel::Error, "❌ Failed to create Pipeline State Object.");
        return false;
    }

    Logger::Log(LogLevel::Info, "✅ Pipeline State Object created.");
    // ----------------------------------------------------
    // 5. Ensure Descriptor Heap Exists
    // ----------------------------------------------------
    if (!bd->SrvDescHeap)
    {
        Logger::Log(LogLevel::Info, "ℹ️ Creating ImGui SRV Descriptor Heap...");

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 64; // Enough for fonts + custom textures later
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;

        hr = bd->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&bd->SrvDescHeap));
        if (FAILED(hr) || !bd->SrvDescHeap)
        {
            Logger::Log(LogLevel::Error, "❌ Failed to create ImGui SRV Descriptor Heap!");
            return false;
        }

        Logger::Log(LogLevel::Info, std::format(
            "✅ ImGui SRV Heap Created | CPU=0x{:X} | GPU=0x{:X} | Count={}",
            (uintptr_t)bd->SrvDescHeap->GetCPUDescriptorHandleForHeapStart().ptr,
            (uintptr_t)bd->SrvDescHeap->GetGPUDescriptorHandleForHeapStart().ptr,
            desc.NumDescriptors
        ));
    }

    // ----------------------------------------------------
    // IMPORTANT: Do NOT create/recreate font texture here.
    // Font upload is owned by the engine frame lifecycle.
    // ----------------------------------------------------

    Logger::Log(LogLevel::Info, "✅ Device objects created successfully.");
    return true;
}

// -----------------------------------------------------------------------------
// Shutdown Backend
// -----------------------------------------------------------------------------
void ImGui_ImplDX12_Shutdown()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (!bd)
        return;

    // Allow re-init after shutdown.
    g_DeviceObjectsCreated = false;
    g_DeviceObjectsCreateFailed = false;

    Logger::Log(LogLevel::Info, std::format("ℹ️ Shutting down ImGui DX12 Backend (bd: {})", static_cast<void*>(bd)));
// Destroy all multi-viewports (platform windows) if used
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        for (int i = 0; i < platform_io.Viewports.Size; ++i)
        {
            ImGuiViewport* viewport = platform_io.Viewports[i];
            if (viewport && viewport->RendererUserData)
            {
                Logger::Log(LogLevel::Debug, std::format("ℹ️ Destroying viewport #{} before shutdown...", i));
                ImGui_ImplDX12_DestroyWindow(viewport);  // You already implemented this
                Logger::Log(LogLevel::Debug, std::format("✅ Viewport #{} fully released.", i));
            }
// Always clear pointer to avoid reuse crash
            viewport->RendererUserData = nullptr;
        }
    }
// Release per-frame buffers
    for (UINT i = 0; i < IMGUI_NUM_FRAMES_IN_FLIGHT; ++i)
    {
        auto& fr = bd->FrameResources[i];
        if (fr.VertexBuffer)  fr.VertexBuffer.Reset();
        if (fr.IndexBuffer)   fr.IndexBuffer.Reset();
        fr.VertexCpuPtr = nullptr;
        fr.IndexCpuPtr = nullptr;
        fr.VertexBufferSize = 0;
        fr.IndexBufferSize = 0;
    }

    Logger::Log(LogLevel::Debug, "✅ Released all FrameResources.");
// Release font-related resources
    if (bd->FontTexture)      bd->FontTexture.Reset();
    if (bd->FontUploadBuffer) bd->FontUploadBuffer.Reset();

    Logger::Log(LogLevel::Debug, "✅ Font texture and upload buffer released.");
// Release pipeline and root signature
    if (bd->PipelineState)    bd->PipelineState.Reset();
    if (bd->RootSignature)    bd->RootSignature.Reset();

    Logger::Log(LogLevel::Debug, "✅ PSO and RootSignature released.");
// Release descriptor heap(s)
    if (bd->SrvDescHeap)      bd->SrvDescHeap.Reset();
// Optionally clear InitInfo
    ZeroMemory(&bd->InitInfo, sizeof(bd->InitInfo));

    Logger::Log(LogLevel::Debug, "✅ Descriptor Heap and InitInfo reset.");
// Delete backend data
    IM_DELETE(bd);
    io.BackendRendererUserData = nullptr;

    Logger::Log(LogLevel::Info, "✅ ImGui DX12 Backend fully shutdown.");
}

// -----------------------------------------------------------------------------
// Create Fonts Texture
// -----------------------------------------------------------------------------
bool ImGui_ImplDX12_CreateFontsTexture(ID3D12Device* device, ID3D12GraphicsCommandList* command_list)
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (!bd || !device || !command_list)
    {
        Logger::Log(LogLevel::Error, "❌ Invalid backend data, device, or command list.");
        return false;
    }
// SRV heap validation
    if (!bd->SrvDescHeap)
    {
        Logger::Log(LogLevel::Error, "❌ SRV descriptor heap is NULL! Cannot upload fonts.");
        return false;
    }

    auto heapDesc = bd->SrvDescHeap->GetDesc();
    if (heapDesc.NumDescriptors < 1)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ SRV heap too small: {} descriptors allocated.", heapDesc.NumDescriptors));
        return false;
    }
// Get font data
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    if (!pixels || width <= 0 || height <= 0)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ Invalid font texture data: pixels={}, size={}x{}",
            (void*)pixels, width, height));
        return false;
    }

    Logger::Log(LogLevel::Info, std::format("ℹ️ Uploading ImGui font texture: {}x{}", width, height));
// Release old resources safely
    bd->FontTexture.Reset();
    bd->FontUploadBuffer.Reset();

    // 1?? Create GPU font texture
    const UINT bytesPerPixel = 4;
    const UINT rowPitch = width * bytesPerPixel;
    const UINT uploadSize = rowPitch * height;

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, width, height);
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&bd->FontTexture)
    );

    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ Failed to create GPU font texture. HRESULT=0x{:08X}", hr));
        return false;
    }

    // 2?? Prepare SRV handles (slot 0 reserved for font texture)
    bd->FontSrvCpuDescHandle = bd->SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    bd->FontSrvGpuDescHandle = bd->SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    bd->FontSrvDescriptorIndex = 0;

    Logger::Log(LogLevel::Info, std::format(
        "ℹ️ Allocated ImGui Font SRV: CPU=0x{:X}, GPU=0x{:X}, Index={}",
        (uintptr_t)bd->FontSrvCpuDescHandle.ptr,
        (uintptr_t)bd->FontSrvGpuDescHandle.ptr,
        bd->FontSrvDescriptorIndex));

    // 3?? Create upload buffer
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    hr = device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&bd->FontUploadBuffer)
    );

    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ Failed to create font upload buffer. HRESULT=0x{:08X}", hr));
        return false;
    }

    // 4?? Copy pixel data into upload buffer
    void* mappedData = nullptr;
    hr = bd->FontUploadBuffer->Map(0, nullptr, &mappedData);
    if (FAILED(hr) || !mappedData)
    {
        Logger::Log(LogLevel::Error, "❌ Failed to map font upload buffer.");
        return false;
    }
    memcpy(mappedData, pixels, uploadSize);
    bd->FontUploadBuffer->Unmap(0, nullptr);

    // 5?? Upload font data to GPU
    D3D12_SUBRESOURCE_DATA subResource = {};
    subResource.pData = pixels;
    subResource.RowPitch = rowPitch;
    subResource.SlicePitch = uploadSize;

    UpdateSubresources(command_list,
        bd->FontTexture.Get(), bd->FontUploadBuffer.Get(),
        0, 0, 1, &subResource);

    // 6?? Transition resource state
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        bd->FontTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &barrier);

    // 7?? Create SRV descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(
        bd->FontTexture.Get(), &srvDesc, bd->FontSrvCpuDescHandle);

    // 8?? Set ImGui TexID
    io.Fonts->TexID = (ImTextureID)(bd->FontSrvGpuDescHandle.ptr);

    // 9?? Force heap bind after upload
    ID3D12DescriptorHeap* heaps[] = { bd->SrvDescHeap.Get() };
    command_list->SetDescriptorHeaps(1, heaps);

    Logger::Log(LogLevel::Info, std::format(
        "ℹ️ ImGui Font Texture uploaded | TexID=0x{:X}",
        (uintptr_t)bd->FontSrvGpuDescHandle.ptr));

    return true;
}

// -----------------------------------------------------------------------------
// Destroy Fonts Texture
// -----------------------------------------------------------------------------
void ImGui_ImplDX12_DestroyFontsTexture()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (!bd || !bd->FontTexture)
        return;

    Logger::Log(LogLevel::Debug, std::format("ℹ️ Destroying FontTexture: {} UploadBuffer: {}",
        (void*)bd->FontTexture.Get(), (void*)bd->FontUploadBuffer.Get()));

    bd->FontTexture.Reset();
    bd->FontUploadBuffer.Reset();

    Logger::Log(LogLevel::Info, "ℹ️ ImGui font texture destroyed");
}

// -----------------------------------------------------------------------------
// New Frame & ℹ️ Frame Lifecycle
// -----------------------------------------------------------------------------
void ImGui_ImplDX12_NewFrame()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    IM_ASSERT(bd != nullptr);

    // Once per second: log main swapchain health
    {
        static int s_scTick = 0;
        if ((++s_scTick % 60) == 0)
        {
            ImGui_ImplDX12_LogSwapChainHealth("NewFrame(Main)", bd->InitInfo.SwapChain);
        }
    }
// Cycle to next frame index (for triple buffering)
    bd->FrameIndex = (bd->FrameIndex + 1) % IMGUI_NUM_FRAMES_IN_FLIGHT;

    ImGui_ImplDX12_FrameResources* fr = &bd->FrameResources[bd->FrameIndex];
// Reset pointers from previous frame (safe cleanup)
    fr->VertexCpuPtr = nullptr;
    fr->IndexCpuPtr = nullptr;
// Optional: Reset command allocator if managed per frame
    if (fr->CommandAllocator)
    {
        HRESULT hr = fr->CommandAllocator->Reset();
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, "❌ Failed to reset CommandAllocator for ImGui frame!");
        }
        else
        {
            Logger::Log(LogLevel::Verbose, std::format("ℹ️ Reset CommandAllocator for frame {}", bd->FrameIndex));
        }
    }
// Ensure ImGui context is valid
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    IM_ASSERT(ctx != nullptr);
// Optional: Log new frame
    Logger::Log(LogLevel::Info, std::format("ℹ️ ImGui New Frame Started — FrameIndex = {}", bd->FrameIndex));
}

//=========================================================================
// Wait for the viewport's fence to reach the last submitted value
//=========================================================================
static bool WaitForViewportFence(ImGui_ImplDX12_ViewportData* vd)
{
    if (!vd || !vd->Fence)
        return true;

    const UINT64 completed = vd->Fence->GetCompletedValue();
    if (completed < vd->LastSubmittedFenceValue)
    {
        HANDLE evt = vd->FenceEvent;
        if (!evt)
            vd->FenceEvent = evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        HRESULT hr = vd->Fence->SetEventOnCompletion(vd->LastSubmittedFenceValue, evt);
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error,
                std::format("❌ Fence SetEventOnCompletion failed. HR=0x{:08X}", hr));
            return false;
        }

        if (evt != nullptr)
        {
            WaitForSingleObject(evt, INFINITE);
        }
        else
        {
            Logger::Log(LogLevel::Error, "❌ WaitForSingleObject called with null event handle!");
            // Optionally handle error or assert here
        }
        Logger::Log(LogLevel::Debug,
            std::format("ℹ️ GPU wait complete for viewport fence ℹ️ Last={}, Completed={}",
                vd->LastSubmittedFenceValue, completed));
    }

    return true;
}

// -----------------------------------------------------------------------------
// Render Draw Data (Main + Secondary Viewports)
// -----------------------------------------------------------------------------
void ImGui_ImplDX12_RenderDrawData(ImDrawData* draw_data,
    ID3D12GraphicsCommandList* command_list)
{
    if (!draw_data || !command_list || draw_data->TotalVtxCount == 0)
        return;

    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    ImGuiViewport* viewport = draw_data->OwnerViewport;
    if (!bd || !viewport)
        return;

    const bool isMainViewport = (viewport->ID == ImGui::GetMainViewport()->ID);

    // Lazily create pipeline objects exactly once (fonts are NOT touched here).
    if ((!bd->RootSignature || !bd->PipelineState) && !g_DeviceObjectsCreated && !g_DeviceObjectsCreateFailed)
    {
        if (!bd->Device)
        {
            Logger::Log(LogLevel::Error, "❌ ImGui device objects: missing D3D12 device");
            g_DeviceObjectsCreateFailed = true;
            return;
        }

        Logger::Log(LogLevel::Info, "[ImGuiDX12] Creating device objects (RootSignature/PSO) lazily...");
        if (ImGui_ImplDX12_CreateDeviceObjects())
        {
            g_DeviceObjectsCreated = true;
            Logger::Log(LogLevel::Success, "[ImGuiDX12] Device objects created (RootSignature/PSO)");
        }
        else
        {
            g_DeviceObjectsCreateFailed = true;
            Logger::Log(LogLevel::Error, "[ImGuiDX12] Failed to create device objects (RootSignature/PSO)");
            return;
        }
    }

    // ------------------------------------------------------------
    // Viewport + MVP
    // ------------------------------------------------------------
    const ImVec2& pos = draw_data->DisplayPos;
    const ImVec2& size = draw_data->DisplaySize;

    if (size.x <= 0.0f || size.y <= 0.0f)
        return;

    D3D12_VIEWPORT vp{};
    vp.Width = size.x;
    vp.Height = size.y;
    vp.MinDepth = D3D12_MIN_DEPTH;
    vp.MaxDepth = D3D12_MAX_DEPTH;
    vp.TopLeftX = pos.x;
    vp.TopLeftY = pos.y;

    command_list->RSSetViewports(1, &vp);

    // ------------------------------------------------------------
    // Get per-viewport data
    // ------------------------------------------------------------
    ImGui_ImplDX12_ViewportData* vd =
        static_cast<ImGui_ImplDX12_ViewportData*>(viewport->RendererUserData);

    if (!vd)
    {
        Logger::Log(LogLevel::Error,
            "❌ RendererUserData missing. Ensure ImGui_ImplDX12_CreateWindow() was called for this viewport.");
        return;
    }

    if (vd->IsDummy && !isMainViewport)
    {
        Logger::Log(LogLevel::Error,
            "❌ Dummy RendererUserData used for non-main viewport!");
        return;
    }

    // ------------------------------------------------------------
    // Select frame index
    // ------------------------------------------------------------
    const UINT frameIndex = isMainViewport
        ? bd->FrameIndex
        : vd->SwapChain->GetCurrentBackBufferIndex();

    // ------------------------------------------------------------
    // Pick the right per-frame resources (main viewport differs from secondary)
    // ------------------------------------------------------------
    Microsoft::WRL::ComPtr<ID3D12Resource>* vertexBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource>* indexBuffer = nullptr;
    UINT64* vertexBufferSizeU64Ptr = nullptr;
    UINT64* indexBufferSizeU64Ptr = nullptr;
    ImDrawVert** vertexCpuPtr = nullptr;
    ImDrawIdx** indexCpuPtr = nullptr;

    ImGui_ImplDX12_FrameResources* frMain = nullptr;
    using VpFrameResourcesT = std::remove_reference_t<decltype(((ImGui_ImplDX12_ViewportData*)nullptr)->FrameResources[0])>;
    VpFrameResourcesT* frVp = nullptr;

    if (isMainViewport)
    {
        ImGui_ImplDX12_FrameResources& fr_main = bd->FrameResources[frameIndex];
        frMain = &fr_main;
        vertexBuffer = &fr_main.VertexBuffer;
        indexBuffer = &fr_main.IndexBuffer;

        // IMPORTANT: keep pointers to a real size value so CreateOrResizeBufferU64 updates persist.
        vertexBufferSizeU64Ptr = reinterpret_cast<UINT64*>(&fr_main.VertexBufferSize);
        indexBufferSizeU64Ptr = reinterpret_cast<UINT64*>(&fr_main.IndexBufferSize);

        vertexCpuPtr = &fr_main.VertexCpuPtr;
        indexCpuPtr = &fr_main.IndexCpuPtr;
    }
    else
    {
        VpFrameResourcesT& fr_vp = vd->FrameResources[frameIndex];
        frVp = &fr_vp;
        vertexBuffer = &fr_vp.VertexBuffer;
        indexBuffer = &fr_vp.IndexBuffer;
        vertexBufferSizeU64Ptr = &fr_vp.VertexBufferSize;
        indexBufferSizeU64Ptr = &fr_vp.IndexBufferSize;
        vertexCpuPtr = &fr_vp.VertexCpuPtr;
        indexCpuPtr = &fr_vp.IndexCpuPtr;
    }

    // ------------------------------------------------------------
    // Ensure buffers exist
    // ------------------------------------------------------------
    const UINT64 requiredVB = (UINT64)draw_data->TotalVtxCount * (UINT64)sizeof(ImDrawVert);
    const UINT64 requiredIB = (UINT64)draw_data->TotalIdxCount * (UINT64)sizeof(ImDrawIdx);

    const UINT64 curVBSize = vertexBufferSizeU64Ptr ? *vertexBufferSizeU64Ptr : 0;
    const UINT64 curIBSize = indexBufferSizeU64Ptr ? *indexBufferSizeU64Ptr : 0;

    if (requiredVB > curVBSize || requiredIB > curIBSize ||
        vertexBuffer->Get() == nullptr || indexBuffer->Get() == nullptr ||
        *vertexCpuPtr == nullptr || *indexCpuPtr == nullptr)
    {
        ImGuiDx12_Log(LogLevel::Info, "ℹ Resizing ImGui buffers");

        CreateOrResizeBufferU64(*vertexBuffer, *vertexBufferSizeU64Ptr, requiredVB, bd->Device.Get());
        CreateOrResizeBufferU64(*indexBuffer, *indexBufferSizeU64Ptr, requiredIB, bd->Device.Get());

        // If creation failed (e.g. device lost) bail before trying to map.
        if (!vertexBuffer->Get() || !indexBuffer->Get())
        {
            Logger::Log(LogLevel::Error, "❌ ImGui buffer allocation failed (device may be lost). Skipping ImGui draw.");
            return;
        }

        *vertexCpuPtr = nullptr;
        *indexCpuPtr = nullptr;

        DX_CHECK((*vertexBuffer)->Map(0, nullptr, reinterpret_cast<void**>(vertexCpuPtr)));
        DX_CHECK((*indexBuffer)->Map(0, nullptr, reinterpret_cast<void**>(indexCpuPtr)));

        if (!*vertexCpuPtr || !*indexCpuPtr)
        {
            Logger::Log(LogLevel::Error, "❌ Failed to map ImGui vertex/index buffers.");
            return;
        }
    }

    // ------------------------------------------------------------
    // Upload vertices/indices into mapped buffers
    // ------------------------------------------------------------
    {
        ImDrawVert* vtxDst = *vertexCpuPtr;
        ImDrawIdx* idxDst = *indexCpuPtr;

        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd = draw_data->CmdLists[n];
            memcpy(vtxDst, cmd->VtxBuffer.Data, (size_t)cmd->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmd->IdxBuffer.Data, (size_t)cmd->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmd->VtxBuffer.Size;
            idxDst += cmd->IdxBuffer.Size;
        }
    }

    // ------------------------------------------------------------
    // Bind buffers
    // ------------------------------------------------------------
    const UINT vbSizeInBytes = (requiredVB > UINT_MAX) ? UINT_MAX : (UINT)requiredVB;
    const UINT ibSizeInBytes = (requiredIB > UINT_MAX) ? UINT_MAX : (UINT)requiredIB;

    D3D12_VERTEX_BUFFER_VIEW vbv =
    {
        (*vertexBuffer)->GetGPUVirtualAddress(),
        vbSizeInBytes,
        sizeof(ImDrawVert)
    };

    D3D12_INDEX_BUFFER_VIEW ibv =
    {
        (*indexBuffer)->GetGPUVirtualAddress(),
        ibSizeInBytes,
        (sizeof(ImDrawIdx) == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT
    };

    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->IASetVertexBuffers(0, 1, &vbv);
    command_list->IASetIndexBuffer(&ibv);

    // ------------------------------------------------------------
    // Bind pipeline (root signature + PSO + root constants)
    // ------------------------------------------------------------
    if (!bd->RootSignature || !bd->PipelineState)
    {
        Logger::Log(LogLevel::Error, "❌ ImGui pipeline not created (RootSignature/PSO missing)");
        return;
    }

    command_list->SetGraphicsRootSignature(bd->RootSignature.Get());
    command_list->SetPipelineState(bd->PipelineState.Get());

    // Root param 0 = 16x 32-bit constants (ProjectionMatrix)
    // Match the standard ImGui orthographic projection.
    const float L = pos.x;
    const float R = pos.x + size.x;
    const float T = pos.y;
    const float B = pos.y + size.y;

    const float mvp[4][4] =
    {
        { 2.0f / (R - L), 0.0f,             0.0f, 0.0f },
        { 0.0f,           2.0f / (T - B),   0.0f, 0.0f },
        { 0.0f,           0.0f,             1.0f, 0.0f },
        { (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f },
    };

    command_list->SetGraphicsRoot32BitConstants(0, 16, mvp, 0);

    // Ensure the ImGui SRV heap is bound before setting descriptor tables.
    if (!bd->SrvDescHeap)
    {
        Logger::Log(LogLevel::Error, "❌ ImGui SRV heap missing");
        return;
    }
    {
        ID3D12DescriptorHeap* heaps[] = { bd->SrvDescHeap.Get() };
        command_list->SetDescriptorHeaps(1, heaps);
    }

    // ------------------------------------------------------------
    // Draw
    // ------------------------------------------------------------
    int vtxOffset = 0;
    int idxOffset = 0;

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd = draw_data->CmdLists[n];
        for (const ImDrawCmd& pcmd : cmd->CmdBuffer)
        {
            if (pcmd.UserCallback)
            {
                pcmd.UserCallback(cmd, &pcmd);
                continue;
            }

            // ClipRect is in ImGui space; convert to framebuffer scissor and clamp.
            LONG clipX1 = (LONG)(pcmd.ClipRect.x - pos.x);
            LONG clipY1 = (LONG)(pcmd.ClipRect.y - pos.y);
            LONG clipX2 = (LONG)(pcmd.ClipRect.z - pos.x);
            LONG clipY2 = (LONG)(pcmd.ClipRect.w - pos.y);

            const LONG fbW = (LONG)(draw_data->DisplaySize.x > 0.0f ? draw_data->DisplaySize.x : 1.0f);
            const LONG fbH = (LONG)(draw_data->DisplaySize.y > 0.0f ? draw_data->DisplaySize.y : 1.0f);

            clipX1 = (std::max)(0L, (std::min)(clipX1, fbW));
            clipY1 = (std::max)(0L, (std::min)(clipY1, fbH));
            clipX2 = (std::max)(0L, (std::min)(clipX2, fbW));
            clipY2 = (std::max)(0L, (std::min)(clipY2, fbH));

            if (clipX2 <= clipX1 || clipY2 <= clipY1)
                continue;

            D3D12_RECT scissor = { clipX1, clipY1, clipX2, clipY2 };
            command_list->RSSetScissorRects(1, &scissor);

            // Validate TextureId against the bound SRV heap to avoid DEVICE_HUNG on invalid descriptor.
            const D3D12_GPU_DESCRIPTOR_HANDLE heapGpuStart = bd->SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
            const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = bd->SrvDescHeap->GetDesc();
            const UINT inc = bd->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const UINT64 heapGpuEnd = heapGpuStart.ptr + (UINT64)heapDesc.NumDescriptors * (UINT64)inc;

            auto isTexIdInHeap = [&](ImTextureID texId) -> bool
            {
                const UINT64 ptr = (UINT64)texId;
                return ptr >= heapGpuStart.ptr && ptr < heapGpuEnd && ((ptr - heapGpuStart.ptr) % inc) == 0;
            };

            if (!isTexIdInHeap(pcmd.TextureId))
            {
                Logger::Log(LogLevel::Error, std::format(
                    "❌ ImGui draw skipped: TextureId=0x{:X} not in bound SRV heap [GPUStart=0x{:X}, GPUEnd=0x{:X}, Count={}, Inc={}]",
                    (UINT64)pcmd.TextureId, (UINT64)heapGpuStart.ptr, (UINT64)heapGpuEnd, heapDesc.NumDescriptors, inc));
                continue;
            }

            command_list->SetGraphicsRootDescriptorTable(
                1,
                *(D3D12_GPU_DESCRIPTOR_HANDLE*)&pcmd.TextureId);

            command_list->DrawIndexedInstanced(
                pcmd.ElemCount, 1,
                pcmd.IdxOffset + idxOffset,
                pcmd.VtxOffset + vtxOffset,
                0);
        }
        idxOffset += cmd->IdxBuffer.Size;
        vtxOffset += cmd->VtxBuffer.Size;
    }

    Logger::Log(LogLevel::Info,
        "✅ ImGui draw submitted successfully");
}

// =============================================================================
// Helper: Create Render Targets for a viewport swap chain (Safe, Per-Viewport)
// =============================================================================
static bool ImGui_ImplDX12_CreateRenderTargets(ImGui_ImplDX12_Data* bd, ImGui_ImplDX12_ViewportData* vd)
{
// Validate essential components
    if (!bd || !vd || !vd->SwapChain || !bd->Device || !vd->RTVHeap)
    {
        Logger::Log(LogLevel::Error, "❌ CreateRenderTargets: Invalid parameters (bd/vd/swapchain/device/RTVHeap missing).");
        return false;
    }

    Logger::Log(LogLevel::Debug, std::format(
        "ℹ️ Creating render targets for HWND=0x{:X} | BufferCount={}",
        reinterpret_cast<uintptr_t>(vd->Hwnd), vd->BufferCount));
// Validate per-viewport RTV heap
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = vd->RTVHeap->GetCPUDescriptorHandleForHeapStart();
    if (rtvHandle.ptr == 0)
    {
        Logger::Log(LogLevel::Error, "❌ RTVHeap is invalid for viewport.");
        return false;
    }

    UINT descriptorSize = bd->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
// Create RTVs for each back buffer
    for (UINT i = 0; i < vd->BufferCount; ++i)
    {
        vd->RenderTargets[i].Reset();

        HRESULT hr = vd->SwapChain->GetBuffer(i, IID_PPV_ARGS(&vd->RenderTargets[i]));
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, std::format("❌ GetBuffer({}) failed. HRESULT=0x{:08X}", i, (UINT)hr));
            return false;
        }

        vd->CurrentStates[i] = D3D12_RESOURCE_STATE_PRESENT;

        D3D12_CPU_DESCRIPTOR_HANDLE thisHandle = rtvHandle;
        thisHandle.ptr += static_cast<SIZE_T>(i) * descriptorSize;

        bd->Device->CreateRenderTargetView(vd->RenderTargets[i].Get(), nullptr, thisHandle);
        vd->RTVHandles[i] = thisHandle;

        vd->RenderTargets[i]->SetName(L"ImGuiViewportBackBuffer");

        Logger::Log(LogLevel::Debug, std::format(
            "✅ Created RTV[{}] | CPUHandle=0x{:X} | State=PRESENT", i, thisHandle.ptr));
    }
// Create Command Allocators
    for (UINT i = 0; i < vd->BufferCount; ++i)
    {
        if (!vd->CommandAllocators[i])
        {
            Logger::Log(LogLevel::Debug, std::format("ℹ️ Creating CommandAllocator[{}]", i));
            HRESULT hr = bd->Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&vd->CommandAllocators[i]));
            if (FAILED(hr))
            {
                Logger::Log(LogLevel::Error, std::format("❌ Failed to create CommandAllocator[{}]!", i));
                return false;
            }
        }
    }
// Create Command List (one per viewport)
    if (!vd->CommandList)
    {
        Logger::Log(LogLevel::Debug, "ℹ️ Creating CommandList for viewport...");
        HRESULT hr = bd->Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            vd->CommandAllocators[0].Get(), nullptr,
            IID_PPV_ARGS(&vd->CommandList));

        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, "❌ Failed to create CommandList!");
            return false;
        }

        hr = vd->CommandList->Close();
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, std::format(
                "❌ Failed to close CommandList after creation. HRESULT=0x{:08X}", (UINT)hr));
            return false;
        }

        Logger::Log(LogLevel::Info, "✅ CommandList created and closed successfully.");
    }

    Logger::Log(LogLevel::Info, std::format(
        "ℹ️ Render targets + allocators created using RTVHeap for HWND=0x{:X}",
        reinterpret_cast<uintptr_t>(vd->Hwnd)));
    return true;
}



// =============================================================================
// Destroy Render Targets for a viewport
// =============================================================================
static void ImGui_ImplDX12_DestroyRenderTargets(ImGui_ImplDX12_ViewportData* vd, bool fullDestroy = false)
{
    if (!vd)
        return;

    Logger::Log(LogLevel::Debug, std::format(
        "ℹ️ DestroyRenderTargets | HWND=0x{:X} | Full={} ",
        reinterpret_cast<uintptr_t>(vd->Hwnd), fullDestroy));

    // 1. Wait per-buffer before destruction
    if (vd->Fence && vd->FenceEvent && ImGui_ImplDX12_GetBackendData()->CommandQueue)
    {
        for (UINT i = 0; i < vd->BufferCount; ++i)
        {
            UINT64 completed = vd->Fence->GetCompletedValue();
            UINT64 expected = vd->FenceValues[i];

            if (completed < expected)
            {
                Logger::Log(LogLevel::Info, std::format(
                    "ℹ️ Waiting for GPU before releasing BackBuffer[{}] | Completed={} Expected={}",
                    i, completed, expected));

                vd->Fence->SetEventOnCompletion(expected, vd->FenceEvent);
                WaitForSingleObject(vd->FenceEvent, INFINITE);
            }

            if (vd->RenderTargets[i])
            {
                Logger::Log(LogLevel::Debug, std::format("ℹ️ Releasing BackBuffer[{}]", i));
                vd->RenderTargets[i].Reset();
            }

            vd->RTVHandles[i].ptr = 0;
            vd->CurrentStates[i] = D3D12_RESOURCE_STATE_PRESENT;
        }
    }
    else
    {
        Logger::Log(LogLevel::Warning, "⚠️ GPU sync skipped during DestroyRenderTargets (missing Fence or CommandQueue)");
    }

    // 2. Reset RTV heap (only if full destroy or guaranteed recreate)
    if (fullDestroy)
    {
        vd->RTVHeap.Reset();
    }

    // 3. Release persistent VB/IB safely
    if (vd->VertexBuffer)
    {
        Logger::Log(LogLevel::Debug, "ℹ️ Releasing persistent VertexBuffer.");
        vd->VertexBuffer.Reset();
        vd->MappedVertexBuffer = nullptr;
        vd->VertexBufferSize = 0;
    }
    if (vd->IndexBuffer)
    {
        Logger::Log(LogLevel::Debug, "ℹ️ Releasing persistent IndexBuffer.");
        vd->IndexBuffer.Reset();
        vd->MappedIndexBuffer = nullptr;
        vd->IndexBufferSize = 0;
    }

    // 4. Release command objects & fence only if FULL destruction
    if (fullDestroy)
    {
        for (UINT i = 0; i < vd->BufferCount; ++i)
            vd->CommandAllocators[i].Reset();

        vd->CommandList.Reset();
        vd->Fence.Reset();

        if (vd->FenceEvent)
        {
            CloseHandle(vd->FenceEvent);
            vd->FenceEvent = nullptr;
        }

        uintptr_t hwnd_value = reinterpret_cast<uintptr_t>(vd->Hwnd);
        vd->Hwnd = nullptr;

        Logger::Log(LogLevel::Info, std::format(
            "✅ Fully destroyed viewport resources | HWND=0x{:X}", hwnd_value));
    }
    else
    {
        Logger::Log(LogLevel::Info, "ℹ️ Destroyed backbuffers + RTVs (lightweight cleanup). ");
    }
}

// ============================================================================
// ImGui_ImplDX12_CreateWindow (Multi-Viewport Window Creation, Persistent Buffers)
// Uses shared CommandQueue (no per-viewport queues) for fence stability.
// ============================================================================
void ImGui_ImplDX12_CreateWindow(ImGuiViewport* viewport)
{
    if (!viewport)
        return;

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

    // Main Viewport (engine-owned swapchain, renderer passthrough)
    if (viewport->ID == ImGui::GetMainViewport()->ID)
    {
        ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();

        auto* vd = IM_NEW(ImGui_ImplDX12_ViewportData)();
        memset(vd, 0, sizeof(ImGui_ImplDX12_ViewportData));

        vd->IsMainViewport = true;
        vd->IsActive = true;
        vd->Hwnd = hwnd;
        vd->Device = bd->Device;
        vd->CommandQueue = bd->InitInfo.CommandQueue;
        const UINT bufferCount = ImGui_ImplDX12_ViewportData::BufferCount;
        vd->FrameIndex = 0;

        // ❌ NO swapchain
        // ❌ NO RTVs
        // ❌ NO fences
        // ❌ NO allocators
        // ❌ NO frame resources

        viewport->RendererUserData = vd;

        Logger::Log(LogLevel::Info,
            "✅ Main viewport RendererUserData created (engine-owned swapchain)");

        return;
    }

// Initial size sanitization
    ImVec2 size = viewport->Size;
    if (!std::isfinite(size.x) || size.x < 64.0f) size.x = 1280.0f;
    if (!std::isfinite(size.y) || size.y < 64.0f) size.y = 720.0f;
    viewport->Size = size;
    viewport->PlatformRequestResize = true;
// Clamp & sanitize size
    ImVec2 raw_size = viewport->Size;
    raw_size.x = ImClamp(raw_size.x, 64.0f, 8192.0f);
    raw_size.y = ImClamp(raw_size.y, 64.0f, 8192.0f);
    int width = (int)raw_size.x;
    int height = (int)raw_size.y;
    viewport->Size = ImVec2((float)width, (float)height);

    // DPI scale fallback
    if (!std::isfinite(viewport->DpiScale) || viewport->DpiScale <= 0.0f)
        viewport->DpiScale = 1.0f;

    SanitizeViewportSize(viewport->Size);

    Logger::Log(LogLevel::Debug, std::format(
        "ℹ️ ViewportID=0x{:X} | Size={}x{} | DpiScale={:.2f}",
        viewport->ID, width, height, viewport->DpiScale));

    // HWND validation
    if (!hwnd || !::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format(
            "⚠️ CreateWindow skipped — invalid HWND (ViewportID=0x{:X})", viewport->ID));
        return;
    }

    if (viewport->RendererUserData)
    {
        Logger::Log(LogLevel::Warning, std::format(
            "⚠️ RendererUserData already exists! ViewportID=0x{:X}, HWND=0x{:X}",
            viewport->ID, (uintptr_t)hwnd));
        return;
    }

    // Allocate persistent data
    auto* vd = IM_NEW(ImGui_ImplDX12_ViewportData)();
    vd->Hwnd = hwnd;
    viewport->RendererUserData = vd;

    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    vd->Device = bd->Device;
    vd->RTVHeap = bd->RTVDescriptorHeap;
    vd->CommandQueue = bd->InitInfo.CommandQueue;

    for (UINT i = 0; i < bd->NumFramesInFlight; i++)
    {
        vd->FenceValues[i] = 0;
        vd->CurrentStates[i] = D3D12_RESOURCE_STATE_PRESENT;
    }
    vd->LastSubmittedFenceValue = 0;
    vd->FrameIndex = 0;
    Logger::Log(LogLevel::Debug, std::format("ℹ️ InitInfo.NumFramesInFlight: {}", bd->InitInfo.NumFramesInFlight));

    // Secondary viewport swapchain creation is not supported in this build.
    bd->InitInfo.SwapChain = nullptr;
    Logger::Log(LogLevel::Warning, "⚠️ ImGui DX12: Secondary viewport swapchain creation is disabled.");
    return;

    // Create swapchain and RTVs
    // NOTE: Secondary viewport swapchains are not supported in this build.
    // if (!ImGui_ImplDX12_CreateSwapChainAndResources(viewport, vd))
    // {
    //     Logger::Log(LogLevel::Error, "❌ Failed to create swapchain or render targets.");
    //     ImGui_ImplDX12_DestroyWindow(viewport);
    //     viewport->RendererUserData = nullptr;
    //     return;
    // }

    for (UINT i = 0; i < ImGui_ImplDX12_ViewportData::BufferCount; i++)
        vd->CurrentStates[i] = D3D12_RESOURCE_STATE_PRESENT;

    // Fence creation (only once)
    HRESULT hr = vd->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&vd->Fence));
    if (FAILED(hr) || !vd->Fence)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ Failed to create fence (HRESULT=0x{:08X})", (UINT)hr));
        ImGui_ImplDX12_DestroyWindow(viewport);
        viewport->RendererUserData = nullptr;
        return;
    }

    vd->FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!vd->FenceEvent)
    {
        Logger::Log(LogLevel::Error, "❌ Failed to create FenceEvent.");
        ImGui_ImplDX12_DestroyWindow(viewport);
        viewport->RendererUserData = nullptr;
        return;
    }

    // Initialize fence values.
    UINT64 completed = vd->Fence->GetCompletedValue();
    if (completed == UINT64_MAX) completed = 0;
    for (UINT i = 0; i < ImGui_ImplDX12_ViewportData::BufferCount; ++i)
        vd->FenceValues[i] = completed;
    vd->LastSubmittedFenceValue = completed;

    Logger::Log(LogLevel::Debug, "✅ Fence initialized");

    // Command Allocators
    for (UINT i = 0; i < ImGui_ImplDX12_ViewportData::BufferCount; i++)
    {
        hr = vd->Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&vd->CommandAllocators[i]));
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, std::format("❌ Failed to create CommandAllocator[{}]!", i));
            return;
        }
    }

    hr = vd->Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, vd->CommandAllocators[0].Get(),
        nullptr, IID_PPV_ARGS(&vd->CommandList));
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, "❌ Failed to create CommandList!");
        return;
    }
    vd->CommandList->Close();
    Logger::Log(LogLevel::Info, "✅ Command allocators + command list created successfully.");

    // NOTE: Per-viewport persistent VB/IB are not used by this renderer path; it uses FrameResources[].
    // Avoid allocating duplicate buffers here.

    // Swapchain creation for secondary viewports is currently not supported in this engine build.
    // The engine runs with `ImGuiConfigFlags_ViewportsEnable` disabled, so we can safely early-out.
    Logger::Log(LogLevel::Warning, "⚠️ ImGui DX12: Secondary viewport swapchain creation is disabled.");
    return;
}

// -----------------------------------------------------------------------------
// Invalidate device objects
// -----------------------------------------------------------------------------
void ImGui_ImplDX12_InvalidateDeviceObjects()
{
    ImGui_ImplDX12_Data* bd = ImGui_ImplDX12_GetBackendData();
    if (!bd)
        return;

    Logger::Log(LogLevel::Info, std::format("ℹ️ Invalidating ImGui DX12 device objects (bd: {})", static_cast<void*>(bd)));

    bd->PipelineState.Reset();
    bd->RootSignature.Reset();

    Logger::Log(LogLevel::Debug, "✅ PSO and RootSignature released.");

    // Allow lazy PSO/root signature recreation.
    g_DeviceObjectsCreated = false;
    g_DeviceObjectsCreateFailed = false;
}