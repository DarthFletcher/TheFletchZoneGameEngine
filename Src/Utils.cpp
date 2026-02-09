#include "Utils.h"        // ✅ Include Utils header for utility functions
#include <Windows.h>      // ✅ For MultiByteToWideChar & WideCharToMultiByte
#include <comdef.h>       // ✅ For _com_error (HRESULT error conversion)
#include <dxgi1_6.h>      // ✅ For DXGI interfaces
#include <wrl.h>          // ✅ For Microsoft::WRL::ComPtr
#include <iostream>       // ✅ For std::cerr
#include <vector>         // ✅ For storing GPU list
#include "UI.h" 		  // ✅ For UI related functions
#include <d3d12.h> 	      // ✅ For D3D12 interfaces
#include <d3d12sdklayers.h> // ✅ For D3D12_AUTO_BREADCRUMB_OP and related enums
#include <chrono> 	      // ✅ For timing functions
#include <iomanip>       // ✅ For std::put_time
#include <sstream>      // ✅ For std::ostringstream
#include <fstream> 	   // ✅ For file operations
#include <shellapi.h>    // ✅ For ShellExecute
#include <logger.h> 	// ✅ For logging
#include <filesystem> // C++17
#include <format>     // C++20
#include <stdexcept>  // ✅ For std::runtime_error
#include <string> // ✅ For std::string and std::wstring

// ✅ Define static members of GPUSelection
std::vector<std::wstring> GPUSelection::gpuList;
Microsoft::WRL::ComPtr<IDXGIAdapter1> GPUSelection::selectedGPU;

// ✅ Convert Wide String to Narrow String
std::string Utils::WideStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &strTo[0], size_needed, nullptr, nullptr);
    strTo.pop_back();  // Remove extra null terminator
    return strTo;
}

// ✅ Convert Narrow String to Wide String
std::wstring Utils::StringToWideString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstrTo[0], size_needed);
    wstrTo.pop_back();  // Remove extra null terminator
    return wstrTo;
}

// ✅ Convert HRESULT to readable string
std::string Utils::HrToString(HRESULT hr) {
    _com_error err(hr);
    return Utils::WideStringToString(err.ErrorMessage()); // ✅ Fix wide string conversion issue
}

// 🔁 Converts wide string to UTF - 8 string(for logging)
std::string Utils::WideToUTF8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), size_needed, nullptr, nullptr);
    result.pop_back(); // Remove null terminator
    return result;
}

// UTF-8 -> UTF-16 helper (replaces deprecated std::filesystem::u8path)
std::wstring Utils::Utf8ToWide(const char* utf8) noexcept
{
    if (!utf8) return {};
    int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
    return wide;
}

// Add this helper near Utf8ToWide (reverse conversion, avoids wchar_t->char copy warnings)
static std::string WideToUtf8(const std::wstring& wide) noexcept
{
    if (wide.empty()) return {};
    int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
        static_cast<int>(wide.size()),
        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

// ✅ List Available GPUs
void GPUSelection::ListAvailableGPUs() {
    gpuList.clear();
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "❌ Failed to create DXGI Factory! Error: " << Utils::HrToString(hr) << std::endl;
        return;
    }

    UINT index = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    while (factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        gpuList.push_back(desc.Description);
        ++index;

        if (index > 0) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            Logger::Log(LogLevel::Info, std::format("🔍 Found GPU [{}]: {}", index, Utils::WideStringToString(desc.Description)));
        }
    }

    if (gpuList.empty()) {
        std::cerr << "⚠ No GPUs found!" << std::endl;
        Logger::Log(LogLevel::Info, "🔍 No GPUs found!");
    }
}

// ✅ Select GPU by Index
void GPUSelection::SelectGPU(int index)
{

}

