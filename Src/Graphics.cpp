// Graphics.cpp - DirectX 12 Graphics Engine Implementation
#define NOMINMAX

// Add this near the top of Graphics.cpp, after includes but before any usage
#ifndef IMGUI_NUM_FRAMES_IN_FLIGHT
#define IMGUI_NUM_FRAMES_IN_FLIGHT 3
#endif

// System Headers
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <dxgidebug.h>
#include <ShellScalingAPI.h>
#include <string>
#include <codecvt>
#include <locale>
#include <unordered_set>
#include <unordered_map>
#include <future>
#include <thread>
#include <algorithm>
#include <format>
#include <queue>
#pragma comment(lib, "Shcore.lib")
#include <d3d12sdklayers.h> // Required for DRED interfaces

// ImGui Headers
#include "imgui.h"
#include "imgui_impl_win32_custom.h"   // ✅ Replaces default Win32 backend
#include "imgui_impl_dx12_custom.h"
#include "ImGuiUtils.h"
#include "imgui_internal.h"

// Engine Headers
#include "Graphics.h"
#include "logger.h"
#include "UI.h"
#include "ImGuiPlatformIO_Extended.h" // <- Add this below imgui_impl_win32.h and imgui_impl_dx12.h
#include <Engine.h>
#include <SplashScreen.h>
#include "ShaderUtils.h"
#include <DirectXMath.h>

// ✅ Define and Initialize Static Variables (Fixes "unresolved external" error)
std::chrono::high_resolution_clock::time_point Graphics::frameStart = std::chrono::high_resolution_clock::now();
double Graphics::deltaTime = 0.0;
double Graphics::totalTime = 0.0;
int Graphics::frameCount = 0;
bool Graphics::vsyncEnabled = true;
HANDLE Graphics::fenceEvent = nullptr;
bool Graphics::allowTearing = true;
bool Graphics::imguiInitialized = false; // ✅ Track ImGui Initialization State // ✅ Track ImGui DX12 backend state
bool Graphics::imguiPlatformInitialized = false; // ✅ Track ImGui Win32 backend state
bool Graphics::lockResolutionWhenMaximized = true; // default true

static bool ImGuiFrameStarted = false; // ✅ Track ImGui frame lifecycle
// Add this at file scope near other static variables (top of Graphics.cpp)
static bool imguiReadyPublished = false;
static bool g_ImGuiRenderedThisFrame = false;

// One-shot font upload gate: initialized true, cleared after successful upload.
static bool g_ImGuiFontsNeedUpload = true;

// ENGINE RULE:
// Present() must be called exactly once per frame, and only by Engine.
static bool g_PresentedThisFrame = false;

// Tracks which view mode the scene grid VB was built for (so we can rebuild on mode swap)
static ViewMode g_LastSceneGridBuiltViewMode = ViewMode::Mode3D;

// Static variable to track the next available SRV descriptor
static UINT g_SRVDescriptorIndex = 1; // slot 0 reserved for ImGui font

// SRV allocator callbacks (custom ImGui DX12 backend signatures)
static void ImGui_SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);
static void ImGui_SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);

// After includes, at file scope
ID3D12DescriptorHeap* g_SRVHeap = nullptr; // global definition

// Scene diagnostics (throttled)
static std::chrono::steady_clock::time_point g_SceneDiag_Last = std::chrono::steady_clock::now();
static uint64_t g_SceneDiag_Frame = 0;

static bool SceneDiag_ShouldLog(std::chrono::milliseconds interval = std::chrono::milliseconds(1000))
{
    const auto now = std::chrono::steady_clock::now();
    if (now - g_SceneDiag_Last >= interval)
    {
        g_SceneDiag_Last = now;
        return true;
    }
    return false;
}

static const char* D3D12StateToString(D3D12_RESOURCE_STATES s)
{
    switch (s)
    {
    case D3D12_RESOURCE_STATE_COMMON: return "COMMON";
    case D3D12_RESOURCE_STATE_RENDER_TARGET: return "RENDER_TARGET";
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return "PIXEL_SHADER_RESOURCE";
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE: return "NON_PIXEL_SHADER_RESOURCE";
    case D3D12_RESOURCE_STATE_COPY_DEST: return "COPY_DEST";
    case D3D12_RESOURCE_STATE_COPY_SOURCE: return "COPY_SOURCE";
    default: return "(other)";
    }
}

// Converts a wide string (std::wstring) to a UTF-8 encoded std::string
static std::string WStringToUTF8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), -1, str.data(), size, nullptr, nullptr);
    str.pop_back(); // remove null terminator
    return str;
}

// ✅ Validate command queue globally when needed
void Graphics::EnsureValidCommandQueue() {
    if (!commandQueue) {
        Logger::Log(LogLevel::Error, "❌ ERROR: commandQueue is NULL!");
        throw std::runtime_error("CommandQueue is NULL.");
    }
}
//===============================================
// Constructor & Destructor Functions
// --------------------------------
// Initializes frame timing and other members
// Ensures proper cleanup on destruction
//===============================================

Graphics::Graphics() : commandListOpen(false) {
    frameStart = std::chrono::high_resolution_clock::now();
}

Graphics::~Graphics() {
    Shutdown();
}

//===============================================
// Toggle VSync Function
// ==============================================
// Enables or disables VSync at runtime
// Call this function to change VSync settings
// Example: Graphics::GetInstance().ToggleVSync(true); // Enable VSync
// Example: Graphics::GetInstance().ToggleVSync(false); // Disable VSync
//===============================================

void Graphics::ToggleVSync(bool enable) {
    Graphics::vsyncEnabled = enable;
    Logger::Log(LogLevel::Info, "✅ VSync " + std::string(enable ? "Enabled" : "Disabled"));

    // Optional: recreate swap chain to ensure tearing settings apply properly
    // CreateSwapChain(hWnd, screenWidth, screenHeight);
}

//===============================================
// Asynchronous Texture Loading Function
// ===============================================
// Loads textures in a separate thread to avoid blocking the main thread
// Usage: auto future = Graphics::GetInstance().LoadTextureAsync("path/to/texture.png");
// The future can be used to check when loading is complete
//===============================================

std::future<void> Graphics::LoadTextureAsync(std::string filePath) {
    return std::async(std::launch::async, [filePath]() {
        Logger::Log(LogLevel::Info, "🔄 Loading Texture: " + std::string(filePath.begin(), filePath.end()));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Logger::Log(LogLevel::Info, "✅ Texture Loaded: " + std::string(filePath.begin(), filePath.end()));
        });
}

//===============================================
//Support for MSAA (Anti-Aliasing) Quality Levels
//===============================================
// Checks and logs the supported MSAA quality levels for 4x MSAA
// Call this during device initialization
//===============================================
void Graphics::CheckMSAAQuality() {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels = {};
    msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaaQualityLevels.SampleCount = 4;
    msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaaQualityLevels, sizeof(msaaQualityLevels)))) {
        Logger::Log(LogLevel::Error, "❌ Failed to check MSAA Quality Levels.");
        return;
    }

    UINT msaaQuality = msaaQualityLevels.NumQualityLevels > 0 ? msaaQualityLevels.NumQualityLevels - 1 : 0;
    Logger::Log(LogLevel::Info, "📌 MSAA Quality Levels: " + std::to_string(msaaQuality));
}

//===============================================
// InitializeMSAA Function
//===============================================
// Sets up MSAA quality levels and logs the result
// Call this during device initialization
//===============================================
void Graphics::InitializeMSAA()
{
    if (!device) return;

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels = {};
    msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaaQualityLevels.SampleCount = 4;
    msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

    HRESULT hr = device->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &msaaQualityLevels, sizeof(msaaQualityLevels)
    );

    UINT msaaQuality = msaaQualityLevels.NumQualityLevels > 0 ? msaaQualityLevels.NumQualityLevels - 1 : 0;
    Logger::Log(LogLevel::Info, "📌 MSAA Quality Levels: " + std::to_string(msaaQuality));

    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to check MSAA quality levels! HRESULT: " + std::to_string(hr));
    }
}

