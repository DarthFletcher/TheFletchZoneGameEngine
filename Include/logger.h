#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <set>
#include <atomic>
#include <imgui.h>
#include <windows.h>   // ✅ Needed for HRESULT
#include <dxgi.h> // For DXGI_ERROR_* codes

// 🎯 Unified Log Levels
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Success,   // ✅ New success level
    Warning,
    Error,
    Critical,   // 🚨 New critical level
    Verbose
};

class Logger {
public:
    // Initialize the logger (called once during engine startup)
    static void Initialize(const std::string& logFileName, size_t maxSizeMB = 5, int maxFiles = 5);

    // Log message (category auto-defaults to "General")
    static void Log(LogLevel level, const std::string& message, const std::string& category = "General");

    // Shutdown the logger (close file handles, etc.)
    static void Shutdown();

    // ImGui console overlay (optional)
    static void DrawConsole();

    // Memory usage helper (engine debug tracking)
    static void LogMemoryUsage(const std::string& context = "Memory");

	// HRESULT to string helper
    static const char* DX12_HRToString(HRESULT hr);

private:
    static std::ofstream logFile;
    static std::mutex logMutex;

    // 🔍 Logs are categorized internally
    static std::unordered_map<std::string, std::vector<std::string>> categorizedLogs;
    static std::set<std::string> activeCategories;

    static bool autoScroll;
    static char logFilter[256];
    static size_t maxLogBufferSize;

    // 🔧 Helpers
    static const char* LogLevelToString(LogLevel level);
    static const char* LogLevelToEmoji(LogLevel level);
    static ImVec4 LogLevelToColor(LogLevel level);
    static void RotateLogsIfNeeded(const std::string& logFileName, int maxFiles);
    static size_t GetCurrentFileSize(const std::string& fileName);
    static std::string GetCurrentTimestamp();
};
