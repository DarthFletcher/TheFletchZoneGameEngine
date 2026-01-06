#include <windows.h>
#include <ShellScalingAPI.h>  // 🧠 Required for SetProcessDpiAwarenessContext
#pragma comment(lib, "Shcore.lib") // 🧱 Link to Shcore.lib for DPI scaling

#include "Engine.h"
#include "HintMacros.h"
#include "Logger.h"

const char* DpiAwarenessToString(DPI_AWARENESS awareness) {
    switch (awareness) {
    case DPI_AWARENESS_INVALID:               return "INVALID";
    case DPI_AWARENESS_UNAWARE:               return "UNAWARE";
    case DPI_AWARENESS_SYSTEM_AWARE:          return "SYSTEM_AWARE";
    case DPI_AWARENESS_PER_MONITOR_AWARE:     return "PER_MONITOR_AWARE";
    default:                                  return "UNKNOWN";
    }
}

// 🔧 Enable high-DPI awareness (Per Monitor V2)
static void EnableDPIAwareness() {
    // Try Windows 10+ DPI awareness context
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        Logger::Log(LogLevel::Warning, "⚠️ DPI Awareness Context V2 not supported. Falling back to PROCESS_PER_MONITOR_DPI_AWARE.");
        // Fallback for older Windows (Vista+)
        HRESULT hr = SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        if (FAILED(hr))
            Logger::Log(LogLevel::Error, std::format("❌ SetProcessDpiAwareness failed. HRESULT=0x{:X}", (UINT)hr));
    }
    else {
        Logger::Log(LogLevel::Info, "✅ DPI Awareness Context V2 enabled.");
    }

	// Verify current DPI awareness
    DPI_AWARENESS currentAwareness = DPI_AWARENESS_INVALID;
    PROCESS_DPI_AWARENESS procAwareness;
    HRESULT hr = GetProcessDpiAwareness(nullptr, &procAwareness);
    currentAwareness = (DPI_AWARENESS)procAwareness;
    if (SUCCEEDED(hr)) {
        Logger::Log(LogLevel::Info, std::format("📐 Current Process DPI Awareness: {}", DpiAwarenessToString(currentAwareness)));
    }
    else {
        Logger::Log(LogLevel::Error, std::format("❌ GetProcessDpiAwareness failed. HRESULT=0x{:X}", (UINT)hr));
	}

    // Log current DPI awareness
    DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(context);
    Logger::Log(LogLevel::Info, std::format("🧠 Current DPI awareness: {}", DpiAwarenessToString(awareness)));

    // 🖥️ Get desktop DPI and scaling factor
    HWND desktop = GetDesktopWindow();
    UINT dpi = GetDpiForWindow(desktop);
    float dpiScale = (dpi > 0) ? (dpi / 96.0f) : 1.0f;

    Logger::Log(LogLevel::Info, std::format("🧮 Desktop DPI: {} (Scale: {:.2f}x)", dpi, dpiScale));
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // 🔧 Fix DPI-related mouse/input offset issues
    EnableDPIAwareness();

    // 🎮 Launch engine
    Engine engine;

    if (!engine.Initialize(hInstance, nCmdShow)) {
        MessageBox(nullptr, L"Failed to initialize the engine.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    engine.Run();
    engine.Shutdown();

    return 0;
}