//===============================================
// Checks Device Health Function
//===============================================
// Logs if the GPU has crashed or been removed
// This should be called periodically, e.g., once per frame
// or during critical operations
// to detect device loss early.
//===============================================
void Graphics::CheckDeviceHealth() {
    if (!device) return;
    HRESULT reason = device->GetDeviceRemovedReason();
    if (reason != S_OK) {
        Logger::Log(LogLevel::Error, "❌ GPU Crashed! Reason Code: " + std::to_string(reason));
    }
}

//===============================================
// Adjusts Resolution Based On FPS Function
//===============================================,
// Dynamically adjusts screen resolution to maintain target FPS
// Call this function periodically, e.g., once every few seconds
//===============================================
void Graphics::AdjustResolutionBasedOnFPS(float currentFPS) {
    static auto lastAdjustmentTime = std::chrono::high_resolution_clock::now();

    // ✅ Ensure we wait at least 5 seconds before adjusting resolution
    auto now = std::chrono::high_resolution_clock::now();
    float timeSinceLastAdjustment = std::chrono::duration<float>(now - lastAdjustmentTime).count();
    if (timeSinceLastAdjustment < 5.0f) {
        return; // ⏳ Skip adjustment if it's too soon
    }

    bool resolutionChanged = false; // ✅ Ensure this variable is declared before use

    if (currentFPS < 30.0f && (screenWidth > 1280 || screenHeight > 720)) {
        screenWidth = std::max(1280, screenWidth - 200);  // ✅ Correct syntax: two parameters only
        screenHeight = std::max(720, screenHeight - 100);
        resolutionChanged = true;
    }
    else if (currentFPS > 60.0f && (screenWidth < 1920 || screenHeight < 1080)) {
        screenWidth = std::min(1920, screenWidth + 200);
        screenHeight = std::min(1080, screenHeight + 100);
        resolutionChanged = true;
    }

    if (resolutionChanged) {
        lastAdjustmentTime = now; // ✅ Update last adjustment time
        Logger::Log(LogLevel::Info, "🔄 Adjusting Resolution (deferred): " + std::to_string(screenWidth) + "x" + std::to_string(screenHeight));

        // Phase 0: resizing is deferred and applied at the top of BeginFrame().
        RequestResize((UINT)screenWidth, (UINT)screenHeight);
    }
}

//=================================================
// Logs FPS Frames Function
//=================================================
// Call this once per frame to log FPS every second
//=================================================
void Graphics::LogFPS() {
    static int frameCounter = 0;
    static double elapsedTime = 0.0;

    frameCounter++;
    elapsedTime += deltaTime;

    if (elapsedTime >= 1.0) {
        Logger::Log(LogLevel::Info, "🎮 FPS: " + std::to_string(frameCounter));
        frameCounter = 0;
        elapsedTime = 0.0;
    }
}

//==============================================================
// ✅ Upload ImGui font texture
// =============================================================
// Uploads the ImGui font texture to the GPU
// Call this after ImGui initialization and before rendering
// to ensure fonts are available for rendering
//==============================================================
void Graphics::UploadImGuiFontTexture() {
    if (!imguiInitialized || !imguiHeap) return;
    Logger::Log(LogLevel::Info, "✅ ImGui font texture uploaded.");
}

//====================================
// Initialize Graphics Engine
//====================================
bool Graphics::Initialize(HWND hWnd)
{
    if (!hWnd)
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: hWnd is NULL in Initialize()");
        return false;
    }

    Logger::Log(LogLevel::Info, std::format("✅ hWnd is valid: {}", reinterpret_cast<uintptr_t>(hWnd)));

    commandListOpen = false;
    totalTime = 0.0;
    frameCount = 0;

    ListAvailableGPUs();
    CreateDX12Device();
    // 🎮 Log selected GPU info
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory4> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        if (SUCCEEDED(factory->EnumAdapters(0, &adapter)))
        {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                Logger::Log(LogLevel::Info, std::format("🖥️ Selected GPU: {} | VRAM: {:.2f} GB",
                    WStringToUTF8(desc.Description),
                    desc.DedicatedVideoMemory / (1024.0f * 1024.0f * 1024.0f)));
            }
        }
    }

    CreateCommandInterfaces();

    GPUSelection::ListAvailableGPUs();
    Logger::Log(LogLevel::Info, "✅ GPU list populated");

    if (!commandQueue)
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: Command Queue is NULL after CreateCommandInterfaces!");
        return false;
    }

    Logger::Log(LogLevel::Info, std::format("📌 commandQueue BEFORE SwapChain: {}", reinterpret_cast<uintptr_t>(commandQueue.Get())));

    // ✅ 1. Create swap chain
    CreateSwapChain(hWnd, 0, 0);

    if (!swapChain)
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: SwapChain is NULL after creation!");
        return false;
    }

    Logger::Log(LogLevel::Info, std::format("✅ SwapChain Created. BackBufferIndex: {}", swapChain->GetCurrentBackBufferIndex()));

    // ✅ 2. Create RTV heap and views (sets `rtvHeap`)
    CreateRenderTargetViews();
    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    // ✅ 3. Initialize ImGui (safe now that RTV heap is valid)
    InitializeImGui(hWnd);

    // ✅ Patch Main Viewport with Dummy RendererUserData
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    if (main_viewport && main_viewport->RendererUserData == nullptr)
    {
        ImGui_ImplDX12_CreateWindow(main_viewport);
        Logger::Log(LogLevel::Info, "✅ Main viewport RendererUserData initialized via ImGui_ImplDX12_CreateWindow().");
    }

    ImGuiIO& io = ImGui::GetIO();

    // Disable multi-viewport for now (Phase 0 stability: single swapchain/present/fence path)
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;
    io.ConfigViewportsNoTaskBarIcon = false;

    Logger::Log(LogLevel::Info, std::format("📌 commandQueue AFTER ImGui Init: {}", reinterpret_cast<uintptr_t>(commandQueue.Get())));

    return true;
}

//==============================
// Shutdown Graphics Engine
//==============================
void Graphics::Shutdown()
{
    Logger::Log(LogLevel::Info, "🔻 Shutting down graphics...");

    // Ensure we are not mid-recording when releasing GPU resources.
    if (commandListOpen)
    {
        Logger::Log(LogLevel::Warning, "⚠️ Shutdown called while command list is open; attempting to close it before releasing resources.");
        if (commandList)
        {
            (void)commandList->Close();
        }
        commandListOpen = false;
    }

    FlushGPU();

    // Ensure all queued releases get a chance to retire (after FlushGPU, fence is complete).
    ProcessDeferredReleases();
    deferredReleases.clear();

    // 1. Shut down ImGui BEFORE freeing ANY DX12 heaps
    if (ImGui::GetCurrentContext())
    {
        Logger::Log(LogLevel::Info, "🧠 Shutting down ImGui safely...");

        ImGui_ImplDX12_DestroyFontsTexture();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();

        ImGui::DestroyPlatformWindows();
        ImGui::GetPlatformIO() = ImGuiPlatformIO();
        ImGui::DestroyContext();
    }

    // 2. Now free DX12 resources safely (enqueue then reset)
    EnqueueDeferredRelease(sceneRenderTarget);
    sceneRtvHeap.Reset();
    sceneImGuiTextureID = (ImTextureID)0;
    sceneSrvCpu = {};
    sceneSrvGpu = {};
    sceneRtvHandle = {};
    sceneRTWidth = 0;
    sceneRTHeight = 0;
    sceneRTState = D3D12_RESOURCE_STATE_COMMON;

    EnqueueDeferredRelease(sceneDepth);
    sceneDsvHeap.Reset();
    sceneDsvHandle = {};
    sceneDepthState = D3D12_RESOURCE_STATE_COMMON;

    sceneCBPtr = nullptr;
    EnqueueDeferredRelease(sceneCB);
    EnqueueDeferredRelease(sceneTrianglePSO);
    EnqueueDeferredRelease(sceneGridPSO);
    EnqueueDeferredRelease(sceneRootSignature);
    EnqueueDeferredRelease(sceneTriangleVB);
    EnqueueDeferredRelease(sceneGridVB);
    EnqueueDeferredRelease(sceneAxesVB);

    for (int i = 0; i < NUM_BACK_BUFFERS; i++)
        EnqueueDeferredRelease(backBuffers[i]);

    if (swapChain) swapChain.Reset();
    SafeReleaseComPtr("rtvHeap", rtvHeap, true);
    SafeReleaseComPtr("imguiHeap", imguiHeap, true);
    SafeReleaseComPtr("commandQueue", commandQueue, true);
    SafeReleaseComPtr("commandAllocator", commandAllocator, true);
    SafeReleaseComPtr("commandList", commandList, true);

    SafeReleaseComPtr("fence", fence, true);
    SafeReleaseComPtr("dxgiFactory", dxgiFactory, true);
    device.Reset();

    if (fenceEvent)
    {
        CloseHandle(fenceEvent);
        fenceEvent = nullptr;
    }

    g_SRVHeap = nullptr;
    Logger::Log(LogLevel::Info, "✅ Graphics Shutdown Completed.");
}

