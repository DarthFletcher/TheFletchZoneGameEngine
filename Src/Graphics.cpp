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

    // Frame counter
    static UINT64 g_FrameCounter = 0;
    Logger::Log(LogLevel::Info, std::format("\n--- Frame {} ---", ++g_FrameCounter));

    // Phase 1A: release GPU resources whose fences have completed.
    ProcessDeferredReleases();

    // Apply pending main window swapchain resize before anything else.
    ApplyPendingResize(hWnd);

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

//===============================================================================//
// Render Function                                                               //
//===============================================================================//
void Graphics::Render(HWND hWnd)
{
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

    // -----------------------------------------------------------------
    // Render health snapshot (throttled)
    // -----------------------------------------------------------------
    {
        static UINT s_renderHealthCounter = 0;
        if ((++s_renderHealthCounter % 60) == 0)
        {
            ImDrawData* dd = ImGui::GetDrawData();
            Logger::Log(LogLevel::Info, std::format(
                "🩺 RenderHealth | cmdListOpen={} cmdList={} ddValid={} cmdLists={} displaySize=({:.1f},{:.1f})",
                commandListOpen ? 1 : 0,
                commandList ? "OK" : "MISS",
                (dd && dd->Valid) ? "true" : "false",
                dd ? dd->CmdListsCount : -1,
                dd ? dd->DisplaySize.x : -1.0f,
                dd ? dd->DisplaySize.y : -1.0f));
        }
    }

    if (!ImGui::GetCurrentContext())
    {
        Logger::Log(LogLevel::Warning,
            "⚠️ Render() skipped — ImGui context missing");
        return;
    }

    // ==========================
    // Main ImGui render pass
    // ==========================
    ImGui::Render();
    g_ImGuiRenderedThisFrame = true;
    ImDrawData* drawData = ImGui::GetDrawData();

    // --- Always transition to RT ---
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

    // Ensure rasterizer state is valid for ImGui (viewport/scissor can otherwise clip everything).
    // IMPORTANT: derive viewport/scissor from the actual backbuffer size to avoid artifacts after resize/maximize.
    {
        UINT bbW = 1;
        UINT bbH = 1;
        if (backBuffers[currentBackBufferIndex])
        {
            const D3D12_RESOURCE_DESC desc = backBuffers[currentBackBufferIndex]->GetDesc();
            bbW = (UINT)(std::max)(1ull, desc.Width);
            bbH = (UINT)(std::max)(1ull, (UINT64)desc.Height);
        }

        D3D12_VIEWPORT vp = {};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = (float)bbW;
        vp.Height = (float)bbH;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        D3D12_RECT scissor = {};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = (LONG)bbW;
        scissor.bottom = (LONG)bbH;

        commandList->RSSetViewports(1, &vp);
        commandList->RSSetScissorRects(1, &scissor);

        // Size mismatch diagnostics (throttled)
        static UINT s_sizeDiagCounter = 0;
        if ((++s_sizeDiagCounter % 120) == 0)
        {
            RECT rc{};
            int cw = -1, ch = -1;
            if (hWnd && GetClientRect(hWnd, &rc))
            {
                cw = rc.right - rc.left;
                ch = rc.bottom - rc.top;
            }

            const float ddW = (drawData && drawData->DisplaySize.x > 0.0f) ? drawData->DisplaySize.x : -1.0f;
            const float ddH = (drawData && drawData->DisplaySize.y > 0.0f) ? drawData->DisplaySize.y : -1.0f;

            Logger::Log(LogLevel::Info, std::format(
                "📐 RT Sizes | BackBuffer={}x{} | Client={}x{} | DrawData={:.1f}x{:.1f} | screen={}x{}",
                bbW, bbH,
                cw, ch,
                ddW, ddH,
                screenWidth, screenHeight));
        }
    }

    // --- Always clear ---
    ImVec4 clear = UI::GetClearColor();
    FLOAT clearColor[4] = { clear.x, clear.y, clear.z, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    if (!drawData || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
    {
        Logger::Log(LogLevel::Trace, "ℹ️ Invalid ImGui draw data — skip render");
    }

    // --- Only draw ImGui if it exists ---
    if (drawData && drawData->CmdListsCount > 0)
    {
        Logger::Log(LogLevel::Debug,
            "🎮 Rendering main ImGui draw data to main swap chain");

        // Ensure ImGui's SRV heap is bound for font/texture sampling.
        ID3D12DescriptorHeap* srvHeap = ImGui_ImplDX12_GetSrvHeap();
        if (!srvHeap)
            srvHeap = imguiHeap.Get();
        if (srvHeap)
        {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        }

        ImGui_ImplDX12_RenderDrawData(drawData, commandList.Get());
    }
    else
    {
        Logger::Log(LogLevel::Trace,
            "ℹ️ Main viewport has no ImGui draw data — clear-only frame");
    }

    // --- Always transition back to PRESENT ---
    CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffers[currentBackBufferIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &toPresent);

    Logger::Log(LogLevel::Info,
        "✅ Main viewport render submitted");

    Logger::Log(LogLevel::Info,
        "📝 Main viewport render recorded");

    // NOTE: Platform windows are updated/rendered in EndFrame() via ImGui::RenderPlatformWindowsDefault().
}

//===============================================================================//
// End Frame Function                                                            //
//===============================================================================//
void Graphics::EndFrame(HWND hWnd)
{
    if (!ImGuiFrameStarted)
    {
        AbortFrame("EndFrame skipped — ImGui frame not started");
        (void)hWnd;
        return;
    }

    if (!frameStarted)
    {
        AbortFrame("EndFrame called without matching BeginFrame()");
        (void)hWnd;
        return;
    }

    if (!commandListOpen)
    {
        AbortFrame("EndFrame called with closed command list");
        (void)hWnd;
        return;
    }

    // EndFrame health (throttled)
    {
        static UINT64 s_endHealthCounter = 0;
        if ((++s_endHealthCounter % 60) == 0)
        {
            Logger::Log(LogLevel::Info, std::format(
                "🩺 EndFrameHealth | frameStarted={} imguiFrameStarted={} renderedThisFrame={} backBufferIndex={} fenceValue={}",
                frameStarted ? 1 : 0,
                ImGuiFrameStarted ? 1 : 0,
                g_ImGuiRenderedThisFrame ? 1 : 0,
                currentBackBufferIndex,
                fenceValue));
        }
    }

    // =========================================================================
    // 1️⃣ CLOSE & EXECUTE MAIN COMMAND LIST
    // =========================================================================
    HRESULT hr = commandList->Close();
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ Failed to close main command list HR=0x{:08X}", (UINT)hr));
        AbortFrame("commandList->Close() failed");
        HandleDeviceLost(hWnd);
        return;
    }

    commandListOpen = false;

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // Single authoritative fence signal for this frame.
    SignalFence();

    Logger::Log(LogLevel::Info,
        "🧠 Main command list executed and fenced");

    // =========================================================================
    // 2️⃣ UPDATE + RENDER PLATFORM WINDOWS
    // =========================================================================
    // Viewports are disabled in Phase 0; keep code path but gated.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        if (g_ImGuiRenderedThisFrame)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();

            ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
            Logger::Log(LogLevel::Info, std::format(
                "📐 Total ImGui Viewports: {}",
                platform_io.Viewports.Size));
        }
        else
        {
            Logger::Log(LogLevel::Warning, "⚠️ Skipping platform windows: ImGui::Render() was not called this frame.");
        }
    }

    // =========================================================================
    // 3️⃣ FINAL FRAME STATE RESET (Present is owned by Engine)
    // =========================================================================
    (void)hWnd;
    frameStarted = false;
    ImGuiFrameStarted = false;
    g_ImGuiRenderedThisFrame = false;

    Logger::Log(LogLevel::Info,
        "✅ EndFrame completed (no Present)");
}