// ✅ Select GPU by Index (Now merged with SelectGPU)
Microsoft::WRL::ComPtr<IDXGIAdapter1> GPUSelection::SelectGPUByIndex(int index) {
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        throw std::runtime_error("❌ Failed to create DXGI Factory: " + Utils::HrToString(hr));
    }

    // Ensure GPU List is populated before selection
    if (gpuList.empty()) {
        GPUSelection::ListAvailableGPUs(); // ✅ Ensure the GPU list is available
    }

    // Validate GPU index
    if (index < 0 || index >= static_cast<int>(gpuList.size())) {
        throw std::runtime_error("❌ Invalid GPU index: " + std::to_string(index));
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(index, &adapter);
    if (FAILED(hr)) {
        throw std::runtime_error("❌ Failed to select GPU at index " + std::to_string(index) + ": " + Utils::HrToString(hr));
    }

    // ✅ Store the selected GPU in `selectedGPU`
    selectedGPU = adapter;

    // ✅ Print confirmation of selection
    std::wcout << L"✅ Selected GPU: " << gpuList[index] << std::endl;

    return selectedGPU;
}

// Utils Function Viewport Size Sanitization
/*ImVec2 SanitizeViewportSize(ImVec2 s)
{
    if (!std::isfinite(s.x) || s.x <= 0.0f || s.x > 8192.0f) s.x = 640.0f;
    if (!std::isfinite(s.y) || s.y <= 0.0f || s.y > 8192.0f) s.y = 480.0f;
    return s;
}*/

//Then Does this in other places:
/* ImVec2 raw_size = SanitizeViewportSize(viewport->Size);
int width = static_cast<int>(raw_size.x);
int height = static_cast<int>(raw_size.y);*/


//=====================================================================
// SanitizeViewportSize - clamps and validates viewport dimensions
//=====================================================================
static ImVec2 SanitizeViewportSize(const ImVec2& raw)
{
    ImVec2 safe = raw;

    // Clamp negatives to 0
    if (safe.x < 0.0f) safe.x = 0.0f;
    if (safe.y < 0.0f) safe.y = 0.0f;

    // Force minimum dimensions (prevent driver/device loss on tiny swapchains)
    const float MIN_SIZE = 128.0f;
    if (safe.x < MIN_SIZE) safe.x = MIN_SIZE;
    if (safe.y < MIN_SIZE) safe.y = MIN_SIZE;

    Logger::Log(LogLevel::Info, std::format(
        "🔧 SanitizeViewportSize: RAW=({:.0f}x{:.0f}) → FIXED=({:.0f}x{:.0f})",
        raw.x, raw.y, safe.x, safe.y));

    return safe;
}


// ✅ Convert D3D12_AUTO_BREADCRUMB_OP to String
const char* D3D12AutoBreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op)
{
    switch (op)
    {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
    case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return "CopyTiles";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "ResolveSubresource";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRenderTargetView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "ClearUnorderedAccessView";
    case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDepthStencilView";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "ExecuteBundle";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BeginSubmission";
    case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "EndSubmission";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME: return "DecodeFrame";
    case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES: return "ProcessFrames";
    case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT: return "AtomicCopyBufferUINT";
    case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64: return "AtomicCopyBufferUINT64";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION: return "ResolveSubresourceRegion";
    case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE: return "WriteBufferImmediate";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1: return "DecodeFrame1";
    case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION: return "SetProtectedResourceSession";
    case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2: return "DecodeFrame2";
    case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1: return "ProcessFrames1";
    case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE: return "BuildRaytracingAccelerationStructure";
    case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO: return "EmitRaytracingAccelerationStructurePostBuildInfo";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE: return "CopyRaytracingAccelerationStructure";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS: return "DispatchRays";
    case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEMETACOMMAND: return "InitializeMetaCommand";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND: return "ExecuteMetaCommand";
    case D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION: return "EstimateMotion";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP: return "ResolveMotionVectorHeap";
    case D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1: return "SetPipelineState1";
    case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEEXTENSIONCOMMAND: return "InitializeExtensionCommand";
    case D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND: return "ExecuteExtensionCommand";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH: return "DispatchMesh";
    case D3D12_AUTO_BREADCRUMB_OP_ENCODEFRAME: return "EncodeFrame";
    case D3D12_AUTO_BREADCRUMB_OP_RESOLVEENCODEROUTPUTMETADATA: return "ResolveEncoderOutputMetadata";
    case D3D12_AUTO_BREADCRUMB_OP_BARRIER: return "Barrier";
    case D3D12_AUTO_BREADCRUMB_OP_BEGIN_COMMAND_LIST: return "BeginCommandList";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCHGRAPH: return "DispatchGraph";
    case D3D12_AUTO_BREADCRUMB_OP_SETPROGRAM: return "SetProgram";
    default: return "Unknown";
    }
}