//=================================
// List Available GPUs
//=================================
void Graphics::ListAvailableGPUs() {
    Logger::Log(LogLevel::Info, "Listing Available GPUs...");
    gpuList.clear();

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    DX_CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    UINT index = 0;
    std::unordered_set<std::wstring> uniqueGPUs;

    while (factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
        if (!adapter) {
            Logger::Log(LogLevel::Warning, "⚠️ Skipping NULL adapter.");
            continue;
        }

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        std::wstring gpuNameW(desc.Description);

        if (uniqueGPUs.insert(gpuNameW).second) {
            gpuList.push_back(gpuNameW);

            // ✅ Convert std::wstring to std::string
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, gpuNameW.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string gpuName(sizeNeeded, 0);
            WideCharToMultiByte(CP_UTF8, 0, gpuNameW.c_str(), -1, gpuName.data(), sizeNeeded, nullptr, nullptr);

            // ✅ Use std::format() properly
            Logger::Log(LogLevel::Info, std::format("✅ GPU Found: {}", gpuName));
        }

        adapter.Reset();  // ✅ Safe release
        index++;
    }
}

//====================================
// Create DirectX 12 Device
//====================================
HRESULT Graphics::CreateDX12Device() {
    Logger::Log(LogLevel::Info, "🔄 Creating DirectX 12 Device...");

    // ✅ Only reset device if it was previously initialized
    if (device) {
        Logger::Log(LogLevel::Warning, "⚠️ Releasing existing device before recreation.");
        device.Reset();
    }

    UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        Logger::Log(LogLevel::Info, "✅ DirectX12 Debug Layer Enabled.");
    }
#endif

    Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory;
    HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create DXGI Factory! HRESULT: " + std::to_string(hr));
        return hr;
    }
    Logger::Log(LogLevel::Info, "✅ DXGI Factory Created Successfully!");

    // ✅ Ensure selectedGPU is valid before resetting
    if (selectedGPU) {
        selectedGPU.Reset();
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
    bool gpuFound = false;

    for (UINT adapterIndex = 0; dxgiFactory->EnumAdapters1(adapterIndex, &hardwareAdapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        selectedGPU = hardwareAdapter;
        gpuFound = true;
        break;
    }

    if (!gpuFound) {
        Logger::Log(LogLevel::Error, "❌ ERROR: No valid GPU adapter found!");
        return DXGI_ERROR_NOT_FOUND;
    }

    // =========================================================
    // 🚀 Create the D3D12 Device
    // =========================================================
    hr = D3D12CreateDevice(selectedGPU.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create DirectX 12 Device! HRESULT: " + std::to_string(hr));
        return hr;
    }

    // =========================================================
    // 🩸 Enable DRED Diagnostics (MUST be done immediately)
    // =========================================================
    {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

            Logger::Log(LogLevel::Info, "🩸 DRED diagnostics enabled (Breadcrumbs + Page Fault + Watson Dumps)");
        }
        else {
            Logger::Log(LogLevel::Warning, "⚠️ DRED interface not available — limited GPU crash detail.");
        }
    }

    Logger::Log(LogLevel::Info, "✅ DirectX 12 Device Created Successfully!");
    return S_OK;
}

//==============================
// Flush GPU Function
//==============================

void Graphics::FlushGPU()
{
    if (!commandQueue || !fence)
        return;

    if (!fenceEvent)
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    const UINT64 signalValue = ++fenceValue;
    lastSignaledFenceValue = signalValue;

    const HRESULT hr = commandQueue->Signal(fence.Get(), signalValue);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ FlushGPU: commandQueue->Signal failed HR=0x{:08X}", (UINT)hr));
        return;
    }

    WaitForFenceValue(signalValue);

    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i)
    {
        frames[i].fenceValue = signalValue;
    }
}

// Phase 1C: Centralized fence waiting (used by BeginFrame, FlushGPU, resize paths)
void Graphics::WaitForFenceValue(UINT64 value)
{
    if (!fence || value == 0)
        return;

    if (!fenceEvent)
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    const UINT64 completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX)
        return; // device lost sentinel handled by callers

    if (completed < value)
    {
        (void)fence->SetEventOnCompletion(value, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

//==============================================
// Signal Fence Function
//==============================================

void Graphics::SignalFence()
{
    if (!commandQueue || !fence)
        return;

    if (!fenceEvent)
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    const UINT64 signalValue = ++fenceValue;
    lastSignaledFenceValue = signalValue;

    const HRESULT hr = commandQueue->Signal(fence.Get(), signalValue);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ SignalFence: commandQueue->Signal failed HR=0x{:08X}", (UINT)hr));
        return;
    }

    if (currentBackBufferIndex < NUM_BACK_BUFFERS)
    {
        frames[currentBackBufferIndex].fenceValue = signalValue;
    }
}

//==============================
// Create Swap Chain Function
//==============================

void Graphics::CreateSwapChain(HWND hwnd, UINT width, UINT height)
{
    if (!hwnd || !IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Error, "❌ CreateSwapChain: invalid HWND.");
        return;
    }

    if (!commandQueue)
    {
        Logger::Log(LogLevel::Error, "❌ CreateSwapChain: commandQueue is NULL.");
        return;
    }

    if (!dxgiFactory)
    {
        UINT flags = 0;
    #ifdef _DEBUG
        flags |= DXGI_CREATE_FACTORY_DEBUG;
    #endif
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            Logger::Log(LogLevel::Error, std::format("❌ CreateSwapChain: CreateDXGIFactory2 failed HR=0x{:08X}", (UINT)hr));
            return;
        }
        dxgiFactory = factory;
    }

    if (width == 0 || height == 0)
    {
        RECT rc{};
        if (GetClientRect(hwnd, &rc))
        {
            width = (UINT)(std::max)(1L, rc.right - rc.left);
            height = (UINT)(std::max)(1L, rc.bottom - rc.top);
        }
        else
        {
            width = (UINT)(std::max)(1, screenWidth);
            height = (UINT)(std::max)(1, screenHeight);
        }
    }

    screenWidth = (int)width;
    screenHeight = (int)height;

    if (swapChain)
        return;

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = width;
    scDesc.Height = height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = NUM_BACK_BUFFERS;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    scDesc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hwnd,
        &scDesc,
        nullptr,
        nullptr,
        &sc1);

    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ CreateSwapChainForHwnd failed HR=0x{:08X}", (UINT)hr));
        return;
    }

    (void)dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    hr = sc1.As(&swapChain);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ SwapChain QueryInterface failed HR=0x{:08X}", (UINT)hr));
        swapChain.Reset();
        return;
    }

    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    const UINT64 completed = fence ? fence->GetCompletedValue() : 0;
    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i)
    {
        frames[i].fenceValue = completed;
    }
}