//==============================
// Check Frame Health Function  
//==============================
 void Graphics::CheckFrameHealth() // Checks if core DX12 objects are valid
 {
     if (!commandList || !commandQueue)
         Logger::Log(LogLevel::Error, "❌ Core DX12 objects are invalid!");

     if (!ImGui::GetDrawData() || !ImGui::GetDrawData()->Valid)
         Logger::Log(LogLevel::Warning, "⚠️ ImGui DrawData is invalid or missing.");

     if (!imguiHeap)
         Logger::Log(LogLevel::Error, "❌ ImGui descriptor heap is NULL!");
 }

//==================================
// Rendering Present Frame Function 
//==================================
void Graphics::Present(HWND hWnd)
{
#ifndef NDEBUG
    // Guard rails: Present should happen only after EndFrame closed/submitted the list.
    assert(!commandListOpen && "Present() called while command list is still open (EndFrame not run?)");
    assert(!frameStarted && "Present() called while frameStarted=true (EndFrame not completed?)");
#endif

    // ENGINE RULE:
    // Present must be called exactly once per frame.
    // Only Engine calls this; Graphics must never auto-present.
    if (g_PresentedThisFrame)
    {
        Logger::Log(LogLevel::Error, "❌ Present() called twice in the same frame — blocked.");
        return;
    }

    if (!swapChain || !commandQueue || !fence) {
        Logger::Log(LogLevel::Error, "❌ Missing DX12 resources — SwapChain, CommandQueue, or Fence is null.");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Viewports are disabled in Phase 0, but keep this guard for future re-enable.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        if (!mainViewport || mainViewport->PlatformHandleRaw != hWnd) {
            Logger::Log(LogLevel::Debug, "⏭️ Skipping Present — not the main viewport HWND.");
            return;
        }
    }

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || !drawData->Valid) {
        Logger::Log(LogLevel::Warning, "⚠️ Skipping Present — ImGui draw data is invalid.");
        return;
    }

    // ✅ Do Present
    UINT syncInterval = vsyncEnabled ? 1 : 0;
    UINT presentFlags = (!vsyncEnabled && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = swapChain->Present(syncInterval, presentFlags);

    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, std::format("❌ Present FAILED: HRESULT=0x{:08X}", (UINT)hr));
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Logger::Log(LogLevel::Warning, "⚠️ Device Lost during Present. Attempting recovery.");
            HandleDredDump(device.Get(), "MainWindow_Present");
            HandleDeviceLost(hWnd);
        }
        return;
    }

    g_PresentedThisFrame = true;

    Logger::Log(LogLevel::Debug, std::format(
        "🖼️ Present() | SyncInterval={} Flags={} BackBuffer={} ",
        syncInterval, presentFlags, currentBackBufferIndex));

    // Update back buffer index (DXGI may advance it on present)
    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    // Fence signaling is handled in EndFrame() via SignalFence();

    // Frame pacing log
    static double lastTime = ImGui::GetTime();
    double now = ImGui::GetTime();
    double delta = now - lastTime;
    lastTime = now;

    Logger::Log(LogLevel::Info,
        std::format("✅ Frame Presented. BackBuffer={} | FrameTime={:.2f}ms ({:.1f} FPS)",
            currentBackBufferIndex, delta * 1000.0, (delta > 0.0 ? 1.0 / delta : 0.0)));
}

