#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <psapi.h>
#include <dxgi.h> // For DXGI_ERROR_* codes

// Statics Initialization
std::ofstream Logger::logFile;
std::mutex Logger::logMutex;
std::unordered_map<std::string, std::vector<std::string>> Logger::categorizedLogs;
std::set<std::string> Logger::activeCategories;
bool Logger::autoScroll = true;
char Logger::logFilter[256] = "";
size_t Logger::maxLogBufferSize = 5000;

// Initialization
void Logger::Initialize(const std::string& logFileName, size_t maxSizeMB, int maxFiles) {
    RotateLogsIfNeeded(logFileName, maxFiles);
    logFile.open(logFileName, std::ios::out | std::ios::trunc);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file: " << logFileName << std::endl;
    }
}

// Rotate old logs
void Logger::RotateLogsIfNeeded(const std::string& logFileName, int maxFiles) {
    namespace fs = std::filesystem;
    for (int i = maxFiles - 1; i >= 0; --i) {
        std::string oldName = logFileName + (i == 0 ? "" : "." + std::to_string(i));
        std::string newName = logFileName + "." + std::to_string(i + 1);
        if (fs::exists(oldName)) {
            fs::rename(oldName, newName);
        }
    }
}

size_t Logger::GetCurrentFileSize(const std::string& fileName) {
    if (std::filesystem::exists(fileName)) {
        return std::filesystem::file_size(fileName);
    }
    return 0;
}

// The main logger
void Logger::Log(LogLevel level, const std::string& message, const std::string& category) {
    std::lock_guard<std::mutex> lock(logMutex);

    auto sanitizeMessage = [](const std::string& msg) -> std::string {
        size_t i = 0;
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
            ++i;

        // Strip leading legacy markers: "?" / "??" etc.
        while (i < msg.size() && msg[i] == '?')
            ++i;

        // Strip common ASCII marker prefixes.
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
            ++i;
        if (i + 1 < msg.size() && msg[i] == '-' && msg[i + 1] == '>')
            i += 2;
        else if (i + 1 < msg.size() && msg[i] == '=' && msg[i + 1] == '>')
            i += 2;

        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t' || msg[i] == '-' || msg[i] == '·'))
            ++i;

        // If the message already starts with an emoji, keep it (user wants emoji in message).
        return msg.substr(i);
    };

    std::string cleaned = sanitizeMessage(message);

    std::ostringstream output;
    output << "[" << LogLevelToString(level) << "] "
           << GetCurrentTimestamp()
           << " [" << category << "] : "
           << cleaned;

    std::string logEntry = output.str();
    std::cout << logEntry << std::endl;

    if (logFile.is_open()) {
        logFile << logEntry << std::endl;
    }

    auto& logVec = categorizedLogs[category];
    logVec.push_back(logEntry);
    if (logVec.size() > maxLogBufferSize) {
        logVec.erase(logVec.begin(), logVec.begin() + 100);
    }

    activeCategories.insert(category);
}

// Shutdown logic
void Logger::Shutdown() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::ClearAllLogs()
{
    std::lock_guard<std::mutex> lock(logMutex);
    for (auto& [_, logs] : categorizedLogs)
        logs.clear();
}

std::vector<std::string> Logger::GetRecentLogs(size_t maxLines)
{
    std::lock_guard<std::mutex> lock(logMutex);

    // Flatten across categories (best-effort in insertion order by category vectors).
    // Minimal-risk approach: use the default "General" first if present; then include others.
    std::vector<std::string> out;
    out.reserve(maxLines);

    auto appendTail = [&](const std::vector<std::string>& v)
    {
        if (out.size() >= maxLines) return;
        const size_t take = (std::min)(maxLines - out.size(), v.size());
        const size_t start = v.size() - take;
        for (size_t i = start; i < v.size(); ++i)
            out.push_back(v[i]);
    };

    auto itGeneral = categorizedLogs.find("General");
    if (itGeneral != categorizedLogs.end())
        appendTail(itGeneral->second);

    for (const auto& [cat, logs] : categorizedLogs)
    {
        if (cat == "General")
            continue;
        appendTail(logs);
        if (out.size() >= maxLines)
            break;
    }

    return out;
}