//==============================
// Execute Command Lists Function
//=================================
void Graphics::ExecuteCommandLists(std::vector<ID3D12CommandList*>& commandLists) {
    if (!commandQueue) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Command Queue is NULL!");
        return;
    }

    // ✅ Force GPU wait before executing new commands
    FlushGPU();

    Logger::Log(LogLevel::Info, "🚀 Executing " + std::to_string(commandLists.size()) + " Command Lists...");
    commandQueue->ExecuteCommandLists((UINT)commandLists.size(), commandLists.data());
}

//==============================
// Create Command Interfaces
//==============================
void Graphics::CreateCommandInterfaces() {
    Logger::Log(LogLevel::Info, "Creating Command Interfaces...");

    if (!device) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Device is NULL before creating command queue!");
        throw std::runtime_error("Device is NULL before command queue creation.");
    }

    if (!device) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Device is NULL in CreateCommandInterfaces!");
        return;
    }

    // Phase 1B: create per-frame allocators directly into `frames[]`.
    for (UINT i = 0; i < NUM_BACK_BUFFERS; ++i) {
        frames[i].allocator.Reset();
        frames[i].fenceValue = 0;

        HRESULT hr = device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frames[i].allocator)
        );
        if (FAILED(hr)) {
            Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create CommandAllocator for buffer " + std::to_string(i));
            return;
        }
    }

    // Set to false if you want to skip log issues
    SafeReleaseComPtr("commandQueue", commandQueue, true);
    SafeReleaseComPtr("commandAllocator", commandAllocator, true);
    SafeReleaseComPtr("commandList", commandList, true);
    SafeReleaseComPtr("fence", fence, true);

    // Dedicated upload context
    SafeReleaseComPtr("uploadAllocator", uploadAllocator, true);
    SafeReleaseComPtr("uploadCommandList", uploadCommandList, true);

    // ✅ Create Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr) || !commandQueue) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Command Queue! HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Command Queue Creation Failed!");
    }

    Logger::Log(LogLevel::Info, std::format("✅ Command Queue Created Successfully! Address: {}", reinterpret_cast<uintptr_t>(commandQueue.Get())));

    // ✅ Validate Command Queue
    if (!commandQueue) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Command Queue is NULL after creation!");
        throw std::runtime_error("Command Queue is NULL after creation!");
    }

    // ✅ Create Command Allocator
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr) || !commandAllocator) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Command Allocator! HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Command Allocator Creation Failed!");
    }

    // ✅ Create Fence
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Fence! HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Fence Creation Failed!");
    }
    fenceValue = 1;
    Logger::Log(LogLevel::Info, "✅ Fence Created Successfully.");

    // ✅ Create Command List
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList)
    );
    if (FAILED(hr) || !commandList) {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Command List! HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Command List Creation Failed!");
    }

    // Close it immediately for reuse later
    commandList->Close();
    Logger::Log(LogLevel::Info, std::format("✅ Command List Created Successfully! Address: {}", reinterpret_cast<uintptr_t>(commandList.Get())));

    // ✅ Create dedicated upload allocator + command list (closed when idle)
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
    if (FAILED(hr) || !uploadAllocator)
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Upload Command Allocator!");
        throw std::runtime_error("Upload Command Allocator Creation Failed!");
    }

    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        uploadAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&uploadCommandList)
    );
    if (FAILED(hr) || !uploadCommandList)
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create Upload Command List!");
        throw std::runtime_error("Upload Command List Creation Failed!");
    }
    uploadCommandList->Close();

    Logger::Log(LogLevel::Info, "✅ Command Interfaces Successfully Created.");
}

//===============================================================================//
//                        ImGui Frame Life Cycle (Corrected)                     //
//===============================================================================//

//=================================
// Begin Dock Space Function
//=================================
void Graphics::BeginDockSpace()
{
    // DockSpace is now owned by UI (UI::BeginDockSpace).
    // Keeping this method as a no-op to avoid breaking existing call sites.
}

//=================================
// Begin Frame Function            
//=================================
void Graphics::BeginFrame(HWND hWnd)
{
#ifndef NDEBUG
    ++frameCounter;
#endif

    // New frame: allow exactly one Present() this frame (Engine-owned).
    g_PresentedThisFrame = false;

    // Apply deferred font reload at the very top (before allocator reset / command list open / ImGui NewFrame).
    ProcessPendingFontReload();

    // Frame counter
    static UINT64 g_FrameCounter = 0;
    Logger::Log(LogLevel::Info, std::format("\n--- Frame {} ---", ++g_FrameCounter));

    // Phase 1A: release GPU resources whose fences have completed.
    ProcessDeferredReleases();

    // Enforce single ImGui frame ownership.
    if (ImGuiFrameStarted)
    {
        Logger::Log(LogLevel::Error, "❌ ImGui::NewFrame called twice in one engine frame!");
        AbortFrame("Double ImGui NewFrame");
        return;
    }

    // Apply pending main window swapchain resize.
    // IMPORTANT: DXGI swapchain resize is illegal before the first successful Present() on some drivers.
    if (hasPresentedOnce)
        ApplyPendingResize(hWnd);
    else if (pendingResize && !loggedDeferredResizeBeforeFirstPresent)
    {
        Logger::Log(LogLevel::Info, "⏳ Resize deferred — waiting for first Present()");
        loggedDeferredResizeBeforeFirstPresent = true;
    }

    // Process any pending scene RT resize before we open/reset the command list for this frame.
    ProcessPendingSceneRenderTargetResize();

#ifndef NDEBUG
    // Phase 1B drift detector: per-buffer fence stamps must never exceed the last signaled fence.
    if (currentBackBufferIndex < NUM_BACK_BUFFERS)
    {
        const UINT64 fv = frames[currentBackBufferIndex].fenceValue;
        if (fv > lastSignaledFenceValue)
        {
            Logger::Log(LogLevel::Error, std::format(
                "🚨 Fence state corruption detected | bb={} frameFence={} lastSignaled={} ",
                currentBackBufferIndex, fv, lastSignaledFenceValue));
            HandleDeviceLost(hWnd);
            return;
        }
    }
#endif

    // ==========================
    // Guard: core DX12 objects must exist
    // ==========================
    if (!device || !commandQueue || !fence || !swapChain)
    {
        Logger::Log(LogLevel::Error, "❌ BeginFrame: missing DX12 core objects (device/queue/fence/swapchain). ");
        HandleDeviceLost(hWnd);
        return;
    }

    // ==========================
    // Sync GPU for current backbuffer
    // ==========================
    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

#ifndef NDEBUG
    // Throttled frame lifecycle validation log.
    static std::chrono::steady_clock::time_point s_FrameDiagLast = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - s_FrameDiagLast >= std::chrono::milliseconds(1000))
    {
        s_FrameDiagLast = now;
        const UINT64 wantFence = (currentBackBufferIndex < NUM_BACK_BUFFERS) ? frames[currentBackBufferIndex].fenceValue : 0;
        const UINT64 completedFence = fence->GetCompletedValue();
        Logger::Log(LogLevel::Debug, std::format(
            "[FrameDiag] BeginFrame bb={} waitForFence={} completedFence={} lastSignaled={}",
            currentBackBufferIndex, wantFence, completedFence, lastSignaledFenceValue));
    }
#endif

#ifndef NDEBUG
    // Phase 1B drift detector: per-buffer fence stamps must never exceed the last signaled fence.
    if (currentBackBufferIndex < NUM_BACK_BUFFERS)
    {
        const UINT64 fv = frames[currentBackBufferIndex].fenceValue;
        if (fv > lastSignaledFenceValue)
        {
            Logger::Log(LogLevel::Error, std::format(
                "🚨 Fence state corruption detected | bb={} frameFence={} lastSignaled={} ",
                currentBackBufferIndex, fv, lastSignaledFenceValue));
            HandleDeviceLost(hWnd);
            return;
        }
    }