//==================================================
// Initialize ImGui Function
//==================================================
bool Graphics::InitializeImGui(HWND hWnd)
{
    fontUploaded = false;

    if (imguiInitialized && imguiPlatformInitialized)
    {
        Logger::Log(LogLevel::Info,
            "ℹ️ ImGui already initialized — publishing GPU resources");

        CacheImGuiResources(
            ImGui_ImplDX12_GetDevice(),
            ImGui_ImplDX12_GetSrvHeap()
        );

        OnImGuiReady();
        return true;
    }

    if (!hWnd || !IsWindow(hWnd))
        throw std::runtime_error("❌ Invalid HWND passed to InitializeImGui.");

    if (!device || !commandQueue)
        throw std::runtime_error("❌ Device or CommandQueue is NULL before initializing ImGui.");

    // 🔄 Force shutdown of old ImGui context if active
    if (ImGui::GetCurrentContext())
    {
        ImGuiIO& io = ImGui::GetIO();

        // ✅ Ensure platform windows are destroyed first
        ImGui::DestroyPlatformWindows();

        if (io.BackendRendererUserData)
        {
            Logger::Log(LogLevel::Warning, "⚠️ Renderer backend still active. Forcing ImGui_ImplDX12_Shutdown...");
            ImGui_ImplDX12_Shutdown();
            io.BackendRendererUserData = nullptr;
        }

        if (io.BackendPlatformUserData)
        {
            Logger::Log(LogLevel::Warning, "⚠️ Platform backend still active. Forcing ImGui_ImplWin32_Shutdown...");
            ImGui_ImplWin32_Shutdown();
            io.BackendPlatformUserData = nullptr;
        }

        Logger::Log(LogLevel::Info, "🧠 Destroying previous ImGui context.");
        ImGui::DestroyContext();
    }

    // 🔧 Create new ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().Alpha = 1.0f;

    PostImGuiInitFixes();
    SanitizeImGuiStyleAlpha();

    Logger::Log(LogLevel::Info, "✅ ImGui context created.");

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Phase 0 stability: disable viewports (single swapchain/present/fence path)
    io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    io.ConfigViewportsNoAutoMerge = true;
    io.ConfigViewportsNoTaskBarIcon = false; // Optional: Keep window icon in taskbar

    Logger::Log(LogLevel::Info, std::format("📌 Win32 Backend Init HWND = 0x{:X}", reinterpret_cast<uintptr_t>(hWnd)));

    // 🧱 Initialize Win32 Platform Backend
    if (!imguiPlatformInitialized)
    {
        if (!ImGui_ImplWin32_Init(hWnd))
            throw std::runtime_error("❌ ImGui_ImplWin32_Init failed.");

        Logger::Log(LogLevel::Info, std::format("📌 Win32 Backend Init HWND = 0x{:X}", reinterpret_cast<uintptr_t>(hWnd)));
        Logger::Log(LogLevel::Info, std::format("🔍 BackendPlatformUserData: {}", ImGui::GetIO().BackendPlatformUserData ? "VALID" : "NULL"));

        ImGuiPlatformIO& base_io = ImGui::GetPlatformIO();
        ImGuiPlatformIO_Extended& platform_io = *(ImGuiPlatformIO_Extended*)&base_io;

        platform_io.Platform_CreateWindow = ImGui_ImplWin32_CreateWindow;
        platform_io.Platform_DestroyWindow = ImGui_ImplWin32_DestroyWindow;
        platform_io.Platform_ShowWindow = ImGui_ImplWin32_ShowWindow;
        platform_io.Platform_SetWindowPos = ImGui_ImplWin32_SetWindowPos;
        platform_io.Platform_GetWindowPos = ImGui_ImplWin32_GetWindowPos;
        platform_io.Platform_SetWindowSize = ImGui_ImplWin32_SetWindowSize;
        platform_io.Platform_GetWindowSize = ImGui_ImplWin32_GetWindowSize;
        platform_io.Platform_SetWindowFocus = ImGui_ImplWin32_SetWindowFocus;
        platform_io.Platform_GetWindowFocus = ImGui_ImplWin32_GetWindowFocus;
        platform_io.Platform_GetWindowMinimized = ImGui_ImplWin32_GetWindowMinimized;
        platform_io.Platform_SetWindowTitle = ImGui_ImplWin32_SetWindowTitle;
        platform_io.Platform_SetWindowAlpha = ImGui_ImplWin32_SetWindowAlpha;
        platform_io.Platform_UpdateWindow = ImGui_ImplWin32_UpdateWindow;
        platform_io.Platform_GetWindowDpiScale = ImGui_ImplWin32_GetWindowDpiScale;
        platform_io.Platform_OnChangedViewport = ImGui_ImplWin32_OnChangedViewport;
        platform_io.Platform_RenderWindow = ImGui_ImplWin32_RenderWindow;

        imguiPlatformInitialized = true;
        Logger::Log(LogLevel::Info, "✅ ImGui Win32 platform backend initialized.");
    }

    // 🧱 Initialize DX12 Renderer Backend
    if (!imguiInitialized)
    {
        // ✅ Create GPU descriptor heap for ImGui
        if (!imguiHeap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 64; // Allocate more descriptors for safety
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiHeap)));

            if (rtvHeap)
            {
                Logger::Log(LogLevel::Debug, std::format(
                    "[Engine RTV Heap] StartCPU=0x{:X} | Count={}",
                    static_cast<uintptr_t>(rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr),
                    Graphics::frameCount // Use the static frameCount variable from Graphics class
                ));
            }

            // ✅ Publish SRV heap for texture allocations
            g_SRVHeap = imguiHeap.Get();

            Logger::Log(LogLevel::Info, "✅ ImGui GPU descriptor heap created.");
        }

        // ✅ FIX: Assign CPU + GPU handles for the font SRV
        imguiFontCPU = imguiHeap->GetCPUDescriptorHandleForHeapStart();
        imguiFontGPU = imguiHeap->GetGPUDescriptorHandleForHeapStart();

        Logger::Log(LogLevel::Debug, std::format("🔗 Font SRV CPU handle = 0x{:X}", imguiFontCPU.ptr));
        Logger::Log(LogLevel::Debug, std::format("🔗 Font SRV GPU handle = 0x{:X}", imguiFontGPU.ptr));

        // ✅ Set up ImGui DX12 Init Info
        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = device.Get();
        initInfo.CommandQueue = commandQueue.Get();
        initInfo.NumFramesInFlight = IMGUI_NUM_FRAMES_IN_FLIGHT;

        // Formats / heaps expected by this repo's custom backend
        initInfo.RenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.SrvDescriptorHeap = imguiHeap.Get();
        initInfo.RTVDescriptorHeap = rtvHeap.Get();
        initInfo.RTVDescriptorSize = rtvDescriptorSize;

        // Optional but used by the custom backend for main viewport correctness
        initInfo.DxgiFactory = dxgiFactory.Get();
        initInfo.SwapChain = swapChain.Get();

        // If we already allocated the font SRV, pass it through so the backend doesn't guess.
        initInfo.FontSrvCpuDescHandle = imguiFontCPU;
        initInfo.FontSrvGpuDescHandle = imguiFontGPU;

        // Phase 0: keep viewport rendering disabled; tearing/vsync still applies to main present.
        initInfo.VsyncEnabled = vsyncEnabled;
        initInfo.AllowTearing = allowTearing;

        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            Logger::Log(LogLevel::Error, "❌ ImGui_ImplDX12_Init failed.");
            return false;
        }

        // Upload the font texture now that we have valid SRV handles (use dedicated upload list)
        if (!uploadAllocator || !uploadCommandList)
        {
            Logger::Log(LogLevel::Error, "❌ Upload context missing (uploadAllocator/uploadCommandList).");
            return false;
        }

        DX_CHECK(uploadAllocator->Reset());
        DX_CHECK(uploadCommandList->Reset(uploadAllocator.Get(), nullptr));

        ImGui_ImplDX12_CreateFontsTexture(device.Get(), uploadCommandList.Get());
        
        DX_CHECK(uploadCommandList->Close());
        ID3D12CommandList* uploadLists[] = { uploadCommandList.Get() };
        commandQueue->ExecuteCommandLists(1, uploadLists);

        FlushGPU(); // Ensure font is uploaded successfully
        Logger::Log(LogLevel::Info, "📦 ImGui font texture uploaded successfully.");

        ImGuiPlatformIO& base_io = ImGui::GetPlatformIO();
        ImGuiPlatformIO_Extended& platform_io = *(ImGuiPlatformIO_Extended*)&base_io;

        platform_io.Renderer_CreateWindow = ImGui_ImplDX12_CreateWindow;
        platform_io.Renderer_DestroyWindow = ImGui_ImplDX12_DestroyWindow;
        platform_io.Renderer_SetWindowSize = ImGui_ImplDX12_SetWindowSize;
        platform_io.Renderer_RenderWindow = ImGui_ImplDX12_RenderWindow;
        platform_io.Renderer_SwapBuffers = ImGui_ImplDX12_SwapBuffers;
        platform_io.Renderer_GetWindowFocus = ImGui_ImplDX12_GetWindowFocus;
        platform_io.Renderer_GetWindowMinimized = ImGui_ImplDX12_GetWindowMinimized;
        platform_io.Renderer_UpdateWindow = ImGui_ImplDX12_UpdateWindow;

        ImGuiStyle& style = ImGui::GetStyle();
        style.Alpha = 1.0f;
        IM_ASSERT(style.Alpha >= 0.0f && style.Alpha <= 1.0f);

        imguiInitialized = true;

        CacheImGuiResources(
            ImGui_ImplDX12_GetDevice(),
            ImGui_ImplDX12_GetSrvHeap()
        );

        OnImGuiReady();

        Logger::Log(LogLevel::Info, "✅ ImGui DX12 Backend Initialized (Published)");
        return true;
    }

    // -----------------------------------------------------------
    // STEP 3 — Hook ImGui PlatformIO Callbacks for Diagnostics
    // -----------------------------------------------------------
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    // Handle viewport creation
    platform_io.Renderer_CreateWindow = [](ImGuiViewport* vp)
        {
            ImGui_ImplDX12_CreateWindow(vp);
            Logger::Log(LogLevel::Info, std::format(
                "🪟 Viewport CREATED | ID=0x{:X} HWND=0x{:X}",
                vp->ID,
                reinterpret_cast<uintptr_t>(vp->PlatformHandle)));
        };

    // Handle per-viewport rendering
    platform_io.Renderer_RenderWindow = [](ImGuiViewport* vp, void*)
        {
            auto* vd = static_cast<ImGui_ImplDX12_ViewportData*>(vp->RendererUserData);
            if (!vd)
            {
                Logger::Log(LogLevel::Error, std::format(
                    "❌ Renderer_RenderWindow called with NULL RendererUserData! ViewportID=0x{:X}", vp->ID));
                return;
            }

            auto start = std::chrono::high_resolution_clock::now();
            ImGui_ImplDX12_RenderWindow(vp, nullptr);
            auto end = std::chrono::high_resolution_clock::now();

            vd->LastRenderDurationNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            vd->FrameCounter++;

            Logger::Log(LogLevel::Debug, std::format(
                "🖼️ Viewport RENDERED | ID=0x{:X} | Frame={} | Duration={}ns",
                vp->ID, vd->FrameCounter, vd->LastRenderDurationNs));
        };

    // Handle viewport destruction
    platform_io.Renderer_DestroyWindow = [](ImGuiViewport* vp)
        {
            Logger::Log(LogLevel::Info, std::format(
                "🗑️ Destroying viewport | ID=0x{:X} HWND=0x{:X}",
                vp->ID,
                reinterpret_cast<uintptr_t>(vp->PlatformHandle)));

            ImGui_ImplDX12_DestroyWindow(vp);

            Logger::Log(LogLevel::Info, std::format(
                "✅ Viewport DESTROYED | ID=0x{:X}", vp->ID));
        };

    Logger::Log(LogLevel::Info, "✅ ImGui initialization complete.");

    PostImGuiInitFixes();

    // FIX: Ensure all control paths return a value
    return imguiInitialized && imguiPlatformInitialized;
}

