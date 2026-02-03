#include "Engine.h" // Main engine header
#define IMGUI_IMPL_WIN32_LOADER 
#include "imgui.h" // Dear ImGui core
#include "imgui_impl_win32_custom.h" // Custom Win32 backend
#include "imgui_impl_dx12_custom.h" // Custom DirectX 12 backend
#include <algorithm> // ✅ For std::max and std::min
#include <psapi.h> // For memory usage
#include <dxgi1_6.h> // ✅ For DXGI interfaces
#pragma comment(lib, "dxgi.lib") // Link against DXGI library
#include <ShellScalingApi.h> // For DPI awareness
#include <SplashScreen.h> // Splash screen header
#include <BootDiagnostics.h> // Boot diagnostics header
#include <BootProgress.h>
#include "UI.h"
#pragma comment(lib, "Shcore.lib") // Link against Shcore for DPI functions


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ==========================================
// ✅ Window Procedure (Handles Messages)
// ==========================================
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    
    // ✅ Let ImGui handle the message first
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_GETMINMAXINFO:
    {
        // Ensure maximize uses the monitor work area (no weird top-right offset)
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        if (mmi)
        {
            HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (monitor && GetMonitorInfo(monitor, &mi))
            {
                const RECT& rcWork = mi.rcWork;
                const RECT& rcMonitor = mi.rcMonitor;

                mmi->ptMaxPosition.x = rcWork.left - rcMonitor.left;
                mmi->ptMaxPosition.y = rcWork.top - rcMonitor.top;
                mmi->ptMaxSize.x = rcWork.right - rcWork.left;
                mmi->ptMaxSize.y = rcWork.bottom - rcWork.top;

                // Reasonable minimum track size
                mmi->ptMinTrackSize.x = (std::max)(mmi->ptMinTrackSize.x, 640L);
                mmi->ptMinTrackSize.y = (std::max)(mmi->ptMinTrackSize.y, 480L);
            }
        }
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            RECT rc{};
            if (GetClientRect(hWnd, &rc))
            {
                const UINT clientW = (UINT)(std::max)(0L, rc.right - rc.left);
                const UINT clientH = (UINT)(std::max)(0L, rc.bottom - rc.top);

                static UINT lastW = 0, lastH = 0;
                if (clientW != lastW || clientH != lastH)
                {
                    lastW = clientW;
                    lastH = clientH;

                    Logger::Log(LogLevel::Info, std::format("🔄 WM_SIZE client={:}x{:}", clientW, clientH));
                    Graphics::GetInstance().RequestResize(clientW, clientH, ResizeSource::Window);
                }
            }
        }
        break;

    case WM_SIZING:
        // Phase 0: disable interactive aspect-ratio enforcement. It causes excessive WM_SIZING churn
        // and fights the deferred swapchain resize path.
        return TRUE;

    case WM_DPICHANGED:
    {
        const UINT dpiX = HIWORD(wParam);
        const RECT* const suggestedRect = reinterpret_cast<RECT*>(lParam);

        // Resize window to suggested bounds (high-DPI monitors may report new bounds)
        SetWindowPos(hWnd,
            nullptr,
            suggestedRect->left,
            suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // ✅ Trigger ImGui DPI + font rescaling
        Graphics::GetInstance().SetupImGuiFontsAndScaling(hWnd);
        Logger::Log(LogLevel::Info, std::format("🔁 DPI Changed: {} | ImGui scale updated.", dpiX));
        return 0;
    }

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            Logger::Log(LogLevel::Warning, "⚠️ ALT Key Menu Activation Blocked");
            return 0;
        }
        break;

    case WM_CLOSE:
        if (MessageBox(hWnd, L"Are you sure you want to quit?", L"Exit Confirmation", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            Logger::Log(LogLevel::Info, "🛑 Window Close Confirmed");
            DestroyWindow(hWnd);
        }
        return 0;

    case WM_DESTROY:
        Logger::Log(LogLevel::Info, "🚪 WM_DESTROY: Exiting Application...");
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            Logger::Log(LogLevel::Info, "🔴 Escape Key Pressed - Closing Application");
            PostQuitMessage(0);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            Logger::Log(LogLevel::Warning, "⚠️ Window Deactivated");
        }
        else {
            Logger::Log(LogLevel::Info, "✅ Window Activated");
        }
        break;

    default:
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ==========================================
// ✅ DPI Awareness to String Helper
// ==========================================
static const char* DpiAwarenessToString(DPI_AWARENESS awareness) {
    switch (awareness) {
    case DPI_AWARENESS_INVALID: return "INVALID";
    case DPI_AWARENESS_UNAWARE: return "UNAWARE";
    case DPI_AWARENESS_SYSTEM_AWARE: return "SYSTEM_AWARE";
    case DPI_AWARENESS_PER_MONITOR_AWARE: return "PER_MONITOR_AWARE";
    default: return "UNKNOWN";
    }
}