// ✅ Get Current DateTime String in "YYYY-MM-DD_HH-MM-SS" format
// This function generates a timestamp string for log file naming.
static std::string GetCurrentDateTimeString()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return oss.str(); // e.g., "2025-08-04_22-45-18"
}

// ✅ Write DRED Crash Log to File
void WriteDredCrashLogToFile(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1* breadcrumbs,
    const D3D12_DRED_PAGE_FAULT_OUTPUT* pageFault)
{
    std::string basePath = "C:\\Users\\fchil\\source\\repos\\TheFletchZoneGameEngine\\logs\\";
    if (!CreateDirectoryA(basePath.c_str(), nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            Logger::Log(LogLevel::Error, std::format("❌ Failed to create logs folder: {} (Error={})", basePath, err));
            basePath = "C:\\Temp\\TheFletchZoneLogs\\";
            CreateDirectoryA(basePath.c_str(), nullptr);
            Logger::Log(LogLevel::Info, "📁 Falling back to C:\\Temp\\TheFletchZoneLogs\\");
        }
    }

    std::string filename = basePath + "DRED_CrashLog_" + GetCurrentDateTimeString() + ".txt";

    char cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    Logger::Log(LogLevel::Info, std::string("📂 Working Directory: ") + cwd);
    Logger::Log(LogLevel::Info, std::string("📄 Intended log file path: ") + filename);

    std::ofstream out(filename);
    if (!out.is_open())
    {
        Logger::Log(LogLevel::Error, "❌ Failed to open DRED log file: " + filename);
        return;
    }

    Logger::Log(LogLevel::Error, "📄 Writing DRED crash log to: " + filename);
    out << "=== DRED GPU Crash Log ===\n\n";

    const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs ? breadcrumbs->pHeadAutoBreadcrumbNode : nullptr;
    int nodeIndex = 0;

    while (node)
    {
        out << "🧠 Breadcrumb Node [" << nodeIndex << "]\n";
        out << "  Command List: " << (node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "Unnamed") << "\n";
        out << "  Queue: " << (node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "Unknown") << "\n";

        for (UINT i = 0; i < node->BreadcrumbCount; ++i)
        {
            auto op = static_cast<D3D12_AUTO_BREADCRUMB_OP>(node->pCommandHistory[i]);
            out << "    🔹 [" << i << "] " << D3D12AutoBreadcrumbOpToString(op) << "\n";
        }

        if (node->pLastBreadcrumbValue)
        {
            auto crashOp = static_cast<D3D12_AUTO_BREADCRUMB_OP>(*node->pLastBreadcrumbValue);
            out << "  ❌ GPU Crashed At: " << D3D12AutoBreadcrumbOpToString(crashOp) << "\n";
        }

        node = node->pNext;
        ++nodeIndex;
    }

    if (!breadcrumbs || !breadcrumbs->pHeadAutoBreadcrumbNode)
        out << "⚠️ No breadcrumbs were available from DRED.\n";

    if (!pageFault || pageFault->PageFaultVA == 0)
        out << "⚠️ No page fault data was available.\n";
    else
        out << "💥 Page Fault VA: 0x" << std::hex << pageFault->PageFaultVA << "\n";

    out.close();
    Logger::Log(LogLevel::Error, "✅ DRED crash log written successfully.");

#ifdef _DEBUG
    ShellExecuteA(nullptr, "open", basePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}


// ✅ Handle DRED Dump
void HandleDredDump(ID3D12Device* device, const std::string& stage)
{
    Logger::Log(LogLevel::Error, std::format("💥 Device lost during {}. Attempting to retrieve DRED info...", stage));

    if (!device)
    {
        Logger::Log(LogLevel::Error, "❌ No valid device reference for DRED dump.");
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred))))
    {
        Logger::Log(LogLevel::Error, "❌ Failed to QueryInterface for DRED.");
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};

    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)) &&
        SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)))
    {
        WriteDredCrashLogToFile(&breadcrumbs, &pageFault);
    }
    else
    {
        Logger::Log(LogLevel::Error, "❌ Failed to retrieve DRED breadcrumb or page fault data.");
    }
}