// Timestamp helper
std::string Logger::GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime;
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream ss;
    ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Convert log level to string
const char* Logger::LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Success: return "SUCCESS";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
    case LogLevel::Verbose: return "VERBOSE";
    default: return "UNKNOWN";
    }
}

// Convert log level to emoji string (for display)
const char* Logger::LogLevelToEmoji(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "🔍";
    case LogLevel::Debug: return "🛠";
    case LogLevel::Info: return "ℹ️";
    case LogLevel::Success: return "✅";
    case LogLevel::Warning: return "⚠️";
    case LogLevel::Error: return "❌";
    case LogLevel::Critical: return "🚨";
    case LogLevel::Verbose: return "📢";
    default: return "❓";
    }
}

// Convert log level to ImGui color
ImVec4 Logger::LogLevelToColor(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return ImVec4(0.6f, 0.6f, 1.0f, 1.0f);
    case LogLevel::Debug: return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
    case LogLevel::Info: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    case LogLevel::Success: return ImVec4(0.2f, 0.9f, 0.2f, 1.0f);
    case LogLevel::Warning: return ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
    case LogLevel::Error: return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    case LogLevel::Critical: return ImVec4(1.0f, 0.1f, 0.1f, 1.0f);
    case LogLevel::Verbose: return ImVec4(0.5f, 0.7f, 1.0f, 1.0f); 
    default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

// ImGui draw console
void Logger::DrawConsole() {
    ImGui::Begin("Console");

    if (ImGui::Button("Clear")) {
        for (auto& [_, logs] : categorizedLogs) logs.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &autoScroll);
    ImGui::Separator();

    ImGui::InputText("Filter", logFilter, sizeof(logFilter));
    ImGui::Separator();

    static std::string selectedCategory = "General";
    if (ImGui::BeginCombo("Category", selectedCategory.c_str())) {
        for (const auto& category : activeCategories) {
            bool selected = (selectedCategory == category);
            if (ImGui::Selectable(category.c_str(), selected))
                selectedCategory = category;
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::BeginChild("LogWindow", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& logs = categorizedLogs[selectedCategory];
    for (const auto& log : logs) {

        LogLevel level = LogLevel::Info;
        if (log.find("[ERROR]") != std::string::npos) level = LogLevel::Error;
        else if (log.find("[WARNING]") != std::string::npos) level = LogLevel::Warning;
        else if (log.find("[DEBUG]") != std::string::npos) level = LogLevel::Debug;
        else if (log.find("[TRACE]") != std::string::npos) level = LogLevel::Trace;
        else if (log.find("[SUCCESS]") != std::string::npos) level = LogLevel::Success;
        else if (log.find("[CRITICAL]") != std::string::npos) level = LogLevel::Critical;
        else if (log.find("[VERBOSE]") != std::string::npos) level = LogLevel::Verbose;

        if (strlen(logFilter) > 0 && log.find(logFilter) == std::string::npos)
            continue;

        ImGui::PushStyleColor(ImGuiCol_Text, LogLevelToColor(level));
        ImGui::TextUnformatted(log.c_str());
        ImGui::PopStyleColor();
    }

    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}

// Memory usage logging
void Logger::LogMemoryUsage(const std::string& context) {
    PROCESS_MEMORY_COUNTERS memInfo;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memInfo, sizeof(memInfo))) {
        SIZE_T used = memInfo.WorkingSetSize;
        SIZE_T peak = memInfo.PeakWorkingSetSize;
        Log(LogLevel::Info, "Current Memory Usage: " + std::to_string(used / 1024) + " KB, Peak: " + std::to_string(peak / 1024) + " KB", context);
    }
    else {
        Log(LogLevel::Warning, "Failed to get memory usage.", context);
    }
}

// HRESULT to string conversion
// -----------------------------------------------------------------------------
// 🔍 Convert HRESULT to human-readable string
// -----------------------------------------------------------------------------
const char* Logger::DX12_HRToString(HRESULT hr)
{
    switch (hr)
    {
    case S_OK: return "S_OK (Success)";
    case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
    case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    case E_INVALIDARG: return "E_INVALIDARG";
    case E_FAIL: return "E_FAIL (Unspecified failure)";
    default: return "Unknown HRESULT";
    }
}