// ==========================================
// ✅ Log Startup Banner
// ==========================================
static void LogStartupBanner() {
    // 🧠 DPI awareness
    DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(context);

    PROCESS_DPI_AWARENESS procAwareness = PROCESS_DPI_UNAWARE;
    GetProcessDpiAwareness(nullptr, &procAwareness);

    HWND desktop = GetDesktopWindow();
    UINT dpi = GetDpiForWindow(desktop);
    float scale = dpi > 0 ? dpi / 96.0f : 1.0f;

    // 💾 Memory usage
    PROCESS_MEMORY_COUNTERS memInfo;
    GetProcessMemoryInfo(GetCurrentProcess(), &memInfo, sizeof(memInfo));

    // 🎮 GPU name
    std::string gpuName = "Unknown GPU";
    IDXGIFactory6* dxgiFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)))) {
        IDXGIAdapter1* adapter = nullptr;
        if (SUCCEEDED(dxgiFactory->EnumAdapters1(0, &adapter))) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                char buffer[128];
                size_t converted = 0;
                wcstombs_s(&converted, buffer, desc.Description, sizeof(buffer));
                // Ensure buffer is zero-terminated before assigning to gpuName
                buffer[sizeof(buffer) - 1] = '\0'; // Guarantee null-termination
                gpuName = buffer;
            }
            adapter->Release();
        }
        dxgiFactory->Release();
    }

    // 🖥️ Monitor resolution
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // 📜 Print the boot banner
    Logger::Log(LogLevel::Info, " ");
    Logger::Log(LogLevel::Info, "============================================");
    Logger::Log(LogLevel::Info, "🚀  Booting TheFletchZone Engine...");
    Logger::Log(LogLevel::Info, std::format("🧠  DPI Awareness: Context={}, Process={}",
        DpiAwarenessToString(awareness),
        DpiAwarenessToString((DPI_AWARENESS)procAwareness)
    ));
    Logger::Log(LogLevel::Info, std::format("🧮  Desktop DPI: {} (Scale: {:.2f}x)", dpi, scale));
    Logger::Log(LogLevel::Info, std::format("💾  Memory: Used={} KB, Peak={} KB",
        memInfo.WorkingSetSize / 1024, memInfo.PeakWorkingSetSize / 1024
    ));
    Logger::Log(LogLevel::Info, std::format("🎮  GPU: {}", gpuName));
    Logger::Log(LogLevel::Info, std::format("🖥️  Resolution: {} x {}", screenWidth, screenHeight));
    Logger::Log(LogLevel::Info, std::format("📦  ImGui Version: {}", IMGUI_VERSION));
    Logger::Log(LogLevel::Info, std::format("📅  Build Date: {} | {}", __DATE__, __TIME__));
    Logger::Log(LogLevel::Info, "============================================");
    Logger::Log(LogLevel::Info, " ");
}

// ==========================================
// ✅ Engine Initialization With Splash Screen
// ==========================================
static float GetBootNowSeconds()
{
    return (float)(GetTickCount64() / 1000.0);
}