// ------------------------------------------------------------------
// Static helpers for descriptor allocation (used in InitInfo struct)
// ------------------------------------------------------------------
static void ImGui_SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    if (!info || !info->SrvDescriptorHeap || !out_cpu_handle || !out_gpu_handle)
    {
        Logger::Log(LogLevel::Error, "❌ SrvDescriptorHeap is NULL in descriptor alloc callback.");
        return;
    }

    ID3D12Device* dev = info->Device;
    if (!dev)
    {
        Logger::Log(LogLevel::Error, "❌ Device is NULL in descriptor alloc callback.");
        return;
    }

    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Slot 0 is reserved for the ImGui font SRV.
    if (g_SRVDescriptorIndex == 0)
        g_SRVDescriptorIndex = 1;

    const D3D12_DESCRIPTOR_HEAP_DESC heapDesc = info->SrvDescriptorHeap->GetDesc();
    if (heapDesc.NumDescriptors == 0)
    {
        Logger::Log(LogLevel::Error, "❌ SRV heap has 0 descriptors in descriptor alloc callback.");
        *out_cpu_handle = {};
        *out_gpu_handle = {};
        return;
    }

    // Prevent overflow. If we hand out an out-of-range descriptor, ImGui may sample garbage (e.g. font atlas)
    // or hit DEVICE_HUNG. Keep it strict.
    if (g_SRVDescriptorIndex >= heapDesc.NumDescriptors)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ SRV heap exhausted: requested index {} but heap has {} descriptors. Increase imgui heap size.",
            g_SRVDescriptorIndex, heapDesc.NumDescriptors));
        *out_cpu_handle = {};
        *out_gpu_handle = {};
        return;
    }

    const UINT index = g_SRVDescriptorIndex++;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)index * (SIZE_T)inc;
    gpu.ptr += (UINT64)index * (UINT64)inc;

    *out_cpu_handle = cpu;
    *out_gpu_handle = gpu;
}

static void ImGui_SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
{
    // 🔄 Add pooling or reuse logic here later if needed
}

//==================================================
// Cache ImGui Resources Function
//==================================================
void Graphics::CacheImGuiResources(ID3D12Device* inDevice, ID3D12DescriptorHeap* inHeap)
{
    if (!inDevice || !inHeap)
    {
        Logger::Log(LogLevel::Error,
            "❌ CacheImGuiResources called with null ImGui device or heap!");
        return;
    }

    this->imguiDevice = inDevice;   // non-owning
    this->imguiSrvHeap = inHeap;    // non-owning

    // Keep the SRV allocator targeting ImGui's heap.
    g_SRVHeap = inHeap;

    Logger::Log(LogLevel::Info, "✅ Cached ImGui GPU resources (non-owning)");
    Logger::Log(LogLevel::Info,
        std::string("🧪 Cached ImGui refs | imguiDevice=") + std::to_string((uintptr_t)this->imguiDevice) +
        std::string(" imguiSrvHeap=") + std::to_string((uintptr_t)this->imguiSrvHeap));
}

//==================================================
// On ImGui Ready Event Function
//==================================================
void Graphics::OnImGuiReady()
{
    if (imguiReadyPublished)
        return;

    imguiReadyPublished = true;
    Logger::Log(LogLevel::Info, "✅ Graphics::OnImGuiReady — GPU resources published");
}

// ===========================================================
// Safe Release Resource Function
// ===========================================================
void Graphics::SafeReleaseResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, bool logNullRelease)
{
    if (resource)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(resource.Get());
        Logger::Log(LogLevel::Info, std::format("🔄 [Resource] Releasing GPU Resource at 0x{:X}", address));
        resource.Reset();
        Logger::Log(LogLevel::Info, "✅ [Resource] GPU Resource Released Successfully.");
    }
    else if (logNullRelease)
    {
        Logger::Log(LogLevel::Debug, "ℹ️ [Resource] SafeReleaseResource: NULL pointer detected (already released or never created).");
    }
}

// ===========================================================
// Recover From Fence Error Function
// ===========================================================
void Graphics::RecoverFromFenceError()
{
    Logger::Log(LogLevel::Warning, "⚠️ WARNING: Fence error detected. Recreating fence...");

    if (!device)
        throw std::runtime_error("Device is null in RecoverFromFenceError");

    fence.Reset();

    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to recreate Fence! HRESULT: " + std::to_string(hr));
        throw std::runtime_error("Fence recreation failed!");
    }

    fenceValue = 1;
    if (commandQueue)
        commandQueue->Signal(fence.Get(), fenceValue);

    Logger::Log(LogLevel::Info, "✅ Fence reinitialized successfully. Resetting GPU sync...");
}

//==================================================
// Get Instance Function (Singleton Pattern)
//==================================================
Graphics& Graphics::GetInstance()
{
    static Graphics instance;

    static bool logged = false;
    if (!logged)
    {
        Logger::Log(LogLevel::Info,
            std::format("🧠 Graphics singleton instance @ {}", (void*)&instance));
        logged = true;
    }

    return instance;
}