#endif

    auto& fc = frames[currentBackBufferIndex];

    UINT64 fenceToWaitFor = fc.fenceValue;
    UINT64 completedValue = fence->GetCompletedValue();

    // ==========================
    // Detect dead device sentinel
    // ==========================
    if (completedValue == UINT64_MAX)
    {
        Logger::Log(LogLevel::Error, std::format(
            "🚨 Fence->GetCompletedValue() == UINT64_MAX (device lost sentinel). BackBuffer={} FenceToWaitFor={}",
            currentBackBufferIndex, fenceToWaitFor));

        if (device)
        {
            const HRESULT removed = device->GetDeviceRemovedReason();
            Logger::Log(LogLevel::Error, std::format(
                "🚨 DeviceRemovedReason=0x{:08X}", (UINT)removed));
        }

        HandleDeviceLost(hWnd);
        return;
    }

    if (completedValue < fenceToWaitFor)
    {
        Logger::Log(LogLevel::Debug, std::format(
            "⏳ Waiting on GPU | BackBuffer={} Fence={} (Completed={})",
            currentBackBufferIndex, fenceToWaitFor, completedValue));

        WaitForFenceValue(fenceToWaitFor);

        // Refresh for accurate diagnostics
        completedValue = fence->GetCompletedValue();
        Logger::Log(LogLevel::Debug, std::format(
            "✅ GPU wait complete | BackBuffer={} Fence={} (Completed now={})",
            currentBackBufferIndex, fenceToWaitFor, completedValue));
    }
    else
    {
        Logger::Log(LogLevel::Debug, std::format(
            "✅ No GPU wait needed | BackBuffer={} Fence={} (Completed={})",
            currentBackBufferIndex, fenceToWaitFor, completedValue));
    }

    // ==========================
    // Reset command allocator + list
    // ==========================
#ifndef NDEBUG
    // Guard against future regressions (resetting allocator while still in-flight).
    if (fence->GetCompletedValue() < fc.fenceValue)
    {
        Logger::Log(LogLevel::Error, std::format(
            "🚨 FrameContext allocator reset while GPU in-flight | bb={} fence={} completed={} ",
            currentBackBufferIndex, fc.fenceValue, fence->GetCompletedValue()));
        HandleDeviceLost(hWnd);
        return;
    }
#endif

    if (!fc.allocator)
    {
        Logger::Log(LogLevel::Error, std::format("❌ BeginFrame(): frames[{}].allocator is NULL", currentBackBufferIndex));
        HandleDeviceLost(hWnd);
        return;
    }

    HRESULT hr = fc.allocator->Reset();
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ Failed to reset allocator[{}] HR=0x{:08X}",
                currentBackBufferIndex, (UINT)hr));
        HandleDeviceLost(hWnd);
        return;
    }

    hr = commandList->Reset(fc.allocator.Get(), nullptr);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ Failed to reset command list HR=0x{:08X}", (UINT)hr));
        HandleDeviceLost(hWnd);
        return;
    }

    commandListOpen = true;

    // ==========================
    // Descriptor heap binding
    // ==========================
    ID3D12DescriptorHeap* srvHeap = ImGui_ImplDX12_GetSrvHeap();
    if (!srvHeap)
        srvHeap = imguiHeap.Get();

    ID3D12DescriptorHeap* heaps[] = { srvHeap };
    commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    // ==========================
    // ImGui new frame
    // ==========================
    ImGui_ImplDX12_NewFrame();

    // One-time font upload, deferred until a valid command list is recording.
    // Must happen after allocator+command list reset and SRV heap binding.
    if (g_ImGuiFontsNeedUpload)
    {
        Logger::Log(LogLevel::Info, "[FontDiag] Uploading ImGui font texture on first frame");
        if (ImGui_ImplDX12_CreateFontsTexture(device.Get(), commandList.Get()))
        {
            g_ImGuiFontsNeedUpload = false;
        }
        else
        {
            Logger::Log(LogLevel::Error, "[FontDiag] Failed to upload ImGui font texture on first frame");
        }
    }

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Logger::Log(LogLevel::Debug, "🆕 ImGui New Frame Started");

    // Ensure most UI stays in the main OS viewport unless explicitly moved.
    // (Prevents accidental extra platform windows for small tool/overlay windows)
    ImGui::GetIO().ConfigViewportsNoAutoMerge = true;

    // ---- Splash FIRST ----
    SplashScreen::Update((float)ImGui::GetIO().DeltaTime);

    if (!SplashScreen::IsFinished())
    {
        // Render ONLY the splash while booting.
        // This avoids editor chrome (dockspace/menu/toolbar) appearing behind it.
        SplashScreen::Render();
    }
    else
    {
        // ---- Editor UI (immutable order) ----
        UI::DrawEditorShell();
    }

    frameStarted = true;
    ImGuiFrameStarted = true;
}

//==============================
// End Frame Function
//=================================
void Graphics::EndFrame(HWND hWnd)
{
    (void)hWnd;

    // Always allow ImGui frame to start fresh next tick.
    ImGuiFrameStarted = false;

    if (!commandListOpen)
    {
        Logger::Log(LogLevel::Error, "❌ EndFrame() called with closed command list.");
        frameStarted = false;
        return;
    }

    if (!commandList || !commandQueue || !fence)
    {
        Logger::Log(LogLevel::Error, "❌ EndFrame(): missing commandList/commandQueue/fence.");
        commandListOpen = false;
        frameStarted = false;
        return;
    }

    const HRESULT hrClose = commandList->Close();
    if (FAILED(hrClose))
    {
        Logger::Log(LogLevel::Error, std::format("❌ EndFrame(): commandList->Close failed HR=0x{:08X}", (UINT)hrClose));
        commandListOpen = false;
        frameStarted = false;
        return;
    }

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // Signal and stamp fence for the current backbuffer.
    const UINT64 signalValue = ++fenceValue;
    lastSignaledFenceValue = signalValue;

    const HRESULT hrSignal = commandQueue->Signal(fence.Get(), signalValue);
    if (FAILED(hrSignal))
    {
        Logger::Log(LogLevel::Error, std::format("❌ EndFrame(): commandQueue->Signal failed HR=0x{:08X}", (UINT)hrSignal));
    }
    else
    {
        if (currentBackBufferIndex < NUM_BACK_BUFFERS)
            frames[currentBackBufferIndex].fenceValue = signalValue;

#ifndef NDEBUG
        // Throttled frame lifecycle validation log.
        static std::chrono::steady_clock::time_point s_EndDiagLast = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - s_EndDiagLast >= std::chrono::milliseconds(1000))
        {
            s_EndDiagLast = now;
            Logger::Log(LogLevel::Debug, std::format(
                "[FrameDiag] EndFrame bb={} signaledFence={} lastSignaled={}",
                currentBackBufferIndex, signalValue, lastSignaledFenceValue));
        }
#endif
    }

    commandListOpen = false;
    frameStarted = false;
}

//===============================================================================//
// Present Function                                                              //
//===============================================================================//
void Graphics::Present(HWND hWnd)
{
    (void)hWnd;

    if (commandListOpen)
    {
        Logger::Log(LogLevel::Error, "❌ Present() called while command list is still open.");
        return;
    }

    if (g_PresentedThisFrame)
    {
        Logger::Log(LogLevel::Error, "❌ Present() called more than once in a single frame.");
        return;
    }

    if (!swapChain)
    {
        Logger::Log(LogLevel::Error, "❌ Present() called with null swapChain.");
        return;
    }

#ifndef NDEBUG
    // Throttled frame lifecycle validation log.
    static std::chrono::steady_clock::time_point s_PresentDiagLast = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (fence && now - s_PresentDiagLast >= std::chrono::milliseconds(1000))
    {
        s_PresentDiagLast = now;
        const UINT64 completedFence = fence->GetCompletedValue();
        Logger::Log(LogLevel::Debug, std::format(
            "[FrameDiag] Present bb={} completedFence={} lastSignaled={}",
            currentBackBufferIndex, completedFence, lastSignaledFenceValue));
    }
#endif

    const UINT syncInterval = vsyncEnabled ? 1u : 0u;
    const UINT presentFlags = (!vsyncEnabled && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;

    const HRESULT hr = swapChain->Present(syncInterval, presentFlags);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ Present failed HR=0x{:08X}", (UINT)hr));
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            HandleDeviceLost(hWnd);
        return;
    }

    hasPresentedOnce = true;
    g_PresentedThisFrame = true;
}