bool Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    Logger::Initialize("engine.log");
    Logger::Log(LogLevel::Info, "🛠 Engine initialization started...");
    LogStartupBanner();

    Boot::Reset(5);
    Boot::SetStage("Window", 0);
    Boot::SetStageProgress(0.0f, "Creating application window...");
    Boot::PushLine("🜂 Waking the Black Flame...", GetBootNowSeconds());

    // 1. Create Window
    if (!InitializeWindow(hInstance, nCmdShow))
        return false;
    Boot::SetStageProgress(1.0f, "Window ready");

    // 2. Initialize GPU
    Boot::SetStage("Graphics", 1);
    Boot::SetStageProgress(0.1f, "Initializing D3D12...");
    Boot::PushLine("⚙️  Spinning command queue...", GetBootNowSeconds());
    if (!graphics.Initialize(hWnd))
        return false;
    Boot::SetStageProgress(1.0f, "Graphics ready");

    // 3. Initialize ImGui
    // NOTE: ImGui is initialized inside `Graphics::Initialize()` once the swapchain/RTV heap is valid.
    // Calling it again here can re-initialize backends and break frame ownership.
    Boot::SetStage("ImGui", 2);
    Boot::SetStageProgress(1.0f, "ImGui ready");

    // Load persisted docking/layout (no-op if missing)
    UI::LoadLayoutFromDisk("imgui.ini");

    // ---------------------------------------------------------
    // 4. Load splash image (CPU decode only here)
    // ---------------------------------------------------------
    Boot::SetStage("Splash", 3);
    Boot::SetStageProgress(0.1f, "Decoding splash...");
    Logger::Log(LogLevel::Info, "🖼️ Splash: initializing image + showing until engine is ready...");
    SplashScreen::Initialize("Assets/Splash/TheFletchZone_Splash.png");
    SplashScreen::SetStatusText("Waking the Black Flame...");
    SplashScreen::Show();
    SplashScreen::SetMinimumShowTime(1.0f);
    Boot::PushLine("🛰  Uploading sigils to VRAM...", GetBootNowSeconds());
    Boot::SetStageProgress(1.0f, "Splash ready (CPU)"
    );

    // ---------------------------------------------------------
    // Boot-time render helper: draw a few frames so splash is visible
    // (keeps window responsive and uploads the splash texture as soon as DX12 is ready)
    // ---------------------------------------------------------
    auto PumpBootFrame = [&](float sleepMs)
        {
            MSG msg = {};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT || msg.message == WM_DESTROY)
                {
                    isRunning = false;
                    return;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (!IsWindow(hWnd) || IsIconic(hWnd))
                return;

            graphics.BeginFrame(hWnd);
            graphics.Render(hWnd);
            graphics.EndFrame(hWnd);
            graphics.Present(hWnd);

            if (sleepMs > 0.0f)
                std::this_thread::sleep_for(std::chrono::milliseconds((int)sleepMs));
        };

    // Give the user an immediate splash frame
    for (int i = 0; i < 2 && isRunning; ++i)
        PumpBootFrame(8.0f);

    // 5. Initialize Input
    Boot::SetStage("Input", 4);
    Boot::SetStageProgress(0.1f, "Initializing input...");
    SplashScreen::SetStatusText("🎮 Initializing Input...");
    for (int i = 0; i < 2 && isRunning; ++i)
        PumpBootFrame(8.0f);
    if (!input.Initialize(hWnd)) return false;
    Boot::SetStageProgress(1.0f, "Input ready");

    SplashScreen::SetStatusText("✅ Ready");
    Boot::SetStageProgress(1.0f, "Ready");
    for (int i = 0; i < 2 && isRunning; ++i)
        PumpBootFrame(8.0f);

    // Tell splash to fade out now that boot is complete.
    SplashScreen::RequestFinish();
    Logger::Log(LogLevel::Info, "🖼️ Splash: finish requested (engine ready)." );

    Logger::Log(LogLevel::Info, "✅ Engine initialized successfully.");
    return true;
}