void Graphics::OnResize(HWND hWnd, UINT width, UINT height)
{
    (void)hWnd;
    screenWidth = width;
    screenHeight = height;
    Logger::Log(LogLevel::Info, std::format(
        "📐 Graphics OnResize called: New Size = {}x{}",
		screenWidth, screenHeight));
}

//==================================================
// Get Device Function
//==================================================
ID3D12Device* Graphics::GetDevice() const
{
    return device.Get();
}

//==================================================
// Get ImGui Device Function
//==================================================
ID3D12Device* Graphics::GetImGuiDevice() const
{
    return this->imguiDevice;
}

//==================================================
// Get ImGui SRV Heap Function
//==================================================
ID3D12DescriptorHeap* Graphics::GetImGuiSrvHeap() const
{
    return this->imguiSrvHeap;
}

//==================================================
// Allocate SRV Function
//==================================================
D3D12_CPU_DESCRIPTOR_HANDLE Graphics::AllocateSRV()
{
    if (!g_SRVHeap)
    {
        Logger::Log(LogLevel::Error, "❌ g_SRVHeap is NULL in AllocateSRV.");
        return {};
    }

    // Prefer cached ImGui device pointer (set in CacheImGuiResources)
    ID3D12Device* dev = Graphics::GetInstance().GetImGuiDevice();
    if (!dev)
        dev = Graphics::GetInstance().GetDevice();

    if (!dev)
    {
        Logger::Log(LogLevel::Error, "❌ Device is NULL in AllocateSRV.");
        return {};
    }

    const UINT descriptorSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Slot 0 is reserved for the ImGui font SRV.
    if (g_SRVDescriptorIndex == 0)
        g_SRVDescriptorIndex = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE handle = g_SRVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(g_SRVDescriptorIndex) * static_cast<SIZE_T>(descriptorSize);
    ++g_SRVDescriptorIndex;
    return handle;
}

//==================================================
// Post ImGui Init Fixes Function
//==================================================
void Graphics::PostImGuiInitFixes()
{
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::GetMainViewport())
        ImGui::GetMainViewport()->DpiScale = 1.0f;

    if (!(io.FontGlobalScale > 0.0f && io.FontGlobalScale < 99.0f))
    {
        Logger::Log(LogLevel::Warning, "⚠️ ImGui FontGlobalScale out of bounds, resetting to 1.0f");
        io.FontGlobalScale = 1.0f;
    }
    Logger::Log(LogLevel::Info, "✅ ImGui DPI scale and font scale verified.");
}

//==================================================
// Setup ImGui Fonts and Scaling Function
//==================================================
void Graphics::SetupImGuiFontsAndScaling(HWND hWnd)
{
    if (!ImGui::GetCurrentContext())
    {
        Logger::Log(LogLevel::Error, "❌ Cannot scale ImGui: no active context.");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96, dpiY = 96;
    float scale = 1.0f;

    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0)
        scale = std::clamp(static_cast<float>(dpiX) / 96.0f, 0.5f, 2.0f);

    // Rounding might be needed if the monitor DPI is exotic (not a whole multiple of 96).
    scale = std::round(scale * 10.0f) / 10.0f;

    // IMPORTANT: avoid accumulating scaling. This function can be called multiple times.
    // Reset style to a known baseline before applying scaled sizes.
    ImGui::StyleColorsDark();

    io.Fonts->Clear();
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 16.0f * scale;
    io.Fonts->AddFontDefault(&fontCfg);

    // Merge Font Awesome (optional). If missing, we keep running and fall back to text labels.
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        static const ImWchar ranges[] = { 0xf000, 0xf8ff, 0 };
        const float iconSize = fontCfg.SizePixels;
        io.Fonts->AddFontFromFileTTF("Assets/Fonts/fa-solid-900.ttf", iconSize, &config, ranges);
    }

    // Render in pixel coords; keep framebuffer scale at 1 to avoid "zoom/crop".
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.Alpha = std::clamp(style.Alpha, 0.0f, 1.0f);

    io.FontGlobalScale = 1.0f;

    io.Fonts->Build();

    Logger::Log(LogLevel::Info, std::format("🖋️ ImGui scale set to {:.2f}, font size {:.1f}px", scale, fontCfg.SizePixels));
    Logger::Log(LogLevel::Debug, "🔍 ImGui DisplayFramebufferScale.x: " + std::to_string(io.DisplayFramebufferScale.x));
}

//==================================================
// Reload ImGui Font
//==================================================
void Graphics::ReloadImGuiFont(float fontSize)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Locked)
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ Font atlas is locked. Deferring reload to next frame (size: {:.1f})", fontSize));
        pendingFontSizeReload = fontSize;
        return;
    }

    io.Fonts->Clear();

    ImFontConfig config;
    config.SizePixels = fontSize;

    io.Fonts->AddFontDefault(&config);
    io.Fonts->Build();

    // Merge Font Awesome (optional). If missing, we keep running and fall back to text labels.
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        static const ImWchar ranges[] = { 0xf000, 0xf8ff, 0 };
        const float iconSize = fontSize;
        io.Fonts->AddFontFromFileTTF("Assets/Fonts/fa-solid-900.ttf", iconSize, &config, ranges);
    }

    Logger::Log(LogLevel::Info, std::format("🔁 Font reloaded immediately (size: {:.1f}px)", fontSize));

    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = std::clamp(style.Alpha, 0.0f, 1.0f);
}

//==================================================
// Create Render Target Views Function
//==================================================
void Graphics::CreateRenderTargetViews()
{
    Logger::Log(LogLevel::Info, "Creating Render Target Views...");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 64;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    DX_CHECK(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
    rtvHeap->SetName(L"MainRTVHeap");

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Logger::Log(LogLevel::Info, std::format("📏 RTV Descriptor Size: {}", rtvDescriptorSize));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        DX_CHECK(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])));
        device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

    Logger::Log(LogLevel::Info, std::format("🔗 Stored RTV Descriptor Heap: 0x{:X}", reinterpret_cast<size_t>(rtvHeap.Get())));
}