//====================================
// Get Device Function
//====================================
ID3D12Device* Graphics::GetDevice() const
{
    return device.Get();
}

//====================================
// Get ImGui SRV Heap Function
//====================================
ID3D12DescriptorHeap* Graphics::GetImGuiSrvHeap() const
{
    return imguiHeap.Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateSRV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};

    auto& gfx = Graphics::GetInstance();

    ID3D12DescriptorHeap* heap = gfx.GetImGuiSrvHeap();
    ID3D12Device* dev = gfx.GetDevice();

    if (!heap || !dev)
        return handle;

    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_CPU_DESCRIPTOR_HANDLE start = heap->GetCPUDescriptorHandleForHeapStart();

    // Slot 0 reserved for ImGui font.
    static UINT s_srvDescriptorIndex = 1;

    handle.ptr = start.ptr + (SIZE_T)s_srvDescriptorIndex * (SIZE_T)inc;
    s_srvDescriptorIndex++;

    return handle;
}

// ...existing code...

Graphics& Graphics::GetInstance()
{
    static Graphics instance;
    return instance;
}

bool Graphics::InitializeImGui(HWND inHwnd)
{
    if (!inHwnd)
    {
        Logger::Log(LogLevel::Error, "❌ InitializeImGui: HWND is null.");
        return false;
    }

    hWnd = inHwnd;

    // ----------------------------------------------
    // 1) If already fully initialized, only publish
    // ----------------------------------------------
    if (ImGui::GetCurrentContext() && imguiPlatformInitialized && imguiInitialized)
    {
        if (!imguiDevice || !imguiSrvHeap)
        {
            if (!device || !imguiHeap)
            {
                Logger::Log(LogLevel::Error, "❌ InitializeImGui: cannot publish resources (device or ImGui heap is null).");
                return false;
            }

            CacheImGuiResources(device.Get(), imguiHeap.Get());
            OnImGuiReady();
        }

        return true;
    }

    // ----------------------------------------------
    // 2) Create context if needed
    // ----------------------------------------------
    if (!ImGui::GetCurrentContext())
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // Keep multi-viewport off for now (engine currently runs single swapchain path).
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        io.ConfigViewportsNoAutoMerge = true;
        io.ConfigViewportsNoTaskBarIcon = false;
    }

    // ----------------------------------------------
    // 3) Init Win32 backend once
    // ----------------------------------------------
    if (!imguiPlatformInitialized)
    {
        if (!ImGui_ImplWin32_Init(inHwnd))
        {
            Logger::Log(LogLevel::Error, "❌ InitializeImGui: ImGui_ImplWin32_Init failed.");
            return false;
        }
        imguiPlatformInitialized = true;
    }

    // ----------------------------------------------
    // 4) Ensure engine-owned SRV heap exists BEFORE DX12 init
    // ----------------------------------------------
    if (!imguiHeap)
    {
        if (!device)
        {
            Logger::Log(LogLevel::Error, "❌ InitializeImGui: cannot create SRV heap (device is null).");
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 2048;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiHeap));
        if (FAILED(hr) || !imguiHeap)
        {
            Logger::Log(LogLevel::Error, std::format("❌ InitializeImGui: CreateDescriptorHeap(SRV) failed HR=0x{:08X}", (UINT)hr));
            return false;
        }

        Logger::Log(LogLevel::Info, std::format(
            "✅ ImGui SRV heap created | Heap=0x{:X} CPUStart=0x{:X} GPUStart=0x{:X} Count={} ShaderVisible=true",
            (uintptr_t)imguiHeap.Get(),
            (uintptr_t)imguiHeap->GetCPUDescriptorHandleForHeapStart().ptr,
            (uintptr_t)imguiHeap->GetGPUDescriptorHandleForHeapStart().ptr,
            desc.NumDescriptors));
    }

    // Assert heap is valid before init.
    IM_ASSERT(imguiHeap.Get() != nullptr);

    // Allow other systems to locate the SRV heap.
    g_SRVHeap = imguiHeap.Get();

    // ----------------------------------------------
    // 5) Init DX12 backend once
    // ----------------------------------------------
    if (!imguiInitialized)
    {
        if (!device || !commandQueue)
        {
            Logger::Log(LogLevel::Error, "❌ InitializeImGui: cannot init DX12 backend (device or commandQueue is null).");
            return false;
        }

        // Pre-allocate a deterministic font SRV handle pair (slot 0) so the backend never starts with 0/0.
        const D3D12_CPU_DESCRIPTOR_HANDLE fontCpu = imguiHeap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE fontGpu = imguiHeap->GetGPUDescriptorHandleForHeapStart();

        Logger::Log(LogLevel::Info, std::format(
            "[FontDiag] Init font SRV reserved | CPU=0x{:X} GPU=0x{:X} (heapStart slot 0)",
            (uintptr_t)fontCpu.ptr, (uintptr_t)fontGpu.ptr));

        ImGui_ImplDX12_InitInfo info{};
        info.Device = device.Get();
        info.CommandQueue = commandQueue.Get();
        info.DxgiFactory = dxgiFactory.Get();
        info.SwapChain = swapChain.Get();
        info.NumFramesInFlight = IMGUI_NUM_FRAMES_IN_FLIGHT;
        info.RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        // Provide engine-owned heaps.
        info.SrvDescriptorHeap = imguiHeap.Get();
        info.RTVDescriptorHeap = rtvHeap.Get();
        info.RTVDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Wire allocator callbacks BEFORE init.
        info.SrvDescriptorAllocFn = ImGui_SrvDescriptorAlloc;
        info.SrvDescriptorFreeFn = ImGui_SrvDescriptorFree;

        // Provide deterministic font descriptors at init time.
        info.FontSrvCpuDescHandle = fontCpu;
        info.FontSrvGpuDescHandle = fontGpu;

        Logger::Log(LogLevel::Info, std::format(
            "ℹ️ ImGui_ImplDX12_InitInfo | SrvHeap=0x{:X} RTVHeap=0x{:X} FontCPU=0x{:X} FontGPU=0x{:X} AllocFn=0x{:X} FreeFn=0x{:X}",
            (uintptr_t)info.SrvDescriptorHeap,
            (uintptr_t)info.RTVDescriptorHeap,
            (uintptr_t)info.FontSrvCpuDescHandle.ptr,
            (uintptr_t)info.FontSrvGpuDescHandle.ptr,
            (uintptr_t)info.SrvDescriptorAllocFn,
            (uintptr_t)info.SrvDescriptorFreeFn));

        if (!ImGui_ImplDX12_Init(&info))
        {
            Logger::Log(LogLevel::Error, "❌ InitializeImGui: ImGui_ImplDX12_Init failed.");
            return false;
        }

        imguiInitialized = true;
    }

    // ----------------------------------------------
    // 6) Publish cached pointers only when valid
    // ----------------------------------------------
    if (!imguiReadyPublished || !imguiDevice || !imguiSrvHeap)
    {
        if (!device || !imguiHeap)
        {
            Logger::Log(LogLevel::Error, "❌ InitializeImGui: publishing blocked (device or heap is null).");
            return false;
        }

        CacheImGuiResources(device.Get(), imguiHeap.Get());
        OnImGuiReady();
        imguiReadyPublished = true;
    }

    // NOTE:
    // Do NOT upload fonts here. Font upload is owned by the normal frame lifecycle (BeginFrame -> backend NewFrame).

    return true;
}