// ==========================================
// ✅ Main Run Loop (Optimized)
// ==========================================
void Engine::Run() {
    Logger::Log(LogLevel::Info, "🎮 Starting Engine Run Loop...");

    MSG msg = {};
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    while (isRunning) {
        // ✅ Handle Windows Messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT || msg.message == WM_DESTROY) {
                Logger::Log(LogLevel::Warning, "🛑 WM_QUIT or WM_DESTROY received — exiting run loop.");
                isRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // ✅ Stop loop if window has been destroyed
        if (!IsWindow(hWnd)) {
            Logger::Log(LogLevel::Warning, "⚠️ HWND invalid — breaking render loop.");
            break;
        }

        // ✅ Skip rendering if minimized (reduces CPU/GPU load)
        if (IsIconic(hWnd)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // ✅ Frame Timing
        auto currentFrameTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        // ✅ Begin Frame
        Logger::Log(LogLevel::Debug, "🔁 BeginFrame()");
        graphics.BeginFrame(hWnd);

		// ✅ Input Handling // Process Input
		Logger::Log(LogLevel::Debug, "🎮 ProcessInput()");
        ProcessInput();

        // ✅ Game Logic & Rendering
        Logger::Log(LogLevel::Debug, "🎮 GameLoop()");
        GameLoop(hWnd);

        // ✅ End Frame
        graphics.EndFrame(hWnd);

        // ✅ Present Frame
        graphics.Present(hWnd);

        // ✅ Frame Time Logging for Profiling
        float frameTimeMs = deltaTime.count() * 1000.0f;
        float fps = (frameTimeMs > 0.0f) ? 1000.0f / frameTimeMs : 0.0f;
        Logger::Log(LogLevel::Info, std::format("FrameTime: {:.3f}ms ({:.1f} FPS)", frameTimeMs, fps));
    }

    Logger::Log(LogLevel::Info, "🏁 Engine Run loop ended.");
}


// ==========================================
// ✅ Graceful Shutdown
// ==========================================
void Engine::Shutdown() {
    Logger::Log(LogLevel::Info, "💤 Shutting down engine...");

    // Persist docking/layout on exit
    UI::SaveLayoutToDisk("imgui.ini");

	game.Shutdown(); // Shutdown game logic
	input.Shutdown(); // Shutdown input system
	SplashScreen::Shutdown(); // Shutdown splash screen
    graphics.Shutdown(); // Graphics will handle ImGui shutdown internally
	Logger::Shutdown(); // Shutdown logger last
}

// ==========================================
// ✅ Window Initialization
// ==========================================
bool Engine::InitializeWindow(HINSTANCE hInstance, int nCmdShow) {
    Logger::Log(LogLevel::Info, "📐 Creating application window...");

    // ✅ Register the window class
    RegisterWindowClass(hInstance);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ✅ Create the window
    hWnd = CreateWindowEx(
        0,
        L"3DGameEngine",
        L"TheFletchZone's 3D Game Engine",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    // ✅ First Error Check — With Return Value
    if (!hWnd || !IsWindow(hWnd)) {
        DWORD error = GetLastError();
        Logger::Log(LogLevel::Error, "❌ ERROR: Failed to create HWND in ImGui_ImplWin32_CreateWindow. Error code: " + std::to_string(error));
        return false;  // ✅ Return a boolean
    }

    Logger::Log(LogLevel::Info, "✅ Successfully created HWND: " + std::to_string((uintptr_t)hWnd));

    // ✅ Redundant check — can be removed or kept for safety
    if (!hWnd || !IsWindow(hWnd)) {
        Logger::Log(LogLevel::Error, "❌ Failed to create or validate window.");
        MessageBox(nullptr, L"Failed to create the window!", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // ✅ Show and Update Window
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    Logger::Log(LogLevel::Info, "✅ Window created and displayed successfully.");
    return true;  // ✅ Return true for success
}

// ==========================================
// ✅ Register Window Class
// ==========================================
void Engine::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"3DGameEngine";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; // ✅ CS_OWNDC added here
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClass(&wc)) {
        Logger::Log(LogLevel::Error, "❌ Failed to register window class.");
        MessageBox(nullptr, L"Failed to register window class.", L"Error", MB_OK | MB_ICONERROR);
        exit(EXIT_FAILURE);
    }

    Logger::Log(LogLevel::Info, "✅ Window class registered successfully.");
}

// ==========================================
// ✅ Input Handling
// ==========================================
void Engine::ProcessInput() {
    input.Update();

    if (input.IsKeyPressed(VK_ESCAPE)) {
        Logger::Log(LogLevel::Info, "🚪 ESC pressed — exiting...");
        isRunning = false;
    }
}

// ==========================================
// ✅ Game Update Logic
// ==========================================
void Engine::Update() {
    timer.Tick();
    game.Update(timer.GetDeltaTime());
}

// ==========================================
// ✅ Rendering Frame
// ==========================================
void Engine::Render(HWND hWnd) {
    (void)hWnd;
    Logger::Log(LogLevel::Debug, "🎮 Render()");
    graphics.Render(this->hWnd);
}

// ==========================================
// ✅ Message Handling (Unused in current window class)
// ==========================================
LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// ==========================================
// ✅ Engine Game Loop Wrapper
// ==========================================
void Engine::GameLoop(HWND hWnd)
{
    timer.Tick();

    // Keep splash responsive/animated until it finishes.
    SplashScreen::Update(timer.GetDeltaTime());

    if (!SplashScreen::IsVisible())
    {
        game.Update(timer.GetDeltaTime());
    }

    graphics.Render(hWnd);
}

// ==========================================
// ✅ Engine State
// ==========================================
namespace
{
    static Engine::State g_engineState = Engine::State::Editing;
}

Engine::State Engine::GetState()
{
    return g_engineState;
}

void Engine::SetState(State s)
{
    if (g_engineState == s)
        return;

    g_engineState = s;
    Logger::Log(LogLevel::Info, std::format("Engine state -> {}",
        (s == State::Editing) ? "Editing" : (s == State::Playing) ? "Playing" : "Paused"));
}

void Engine::NewScene() { Logger::Log(LogLevel::Info, "NewScene()"); }
void Engine::SaveScene() { Logger::Log(LogLevel::Info, "SaveScene()"); }
void Engine::LoadScene() { Logger::Log(LogLevel::Info, "LoadScene()"); }
