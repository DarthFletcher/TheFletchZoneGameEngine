// BootDiagnostics.cpp
#include "BootDiagnostics.h"
#include "Logger.h"
#include "imgui.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <VersionHelpers.h>
#include <algorithm> // ✅ For std::max and std::min
#include <format> // C++20 std::format
#include <windows.h>
#include <string>
#include <Utils.h>

// Add this helper function at the top or in a suitable utility file/header.
static const char* DpiAwarenessToString(DPI_AWARENESS awareness) {
    switch (awareness) {
    case DPI_AWARENESS_UNAWARE: return "Unaware";
    case DPI_AWARENESS_SYSTEM_AWARE: return "System Aware";
    case DPI_AWARENESS_PER_MONITOR_AWARE: return "Per Monitor Aware";
    default: return "Invalid";
    }
}

void RunBootDiagnostics() {
    Logger::Log(LogLevel::Info, "🌑 Waking the Black Flame...");
    Logger::Log(LogLevel::Info, "🔧 Running startup diagnostics...");

    // DPI Awareness
    DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(context);
    Logger::Log(LogLevel::Info, std::format("📐 DPI Awareness: {}", DpiAwarenessToString(awareness)));

    // ImGui Version
    Logger::Log(LogLevel::Info, std::format("🧱 ImGui Version: {}", ImGui::GetVersion()));

    // GPU Info
    IDXGIFactory6* factory = nullptr;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    IDXGIAdapter1* adapter = nullptr;
    factory->EnumAdapters1(0, &adapter);
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    std::wstring wdesc(desc.Description);
    std::string sdesc = Utils::WideStringToString(wdesc); // Proper UTF-16 → UTF-8
    Logger::Log(LogLevel::Info, std::format("🖥️ GPU: {}", sdesc));
    //Logger::Log(LogLevel::Info, std::format("🖥️ GPU: {}", std::wstring(desc.Description)));

    // OS Version
    Logger::Log(LogLevel::Info, "🧠 OS: Windows 10/11 Detected");

    // Unique Session ID
    SYSTEMTIME time{};
    GetLocalTime(&time);
    Logger::Log(LogLevel::Info, std::format("🕓 Session ID: TFZ-VOID-{:02}{:02}{:02}", time.wHour, time.wMinute, time.wSecond));

    Logger::Log(LogLevel::Info, "🔥 Boot diagnostics complete.");
}