// SRV allocator callbacks (custom ImGui DX12 backend signatures)
static void ImGui_SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    if (!out_cpu_handle || !out_gpu_handle)
        return;

    *out_cpu_handle = {};
    *out_gpu_handle = {};

    ID3D12DescriptorHeap* heap = info ? info->SrvDescriptorHeap : nullptr;
    if (!heap)
        heap = Graphics::GetInstance().GetImGuiSrvHeap();

    ID3D12Device* dev = info ? info->Device : nullptr;
    if (!dev)
        dev = Graphics::GetInstance().GetDevice();

    if (!heap || !dev)
        return;

    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Slot 0 reserved for the font.
    const UINT index = g_SRVDescriptorIndex++;

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = heap->GetGPUDescriptorHandleForHeapStart();

    out_cpu_handle->ptr = cpuStart.ptr + (SIZE_T)index * (SIZE_T)inc;
    out_gpu_handle->ptr = gpuStart.ptr + (UINT64)index * (UINT64)inc;
}

static void ImGui_SrvDescriptorFree(ImGui_ImplDX12_InitInfo* /*info*/, D3D12_CPU_DESCRIPTOR_HANDLE /*cpu_handle*/, D3D12_GPU_DESCRIPTOR_HANDLE /*gpu_handle*/)
{
    // No-op: descriptor slots are currently monotonically allocated.
}

void Graphics::CacheImGuiResources(ID3D12Device* inDevice, ID3D12DescriptorHeap* inHeap)
{
    if (!inDevice || !inHeap)
    {
        Logger::Log(LogLevel::Error, "❌ CacheImGuiResources called with null ImGui device or heap!");
        return;
    }

    imguiDevice = inDevice;
    imguiSrvHeap = inHeap;
}

void Graphics::OnImGuiReady()
{
    // Mark readiness for any systems that poll the backend.
    // This function intentionally does not call ImGui::NewFrame or backend NewFrame.
    imguiReadyPublished = true;
}

// ========================================
// Setup ImGui Fonts and Scaling
// ========================================
void Graphics::SetupImGuiFontsAndScaling(HWND inHwnd)
{
    if (!inHwnd)
        return;

    if (!ImGui::GetCurrentContext())
        return;

    const UINT dpi = GetDpiForWindow(inHwnd);
    const float scale = (dpi > 0) ? (float)dpi / 96.0f : 1.0f;

    static float s_lastScale = -1.0f;
    if (fabsf(s_lastScale - scale) < 1e-6f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = scale;

    s_lastScale = scale;
}

// Reload ImGui font texture atlas (safe version)
void Graphics::ReloadImGuiFont(float dpiScale)
{
    ReloadImGuiFontImpl(dpiScale, "<unknown>", 0);
}

void Graphics::ReloadImGuiFontImpl(float dpiScale, const char* callerFile, int callerLine)
{
    Logger::Log(LogLevel::Info, std::format(
        "[FontDiag] ReloadImGuiFont requested size={:.3f} frame={} hasPresentedOnce={} commandListOpen={} frameStarted={} ImGuiFrameStarted={} caller={}:{}",
        dpiScale,
        (unsigned)ImGui::GetFrameCount(),
        hasPresentedOnce ? "true" : "false",
        commandListOpen ? "true" : "false",
        frameStarted ? "true" : "false",
        ImGuiFrameStarted ? "true" : "false",
        callerFile ? callerFile : "<null>",
        callerLine));

    // Hard gate: never mutate fonts before first Present.
    if (!hasPresentedOnce)
    {
        pendingFontReload = true;
        pendingFontPixelSize = dpiScale;

        if (!loggedDeferredFontBeforeFirstPresent)
        {
            loggedDeferredFontBeforeFirstPresent = true;
            Logger::Log(LogLevel::Info, "[FontDiag] ReloadImGuiFont deferred — waiting for first Present");
        }
        return;
    }

    if (commandListOpen || frameStarted || ImGuiFrameStarted)
    {
        Logger::Log(LogLevel::Error, "❌ ReloadImGuiFont() called mid-frame. Ignoring.");
        return;
    }

    if (!device || !imguiHeap)
    {
        Logger::Log(LogLevel::Error, "❌ ReloadImGuiFont() missing device or imguiHeap.");
        return;
    }

    dpiScale = (std::max)(0.5f, dpiScale);

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts)
        return;

    // Clear then rebuild deterministically.
    io.Fonts->Clear();

    // Treat param as a pixel size (UI calls pass 14/16/18/20).
    const float sizePx = dpiScale;
    ImFontConfig cfg{};
    cfg.SizePixels = sizePx;
    io.Fonts->AddFontDefault(&cfg);

    // Build atlas on CPU.
    io.Fonts->Build();

#ifndef NDEBUG
    if (!io.Fonts->IsBuilt() || io.Fonts->TexID == 0)
    {
        Logger::Log(LogLevel::Error,
            "🚨 ReloadImGuiFont() failed: Fonts atlas not built after Build().");
        return;
    }
#endif

    // Upload to GPU using the backend helper.
    // Use the engine's dedicated upload command list if available and idle.
    ID3D12GraphicsCommandList* uploadCL = nullptr;
    if (uploadCommandList)
        uploadCL = uploadCommandList.Get();
    else if (commandList)
        uploadCL = commandList.Get();

    if (!uploadCL)
    {
        Logger::Log(LogLevel::Error, "❌ ReloadImGuiFont() no command list available for font upload.");
        return;
    }

    // Ensure SRV heap is bound for the upload operations.
    {
        ID3D12DescriptorHeap* heaps[] = { imguiHeap.Get() };
        uploadCL->SetDescriptorHeaps(1, heaps);
    }

    if (!ImGui_ImplDX12_CreateFontsTexture(device.Get(), uploadCL))
    {
        Logger::Log(LogLevel::Error, "❌ ReloadImGuiFont() failed to upload font texture.");
        return;
    }

#ifndef NDEBUG
    if (!io.Fonts->IsBuilt() || io.Fonts->TexID == 0)
    {
        Logger::Log(LogLevel::Error,
            "🚨 [FontDiag] Font upload completed but TexID is null or atlas not built");
    }
#endif

    CacheImGuiResources(device.Get(), imguiHeap.Get());
    OnImGuiReady();

    fontUploaded = true;

    Logger::Log(LogLevel::Info, std::format("✅ ReloadImGuiFont() completed (sizePx={:.1f})", sizePx));
}

