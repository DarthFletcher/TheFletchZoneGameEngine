#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <windows.h>
#include <psapi.h>
#include <dxgi.h> // For DXGI_ERROR_* codes

namespace
{
    std::string SanitizeMessage(const std::string& msg)
    {
        size_t i = 0;
        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
            ++i;

        while (i < msg.size() && msg[i] == '?')
            ++i;

        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t'))
            ++i;
        if (i + 1 < msg.size() && msg[i] == '-' && msg[i + 1] == '>')
            i += 2;
        else if (i + 1 < msg.size() && msg[i] == '=' && msg[i + 1] == '>')
            i += 2;

        while (i < msg.size() && (msg[i] == ' ' || msg[i] == '\t' || msg[i] == '-' || msg[i] == '·'))
            ++i;

        return msg.substr(i);
    }

    std::string NormalizeCategory(std::string category)
    {
        if (category.empty())
            return "General";

        while (!category.empty() && (category.front() == ' ' || category.front() == '\t'))
            category.erase(category.begin());
        while (!category.empty() && (category.back() == ' ' || category.back() == '\t'))
            category.pop_back();

        if (category.size() >= 2 && category.front() == '[' && category.back() == ']')
            category = category.substr(1, category.size() - 2);

        return category.empty() ? "General" : category;
    }
}

// Statics Initialization
std::ofstream Logger::logFile;
std::mutex Logger::logMutex;
std::atomic<uint64_t> Logger::currentFrameNumber = 0;
std::vector<LogEntry> Logger::timelineLogs;
std::unordered_map<std::string, std::vector<size_t>> Logger::categoryTimelineIndices;
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

void Logger::SetFrameNumber(uint64_t frameNumber)
{
    currentFrameNumber.store(frameNumber, std::memory_order_relaxed);
}

uint64_t Logger::GetFrameNumber()
{
    return currentFrameNumber.load(std::memory_order_relaxed);
}

void Logger::LogFrameBanner(uint64_t frameNumber)
{
    Log(LogLevel::Info, std::format(
        "\n------------------------------------\nFrame {}\n------------------------------------",
        frameNumber), "Frame");
}

void Logger::LogPassBegin(const std::string& passName)
{
    const std::string category = NormalizeCategory(passName);
    Log(LogLevel::Debug, "BEGIN", category);
}

void Logger::LogPassEnd(const std::string& passName)
{
    const std::string category = NormalizeCategory(passName);
    Log(LogLevel::Debug, "END", category);
}

std::string Logger::FormatLogEntry(const LogEntry& entry)
{
    std::ostringstream output;
    output << "[" << LogLevelToString(entry.level) << "] "
           << entry.timestamp
           << " [Frame " << entry.frame << "]"
           << " [" << entry.category << "] : "
           << entry.message;
    return output.str();
}

void Logger::RebuildCategoryTimelineIndices()
{
    categoryTimelineIndices.clear();
    for (size_t i = 0; i < timelineLogs.size(); ++i)
        categoryTimelineIndices[timelineLogs[i].category].push_back(i);
}

// The main logger
void Logger::Log(LogLevel level, const std::string& message, const std::string& category) {
    std::lock_guard<std::mutex> lock(logMutex);

    const std::string cleaned = SanitizeMessage(message);
    const std::string normalizedCategory = NormalizeCategory(category);

    LogEntry entry;
    entry.frame = currentFrameNumber.load(std::memory_order_relaxed);
    entry.timestamp = GetCurrentTimestamp();
    entry.level = level;
    entry.category = normalizedCategory;
    entry.message = cleaned;

    const std::string logEntry = FormatLogEntry(entry);
    std::cout << logEntry << std::endl;

    if (logFile.is_open()) {
        logFile << logEntry << std::endl;
    }

    const size_t timelineIndex = timelineLogs.size();
    timelineLogs.push_back(entry);
    categoryTimelineIndices[normalizedCategory].push_back(timelineIndex);

    auto& logVec = categorizedLogs[normalizedCategory];
    logVec.push_back(logEntry);
    if (logVec.size() > maxLogBufferSize) {
        logVec.erase(logVec.begin(), logVec.begin() + (std::min)(size_t(100), logVec.size()));
    }

    if (timelineLogs.size() > maxLogBufferSize)
    {
        const size_t trimCount = (std::min)(size_t(100), timelineLogs.size());
        timelineLogs.erase(timelineLogs.begin(), timelineLogs.begin() + trimCount);
        RebuildCategoryTimelineIndices();
    }

    activeCategories.insert(normalizedCategory);
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
    timelineLogs.clear();
    categoryTimelineIndices.clear();
    for (auto& [_, logs] : categorizedLogs)
        logs.clear();
}

std::vector<std::string> Logger::GetRecentLogs(size_t maxLines)
{
    std::lock_guard<std::mutex> lock(logMutex);

    std::vector<std::string> out;
    const size_t take = (std::min)(maxLines, timelineLogs.size());
    out.reserve(take);

    const size_t start = timelineLogs.size() - take;
    for (size_t i = start; i < timelineLogs.size(); ++i)
        out.push_back(FormatLogEntry(timelineLogs[i]));

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
        ClearAllLogs();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &autoScroll);
    ImGui::Separator();

    ImGui::InputText("Filter", logFilter, sizeof(logFilter));
    ImGui::Separator();

    static std::string selectedCategory = "All";
    if (ImGui::BeginCombo("Category", selectedCategory.c_str())) {
        const bool allSelected = (selectedCategory == "All");
        if (ImGui::Selectable("All", allSelected))
            selectedCategory = "All";

        for (const auto& category : activeCategories) {
            bool selected = (selectedCategory == category);
            if (ImGui::Selectable(category.c_str(), selected))
                selectedCategory = category;
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::BeginChild("LogWindow", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::lock_guard<std::mutex> lock(logMutex);

    auto drawEntry = [&](const LogEntry& entry)
    {
        const std::string line = FormatLogEntry(entry);
        if (strlen(logFilter) > 0 && line.find(logFilter) == std::string::npos)
            return;

        ImGui::PushStyleColor(ImGuiCol_Text, LogLevelToColor(entry.level));
        ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    };

    if (selectedCategory == "All")
    {
        for (const auto& entry : timelineLogs)
            drawEntry(entry);
    }
    else if (const auto it = categoryTimelineIndices.find(selectedCategory); it != categoryTimelineIndices.end())
    {
        for (size_t index : it->second)
        {
            if (index < timelineLogs.size())
                drawEntry(timelineLogs[index]);
        }
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