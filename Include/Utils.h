#pragma once

#include <string>
#include <vector>
#include <memory>

// Forward declarations to keep this header free of Win32/DX includes.
struct IDXGIAdapter1;

// ✅ Utility Functions Namespace
namespace Utils {

    // ✅ Converts std::wstring to std::string
    std::string WideStringToString(const std::wstring& wstr);

    // ✅ Converts std::string to std::wstring
    std::wstring StringToWideString(const std::string& str);

    // ✅ Convert HRESULT to readable string
    std::string HrToString(long hr);

    //✅ Convert WideToUTF8 to readable string
    std::string WideToUTF8(const std::wstring& wstr);

    // ✅ UTF-8 -> UTF-16 helper
    std::wstring Utf8ToWide(const char* utf8) noexcept;
}

// ✅ GPU Selection Namespace
namespace GPUSelection {

    // ✅ Global GPU List (Extern to avoid multiple definitions)
    extern std::vector<std::wstring> gpuList;

    // ✅ Currently selected GPU (kept opaque here; defined/owned in DX-capable code)
    extern IDXGIAdapter1* selectedGPU;

    // ✅ List available GPUs
    void ListAvailableGPUs();

    // ✅ Select a GPU by its index
    IDXGIAdapter1* SelectGPUByIndex(int index);
}

// ✅ Get current date-time string for logging
std::string GetCurrentDateTimeString();