//==================================================
// Offscreen Scene Panel Render Target
//==================================================
void Graphics::EnsureSceneRenderTarget(UINT width, UINT height)
{
    if (!device || !imguiHeap)
        return;

    // If we need to resize while recording, defer (don't Flush/Reset mid-list).
    if (commandListOpen)
    {
        width = (std::max)(1u, width);
        height = (std::max)(1u, height);
        pendingSceneRTW = width;
        pendingSceneRTH = height;
        pendingSceneRTResize = true;
        return;
    }

    if (sceneRenderTarget && sceneRTWidth == width && sceneRTHeight == height)
        return;

    // If resizing/recreating, ensure GPU is idle (keeps this simple and safe).
    FlushGPU();

    // Phase 1A: enqueue old resources so release is safe on future non-flush paths.
    EnqueueDeferredRelease(sceneRenderTarget);
    EnqueueDeferredRelease(sceneDepth);
    EnqueueDeferredRelease(sceneTriangleVB);
    EnqueueDeferredRelease(sceneGridVB);
    EnqueueDeferredRelease(sceneAxesVB);
    EnqueueDeferredRelease(sceneCB);
    EnqueueDeferredRelease(sceneTrianglePSO);
    EnqueueDeferredRelease(sceneGridPSO);
    EnqueueDeferredRelease(sceneRootSignature);

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

    // Create the offscreen texture (render target)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = texDesc.Format;
    clearValue.Color[0] = 0.10f;
    clearValue.Color[1] = 0.10f;
    clearValue.Color[2] = 0.12f;
    clearValue.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        IID_PPV_ARGS(&sceneRenderTarget));

    if (FAILED(hr) || !sceneRenderTarget)
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to create Scene render target. HR=0x{:08X}", (UINT)hr));
        return;
    }

    sceneRenderTarget->SetName(L"SceneRenderTarget");

    // RTV heap for the scene render target (CPU-only)
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&sceneRtvHeap));
    if (FAILED(hr) || !sceneRtvHeap)
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to create Scene RTV heap. HR=0x{:08X}", (UINT)hr));
        sceneRenderTarget.Reset();
        return;
    }
    sceneRtvHeap->SetName(L"SceneRTVHeap");

    sceneRtvHandle = sceneRtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(sceneRenderTarget.Get(), nullptr, sceneRtvHandle);

    // SRV descriptor in ImGui heap (shader-visible)
    // NOTE: Allocate the SRV slot once and reuse it across resizes to avoid descriptor exhaustion.
    ID3D12Device* dev = GetImGuiDevice();
    if (!dev)
        dev = device.Get();

    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (sceneSrvCpu.ptr == 0)
    {
        sceneSrvCpu = Graphics::AllocateSRV();
        if (sceneSrvCpu.ptr == 0)
        {
            Logger::Log(LogLevel::Error, "❌ Failed to allocate SRV for Scene render target (sceneSrvCpu=0)");
            return;
        }

        // Compute GPU handle for the same slot used by AllocateSRV().
        // AllocateSRV increments g_SRVDescriptorIndex after assigning; so current slot is (g_SRVDescriptorIndex - 1).
        sceneSrvGpu = imguiHeap->GetGPUDescriptorHandleForHeapStart();
        sceneSrvGpu.ptr += (UINT64)(g_SRVDescriptorIndex - 1) * (UINT64)inc;

        // ImGui expects ImTextureID to reference a GPU descriptor handle (DX12 backend convention).
        sceneImGuiTextureID = (ImTextureID)sceneSrvGpu.ptr;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(sceneRenderTarget.Get(), &srvDesc, sceneSrvCpu);

    // ---- Scene depth buffer (D32_FLOAT) ----
    {
        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Alignment = 0;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthClear = {};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;
        depthClear.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES depthHeap(D3D12_HEAP_TYPE_DEFAULT);
        DX_CHECK(device->CreateCommittedResource(
            &depthHeap,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthClear,
            IID_PPV_ARGS(&sceneDepth)));

        sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DX_CHECK(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&sceneDsvHeap)));

        sceneDsvHandle = sceneDsvHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = 0;

        device->CreateDepthStencilView(sceneDepth.Get(), &dsvDesc, sceneDsvHandle);
    }

    sceneRTWidth = width;
    sceneRTHeight = height;

    // When recreating or creating the scene SRV, log the CPU/GPU descriptor handles.
    // (This verifies the descriptor slot ImGui will sample from.)
    if (SceneDiag_ShouldLog())
    {
        Logger::Log(LogLevel::Debug, std::format(
            "[SceneDiag] EnsureSceneRenderTarget: Scene SRV handles cpu=0x{:X} gpu=0x{:X} rt={}x{}",
            (UINT64)sceneSrvCpu.ptr, (UINT64)sceneSrvGpu.ptr, sceneRTWidth, sceneRTHeight));
    }
}

void Graphics::EnsureScenePipeline()
{
    // Create root signature if missing
    if (!sceneRootSignature)
    {
        if (!device)
            return;

        // Root signature: slot 0 = CBV(b0) visible to VS/PS.
        CD3DX12_ROOT_PARAMETER rp[1]{};
        rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.Init(_countof(rp), rp, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Microsoft::WRL::ComPtr<ID3DBlob> sig;
        Microsoft::WRL::ComPtr<ID3DBlob> err;
        HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            sig.GetAddressOf(), err.GetAddressOf());
        if (FAILED(hr))
        {
            if (err)
            {
                const char* msg = (const char*)err->GetBufferPointer();
                Logger::Log(LogLevel::Error, std::format("❌ Scene root signature serialize failed: {}", msg ? msg : "<no message>"));
            }
            else
            {
                Logger::Log(LogLevel::Error, std::format("❌ Scene root signature serialize failed. HR=0x{:08X}", (UINT)hr));
            }
            return;
        }

        DX_CHECK(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&sceneRootSignature)));
        sceneRootSignature->SetName(L"SceneRootSignature");
    }

    if (sceneTrianglePSO && sceneGridPSO && sceneCB)
    {
        // Ensure our CB is mapped even if PSOs already exist.
        if (sceneCB && sceneCBPtr == nullptr)
        {
            CD3DX12_RANGE range(0, 0);
            void* p = nullptr;
            HRESULT hr = sceneCB->Map(0, &range, &p);
            if (SUCCEEDED(hr))
                sceneCBPtr = p;
        }
        return;
    }

    if (!device)
        return;

    // Compile shaders at runtime (so we don't depend on vcxproj FxCompile)
    // Use SM5.0 for widest compatibility with FXC/D3DCompile toolchains.
    auto triVS = CompileShaderFromRelativeFile(L"shaders\\scene_triangle_vs.hlsl", "main", "vs_5_0");
    auto triPS = CompileShaderFromRelativeFile(L"shaders\\scene_triangle_ps.hlsl", "main", "ps_5_0");
    auto gridVS = CompileShaderFromRelativeFile(L"shaders\\scene_grid_vs.hlsl", "main", "vs_5_0");
    auto gridPS = CompileShaderFromRelativeFile(L"shaders\\scene_grid_ps.hlsl", "main", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = sceneRootSignature.Get();
    pso.InputLayout = { layout, _countof(layout) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.VS = { triVS->GetBufferPointer(), triVS->GetBufferSize() };
    pso.PS = { triPS->GetBufferPointer(), triPS->GetBufferSize() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;

    if (!sceneTrianglePSO)
        DX_CHECK(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sceneTrianglePSO)));

    // Grid PSO: line list + alpha blending
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pso.VS = { gridVS->GetBufferPointer(), gridVS->GetBufferSize() };
    pso.PS = { gridPS->GetBufferPointer(), gridPS->GetBufferSize() };

    // Alpha blending for grid
    D3D12_BLEND_DESC bs = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    bs.RenderTarget[0].BlendEnable = TRUE;
    bs.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    bs.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    bs.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    bs.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    bs.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    bs.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.BlendState = bs;

    if (!sceneGridPSO)
        DX_CHECK(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&sceneGridPSO)));

    // Constant buffer (persistently mapped)
    if (!sceneCB)
    {
        const UINT cbSize = (UINT)((sizeof(SceneCBData) + 255) & ~255u);
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC buf = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
        DX_CHECK(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &buf,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&sceneCB)));
        sceneCB->SetName(L"SceneCB");
    }

    if (sceneCB && sceneCBPtr == nullptr)
    {
        CD3DX12_RANGE range(0, 0);
        void* p = nullptr;
        HRESULT hr = sceneCB->Map(0, &range, &p);
        if (SUCCEEDED(hr))
        {
            sceneCBPtr = p;
        }
        else
        {
            Logger::Log(LogLevel::Error, std::format("❌ Failed to map SceneCB. HR=0x{:08X}", (UINT)hr));
            sceneCBPtr = nullptr;
        }
    }
}

