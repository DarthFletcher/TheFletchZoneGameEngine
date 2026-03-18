#pragma once

#include "imgui.h"
#include <vector>
#include <string>
#include <wrl.h>
#include <dxgi1_6.h>

extern ImFont* g_UIFont;
extern ImFont* g_UIFontBold;
extern ImFont* g_MonoFont;

// ==========================
// 🌅 UI Namespace
// ==========================
namespace UI {

	// 🎨 Get Clear Color Based on Theme
	ImVec4 GetClearColor();
	
    // 🎨 Theme Enumeration
    enum class Theme {
        Dark,
        Light,
        Classic,
        Synthwave,
        Magenta
    };

	// 🧭 Main Dock Space (UI-owned)
    ImGuiID BeginDockSpace();
    void RequestResetLayout();

    // ImGui .ini layout persistence
    void LoadLayoutFromDisk(const char* iniPath = "imgui.ini");
    void SaveLayoutToDisk(const char* iniPath = "imgui.ini");

    // Editor shell: fixed frame submission order for the editor UI.
    void DrawEditorShell();

	void ShowMainMenu();
	void DrawMainMenuBar();
	void ShowGPUSelectionMenu(const std::vector<std::wstring>& gpuList, int& selectedGPUIndex);
	void DrawOverlays();
    void DrawSplashOverlay();
    void DrawCommandStrip();
    void DrawEditorPanels();
    void ApplyTheme(Theme theme);
	void SetMainWindowSize(int width, int height);

    // Applies a pending default dock layout build. Call this once per frame AFTER windows are drawn.
    void EndDockSpaceFrame();

    // ImGui debug tools
    bool IsImGuiDemoVisible();
    bool IsImGuiMetricsVisible();
}

// ==========================
// 🖥️ GPU Selection Namespace
// ==========================
namespace GPUSelection {
    extern std::vector<std::wstring> gpuList;
    extern Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedGPU;

    void ListAvailableGPUs();
    void SelectGPU(int index);
}