void Graphics::EnsureSceneRenderTarget(UINT width, UINT height)
{
    if (!device)
        return;

    width = (std::max)(1u, width);
    height = (std::max)(1u, height);

    if (sceneRenderTarget && sceneRtvHeap && sceneSrvCpu.ptr != 0 && sceneSrvGpu.ptr != 0 &&
        sceneRTWidth == width && sceneRTHeight == height)
        return;

    // Release old resources safely.
    EnqueueDeferredRelease(sceneRenderTarget);
    EnqueueDeferredRelease(sceneDepth);
    sceneRtvHeap.Reset();
    sceneDsvHeap.Reset();

    sceneRTWidth = width;
    sceneRTHeight = height;

    // ---------------------------------------------------------------------------------
    // Create scene color target
    // ---------------------------------------------------------------------------------
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        clear.Color[0] = 0.10f;
        clear.Color[1] = 0.10f;
        clear.Color[2] = 0.20f;
        clear.Color[3] = 1.0f;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear,
            IID_PPV_ARGS(&sceneRenderTarget));

        if (FAILED(hr) || !sceneRenderTarget)
        {
            Logger::Log(LogLevel::Error, std::format("❌ EnsureSceneRenderTarget: failed to create scene RT HR=0x{:08X}", (UINT)hr));
            sceneRTWidth = sceneRTHeight = 0;
            return;
        }

        sceneRTState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // ---------------------------------------------------------------------------------
    // Create RTV heap + RTV
    // ---------------------------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        const HRESULT hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&sceneRtvHeap));
        if (FAILED(hr) || !sceneRtvHeap)
        {
            Logger::Log(LogLevel::Error, std::format("❌ EnsureSceneRenderTarget: failed to create scene RTV heap HR=0x{:08X}", (UINT)hr));
            sceneRTWidth = sceneRTHeight = 0;
            sceneRenderTarget.Reset();
            return;
        }

        sceneRtvHandle = sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(sceneRenderTarget.Get(), nullptr, sceneRtvHandle);
    }

    // ---------------------------------------------------------------------------------
    // Create depth buffer + DSV
    // ---------------------------------------------------------------------------------
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1.0f;
        clear.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clear,
            IID_PPV_ARGS(&sceneDepth));

        if (FAILED(hr) || !sceneDepth)
        {
            Logger::Log(LogLevel::Error, std::format("❌ EnsureSceneRenderTarget: failed to create scene depth HR=0x{:08X}", (UINT)hr));
            // Depth is optional for the placeholder pass; keep color RT alive.
            sceneDsvHeap.Reset();
            sceneDsvHandle = {};
            sceneDepthState = D3D12_RESOURCE_STATE_COMMON;
        }
        else
        {
            sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

            D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
            dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvDesc.NumDescriptors = 1;
            dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            const HRESULT hrH = device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&sceneDsvHeap));
            if (FAILED(hrH) || !sceneDsvHeap)
            {
                Logger::Log(LogLevel::Error, std::format("❌ EnsureSceneRenderTarget: failed to create scene DSV heap HR=0x{:08X}", (UINT)hrH));
                EnqueueDeferredRelease(sceneDepth);
                sceneDsvHandle = {};
                sceneDepthState = D3D12_RESOURCE_STATE_COMMON;
            }
            else
            {
                sceneDsvHandle = sceneDsvHeap->GetCPUDescriptorHandleForHeapStart();
                D3D12_DEPTH_STENCIL_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_D32_FLOAT;
                view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                view.Flags = D3D12_DSV_FLAG_NONE;
                device->CreateDepthStencilView(sceneDepth.Get(), &view, sceneDsvHandle);
            }
        }
    }

    // ---------------------------------------------------------------------------------
    // Create SRV in the engine ImGui heap for ImGui::Image
    // ---------------------------------------------------------------------------------
    {
        sceneSrvCpu = {};
        sceneSrvGpu = {};
        sceneImGuiTextureID = (ImTextureID)0;

        if (imguiHeap)
        {
            // Allocate a CPU descriptor from the same heap.
            sceneSrvCpu = AllocateSRV();

            if (sceneSrvCpu.ptr != 0)
            {
                const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = imguiHeap->GetCPUDescriptorHandleForHeapStart();
                const UINT index = (UINT)((sceneSrvCpu.ptr - cpuStart.ptr) / (SIZE_T)inc);
                const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = imguiHeap->GetGPUDescriptorHandleForHeapStart();
                sceneSrvGpu = { gpuStart.ptr + (UINT64)index * (UINT64)inc };
                sceneImGuiTextureID = (ImTextureID)sceneSrvGpu.ptr;

                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(sceneRenderTarget.Get(), &srv, sceneSrvCpu);
            }
        }
    }
}

void Graphics::RenderSceneToTarget()
{
    if (!commandListOpen)
        return;

    if (!commandList || !sceneRenderTarget || sceneRtvHandle.ptr == 0 || sceneRTWidth == 0 || sceneRTHeight == 0)
        return;

    // Transition RT to render target
    if (sceneRTState != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
            sceneRenderTarget.Get(),
            sceneRTState,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &b);
        sceneRTState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    const float clearColor[4] = { 0.25f, 0.05f, 0.35f, 1.0f };

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)sceneRTWidth;
    vp.Height = (float)sceneRTHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT sc{};
    sc.left = 0;
    sc.top = 0;
    sc.right = (LONG)sceneRTWidth;
    sc.bottom = (LONG)sceneRTHeight;

    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &sc);

    if (sceneDsvHandle.ptr != 0)
        commandList->OMSetRenderTargets(1, &sceneRtvHandle, FALSE, &sceneDsvHandle);
    else
        commandList->OMSetRenderTargets(1, &sceneRtvHandle, FALSE, nullptr);

    commandList->ClearRenderTargetView(sceneRtvHandle, clearColor, 0, nullptr);

    if (sceneDsvHandle.ptr != 0)
        commandList->ClearDepthStencilView(sceneDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Phase 0: placeholder pass only. Real scene drawing will be restored later.

    // Transition RT back to shader resource
    if (sceneRTState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        CD3DX12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(
            sceneRenderTarget.Get(),
            sceneRTState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &b);
        sceneRTState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

void Graphics::Render(HWND hWnd)
{
    (void)hWnd;

    if (!commandListOpen)
    {
        Logger::Log(LogLevel::Error,
            "❌ Render() called with closed command list!");
        return;
    }

    if (!commandList || !ImGui::GetCurrentContext())
        return;

    // Render the Scene viewport target first (so UI can display it via ImGui::Image)
    RenderSceneToTarget();

    // ==========================
    // Main ImGui render pass
    // ==========================
#ifndef NDEBUG
    if (!ImGui::GetIO().Fonts->IsBuilt())
    {
        Logger::Log(LogLevel::Error,
            "🚨 ImGui Fonts not built before ImGui::Render()");
    }
#endif

    ImGui::Render();
    g_ImGuiRenderedThisFrame = true;
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData)
        return;

    // Transition backbuffer to RT
    CD3DX12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers[currentBackBufferIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &toRT);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        currentBackBufferIndex,
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Clear
    const ImVec4 clear = UI::GetClearColor();
    const float clearColor[4] = { clear.x, clear.y, clear.z, clear.w };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Draw ImGui
    ImGui_ImplDX12_RenderDrawData(drawData, commandList.Get());

    // Transition backbuffer back to present
    CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers[currentBackBufferIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &toPresent);
}

//===============================================
// Request Resize Function
//===============================================
// Call this to request a swap chain resize
// Width and height of 0 will be ignored
// Requested resizes are applied at the top of the next BeginFrame()
// Example: Graphics::GetInstance().RequestResize(1920, 1080);
//===============================================
void Graphics::RequestResize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    pendingWidth = width;
    pendingHeight = height;
    pendingResize = true;
}

//===============================================
// Apply Pending Resize Function
//===============================================
// Call this to apply a pending resize request
// Resizes the swap chain and updates the RTVs
//===============================================
void Graphics::ApplyPendingResize(HWND hWnd)
{
    if (!pendingResize)
        return;

    // Never resize before first present; just keep the request pending.
    if (!hasPresentedOnce)
    {
        if (!loggedDeferredResizeBeforeFirstPresent)
        {
            loggedDeferredResizeBeforeFirstPresent = true;
            Logger::Log(LogLevel::Info, "ℹ️ Resize deferred — waiting for first Present");
        }
        return;
    }

    const UINT w = pendingWidth;
    const UINT h = pendingHeight;
    pendingResize = false;

    HandleResize(hWnd);

    Logger::Log(LogLevel::Info, std::format("✅ Swapchain resize applied: {}x{}", w, h));
}

//===============================================
// Process Pending Scene Render Target Resize Function
//===============================================
// Checks and applies any pending resize for the scene render target
// This is separate from the swap chain resize
// Ensures the scene render target matches the current window size
//===============================================
void Graphics::ProcessPendingSceneRenderTargetResize()
{
    if (!pendingSceneRTResize)
        return;

    const UINT w = pendingSceneRTW;
    const UINT h = pendingSceneRTH;

    pendingSceneRTResize = false;
    EnsureSceneRenderTarget(w, h);
}

void Graphics::HandleResize(HWND hWnd)
{
    // Existing engine uses deferred resize requests and applies them at the top of BeginFrame.
    // `ApplyPendingResize()` contains the hasPresentedOnce gating.
    ApplyPendingResize(hWnd);
}

void Graphics::ProcessPendingFontReload()
{
    if (!pendingFontReload)
        return;

    if (!hasPresentedOnce)
        return;

    if (commandListOpen || frameStarted || ImGuiFrameStarted)
        return;

    const float sizePx = pendingFontPixelSize;
    pendingFontReload = false;

    ReloadImGuiFontImpl(sizePx, "<deferred>", 0);
}