void Graphics::EnsureSceneGeometry()
{
    // Triangle VB is mode-independent, grid/axes depend on view mode.
    // IMPORTANT:
    // ViewMode switches (3D <-> 2D) must NOT rebuild geometry while the main command list is recording.
    // Releasing/recreating VBs while the GPU may still reference them can lead to DEVICE_HUNG.
    const ViewMode vm = GetViewMode();
    const bool viewModeChanged = (g_LastSceneGridBuiltViewMode != vm);

    if (viewModeChanged && commandListOpen)
    {
        // Defer rebuild to a safe point (next frame, before recording starts).
        static ViewMode s_lastDeferredLogged = ViewMode::Mode3D;
        if (s_lastDeferredLogged != vm)
        {
            s_lastDeferredLogged = vm;
            Logger::Log(LogLevel::Info, "🔄 ViewMode changed → deferring scene geometry rebuild until safe point");
        }
        return;
    }

#ifndef NDEBUG
    // If we are about to rebuild (view mode changed), we should never be doing it while recording.
    if (viewModeChanged)
        assert(!commandListOpen && "Scene geometry rebuild attempted during active command list");
#endif

    if (sceneTriangleVB && sceneGridVB && sceneAxesVB && !viewModeChanged)
        return;

    if (!device)
        return;

    // On a view-mode swap, keep it conservative and ensure the GPU is idle before rebuilding.
    if (viewModeChanged)
        FlushGPU();

    if (!sceneTriangleVB)
    {
        SceneVertex tri[3] = {
            { { 0.0f,  0.25f, 0.0f }, { 1.0f, 0.25f, 0.25f, 1.0f } },
            { { 0.25f, -0.25f, 0.0f }, { 0.25f, 1.0f, 0.25f, 1.0f } },
            { { -0.25f, -0.25f, 0.0f }, { 0.25f, 0.25f, 1.0f, 1.0f } },
        };

        const UINT size = (UINT)sizeof(tri);
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC buf = CD3DX12_RESOURCE_DESC::Buffer(size);
        DX_CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sceneTriangleVB)));

        void* p = nullptr;
        CD3DX12_RANGE r(0, 0);
        DX_CHECK(sceneTriangleVB->Map(0, &r, &p));
        memcpy(p, tri, size);
        sceneTriangleVB->Unmap(0, nullptr);

        sceneTriangleVBV.BufferLocation = sceneTriangleVB->GetGPUVirtualAddress();
        sceneTriangleVBV.SizeInBytes = size;
        sceneTriangleVBV.StrideInBytes = sizeof(SceneVertex);
    }

    // Axes VB: rebuild if missing OR if view mode changed (2D uses XY axes, 3D uses XYZ)
    if (!sceneAxesVB || g_LastSceneGridBuiltViewMode != GetViewMode())
    {
        sceneAxesVB.Reset();

        const bool mode3D = (GetViewMode() == ViewMode::Mode3D);

        std::vector<SceneVertex> a;
        a.reserve(mode3D ? 6 : 4);

        // X axis (red)
        a.push_back({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.15f, 0.15f, 1.0f } });
        a.push_back({ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.15f, 0.15f, 1.0f } });

        // Y axis (green)
        a.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.15f, 1.0f, 0.15f, 1.0f } });
        a.push_back({ { 0.0f, 1.0f, 0.0f }, { 0.15f, 1.0f, 0.15f, 1.0f } });

        if (mode3D)
        {
            // Z axis (blue)
            a.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.35f, 1.0f, 1.0f } });
            a.push_back({ { 0.0f, 0.0f, 1.0f }, { 0.25f, 0.35f, 1.0f, 1.0f } });
        }

        sceneAxesVertexCount = (UINT)a.size();
        const UINT size = (UINT)(a.size() * sizeof(SceneVertex));

        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC buf = CD3DX12_RESOURCE_DESC::Buffer(size);
        DX_CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sceneAxesVB)));

        void* p = nullptr;
        CD3DX12_RANGE r(0, 0);
        DX_CHECK(sceneAxesVB->Map(0, &r, &p));
        memcpy(p, a.data(), size);
        sceneAxesVB->Unmap(0, nullptr);

        sceneAxesVBV.BufferLocation = sceneAxesVB->GetGPUVirtualAddress();
        sceneAxesVBV.SizeInBytes = size;
        sceneAxesVBV.StrideInBytes = sizeof(SceneVertex);
    }

    // Rebuild grid VB if not created yet OR if view mode changed (orientation swap)
    if (!sceneGridVB || g_LastSceneGridBuiltViewMode != GetViewMode())
    {
        sceneGridVB.Reset();

        const int half = 50;
        const float spacing = 1.0f;

        std::vector<SceneVertex> v;
        v.reserve(static_cast<size_t>((half * 2 + 1) * 4));

        const bool mode3D = (GetViewMode() == ViewMode::Mode3D);
        g_LastSceneGridBuiltViewMode = GetViewMode();

        for (int i = -half; i <= half; ++i)
        {
            const float k = (float)i * spacing;
            const bool major = (i % 10) == 0;
            const float a = major ? 0.55f : 0.25f;
            const float c = major ? 0.55f : 0.35f;
            const float col[4] = { c, c, c, a };

            if (mode3D)
            {
                // XZ grid at Y=0
                const float y = 0.0f;

                // line parallel X (vary Z)
                v.push_back({ { -half * spacing, y, k }, { col[0], col[1], col[2], col[3] } });
                v.push_back({ {  half * spacing, y, k }, { col[0], col[1], col[2], col[3] } });

                // line parallel Z (vary X)
                v.push_back({ { k, y, -half * spacing }, { col[0], col[1], col[2], col[3] } });
                v.push_back({ { k, y,  half * spacing }, { col[0], col[1], col[2], col[3] } });
            }
            else
            {
                // XY grid at Z=0
                const float z = 0.0f;

                // line parallel X (vary Y)
                v.push_back({ { -half * spacing, k, z }, { col[0], col[1], col[2], col[3] } });
                v.push_back({ {  half * spacing, k, z }, { col[0], col[1], col[2], col[3] } });

                // line parallel Y (vary X)
                v.push_back({ { k, -half * spacing, z }, { col[0], col[1], col[2], col[3] } });
                v.push_back({ { k,  half * spacing, z }, { col[0], col[1], col[2], col[3] } });
            }
        }

        sceneGridVertexCount = (UINT)v.size();
        const UINT size = (UINT)(v.size() * sizeof(SceneVertex));

        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC buf = CD3DX12_RESOURCE_DESC::Buffer(size);
        DX_CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buf, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sceneGridVB)));

        void* p = nullptr;
        CD3DX12_RANGE r(0, 0);
        DX_CHECK(sceneGridVB->Map(0, &r, &p));
        memcpy(p, v.data(), size);
        sceneGridVB->Unmap(0, nullptr);

        sceneGridVBV.BufferLocation = sceneGridVB->GetGPUVirtualAddress();
        sceneGridVBV.SizeInBytes = size;
        sceneGridVBV.StrideInBytes = sizeof(SceneVertex);
    }
}

