#include <windows.h>
#include <ShellScalingAPI.h>  // 🧠 Required for SetProcessDpiAwarenessContext
#pragma comment(lib, "Shcore.lib") // 🧱 Link to Shcore.lib for DPI scaling

#include "Engine.h"
#include "HintMacros.h"
#include "Logger.h"
#include "CrashDiagnostics.h"
#include "Entity.h"
#include "Components.h"

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

Engine* g_engineInstance = nullptr;

// Minimal scene entities (temporary until a real Scene/World exists)
static std::vector<std::unique_ptr<Entity>> g_SceneEntities;
static std::vector<Entity*> g_SceneEntityPtrs;

static void EnsureTestSceneEntities()
{
    if (!g_SceneEntities.empty())
        return;

    {
        auto e = std::make_unique<Entity>();
        auto& t = e->AddComponent<TransformComponent>();
        t.position = { 0.0f, 0.0f, 0.0f };
        t.scale = { 1.0f, 1.0f, 1.0f };

        auto& mr = e->AddComponent<MeshRendererComponent>();
        mr.primitive = MeshPrimitive::Cube;
        mr.color = { 0.2f, 0.8f, 1.0f, 1.0f };
        g_SceneEntities.push_back(std::move(e));
    }

    {
        auto e = std::make_unique<Entity>();
        auto& t = e->AddComponent<TransformComponent>();
        t.position = { 2.0f, 0.0f, 0.0f };
        t.scale = { 1.0f, 1.0f, 1.0f };

        auto& mr = e->AddComponent<MeshRendererComponent>();
        mr.primitive = MeshPrimitive::Triangle;
        mr.color = { 1.0f, 0.4f, 0.2f, 1.0f };
        g_SceneEntities.push_back(std::move(e));
    }
}

const std::vector<Entity*>& GetSceneEntitiesForRendering()
{
    EnsureTestSceneEntities();

    g_SceneEntityPtrs.clear();
    g_SceneEntityPtrs.reserve(g_SceneEntities.size());
    for (auto& e : g_SceneEntities)
        g_SceneEntityPtrs.push_back(e.get());

    return g_SceneEntityPtrs;
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    CrashDiagnostics::Initialize(L"CrashDumps");

    // 🔧 Fix DPI-related mouse/input offset issues
    EnableDPIAwareness();

    // 🎮 Launch engine
    Engine engine;
    g_engineInstance = &engine;

    if (!engine.Initialize(hInstance, nCmdShow)) {
        MessageBox(nullptr, L"Failed to initialize the engine.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    engine.Run();
    engine.Shutdown();

    g_engineInstance = nullptr;

    return 0;
}