void Graphics::RenderSceneToTarget()
{
    ++g_SceneDiag_Frame;

    // Ensure pipeline + geometry exist before any CB update.
    EnsureScenePipeline();
    EnsureSceneGeometry();

    const bool wantLog = SceneDiag_ShouldLog();

    // Skip rendering if command list or device is not ready
    if (!commandListOpen || !commandList || !device)
    {
        if (wantLog)
            Logger::Log(LogLevel::Warning, "[SceneDiag] RenderSceneToTarget early-return: command list or device not ready");
        return;
    }

    // Skip rendering if render target or depth stencil view is not ready
    if (!sceneRenderTarget || !sceneDepth || sceneDsvHandle.ptr == 0)
    {
        if (wantLog)
            Logger::Log(LogLevel::Warning, "[SceneDiag] RenderSceneToTarget early-return: sceneRenderTarget or sceneDepth/DSV not ready");
        return;
    }

    // Guard against zero/invalid RT sizes (can occur during layout transitions / first frame after docking changes).
    if (sceneRTWidth < 1 || sceneRTHeight < 1)
    {
        if (wantLog)
            Logger::Log(LogLevel::Warning, std::format(
                "[SceneDiag] RenderSceneToTarget early-return: invalid RT size {}x{}", sceneRTWidth, sceneRTHeight));
        return;
    }

    // Transition to render target
    if (sceneRTState != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        CD3DX12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(sceneRenderTarget.Get(), sceneRTState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &toRT);
        sceneRTState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Ensure depth is writable
    if (sceneDepthState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        CD3DX12_RESOURCE_BARRIER toDepth = CD3DX12_RESOURCE_BARRIER::Transition(sceneDepth.Get(), sceneDepthState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commandList->ResourceBarrier(1, &toDepth);
        sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Bind RTV + DSV
    commandList->OMSetRenderTargets(1, &sceneRtvHandle, FALSE, &sceneDsvHandle);

    // Set viewport/scissor
    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)sceneRTWidth;
    vp.Height = (float)sceneRTHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT sc = { 0, 0, (LONG)sceneRTWidth, (LONG)sceneRTHeight };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &sc);

    // Clear (locked neutral background)
    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    commandList->ClearRenderTargetView(sceneRtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(sceneDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Update constant buffer (SceneCamera)
    using namespace DirectX;

    const float aspect = (sceneRTHeight > 0) ? (static_cast<float>(sceneRTWidth) / static_cast<float>(sceneRTHeight)) : 1.0f;

    const XMMATRIX view = sceneCamera.GetView();
    const XMMATRIX proj = (GetViewMode() == ViewMode::Mode2D)
        ? sceneCamera.GetOrthoProj(aspect)
        : sceneCamera.GetProj(aspect);

    const XMMATRIX vpM = XMMatrixTranspose(view * proj);
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(sceneCBData.viewProj), vpM);

    // Camera position from inverse view matrix translation
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);
    XMFLOAT4X4 invViewF;
    XMStoreFloat4x4(&invViewF, invView);
    sceneCBData.cameraPos[0] = invViewF._41;
    sceneCBData.cameraPos[1] = invViewF._42;
    sceneCBData.cameraPos[2] = invViewF._43;

    sceneCBData.gridFadeDist = 30.0f;

    if (!sceneCB || sceneCBPtr == nullptr)
    {
        if (wantLog)
            Logger::Log(LogLevel::Warning, "[SceneDiag] SceneCB not ready/mapped; skipping scene draw this frame");
        return;
    }

    memcpy(sceneCBPtr, &sceneCBData, sizeof(sceneCBData));

    // After constant buffer update, log a few viewProj elements.
    if (wantLog)
    {
        const float* m = sceneCBData.viewProj;
        Logger::Log(LogLevel::Debug, std::format(
            "[SceneDiag] CB viewProj sample: m00={:.4f} m01={:.4f} m10={:.4f} m11={:.4f} m22={:.4f} m33={:.4f}",
            m[0], m[1], m[4], m[5], m[10], m[15]));
    }

    commandList->SetGraphicsRootSignature(sceneRootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, sceneCB->GetGPUVirtualAddress());

    // Draw grid
    if (sceneGridPSO && sceneGridVB && sceneGridVertexCount > 0)
    {
        commandList->SetPipelineState(sceneGridPSO.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        commandList->IASetVertexBuffers(0, 1, &sceneGridVBV);
        commandList->DrawInstanced(sceneGridVertexCount, 1, 0, 0);
    }

    // Draw axes (on top of grid)
    if (sceneGridPSO && sceneAxesVB && sceneAxesVertexCount > 0)
    {
        commandList->SetPipelineState(sceneGridPSO.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        commandList->IASetVertexBuffers(0, 1, &sceneAxesVBV);
        commandList->DrawInstanced(sceneAxesVertexCount, 1, 0, 0);
    }

    // Draw triangle
    if (sceneTrianglePSO && sceneTriangleVB)
    {
        commandList->SetPipelineState(sceneTrianglePSO.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &sceneTriangleVBV);
        commandList->DrawInstanced(3, 1, 0, 0);
    }

    // Transition to shader resource for ImGui sampling
    CD3DX12_RESOURCE_BARRIER toSRV = CD3DX12_RESOURCE_BARRIER::Transition(
        sceneRenderTarget.Get(),
        sceneRTState,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toSRV);
    sceneRTState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (wantLog)
    {
        Logger::Log(LogLevel::Debug, std::format(
            "[SceneDiag] RenderSceneToTarget end: state={} (expected PIXEL_SHADER_RESOURCE); SceneSRV cpu=0x{:X} gpu=0x{:X}",
            D3D12StateToString(sceneRTState),
            (UINT64)sceneSrvCpu.ptr,
            (UINT64)sceneSrvGpu.ptr));
    }
}

// Unwind helper: if a frame is in a bad state, never return leaving the command list open.
void Graphics::AbortFrame(const char* why)
{
    Logger::Log(LogLevel::Error, std::string("🚨 AbortFrame: ") + (why ? why : "<unknown>"));

    if (commandListOpen && commandList)
    {
        (void)commandList->Close();
    }

    commandListOpen = false;
    frameStarted = false;
    ImGuiFrameStarted = false;
    g_ImGuiRenderedThisFrame = false;
}
