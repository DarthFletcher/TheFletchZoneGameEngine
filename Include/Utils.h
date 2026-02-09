#pragma once

#include <string>
#include <dxgi1_6.h>
#include <wrl.h>
#include <vector>
#include <memory>  // ✅ Ensure smart pointers are fully supported
#include <d3d12sdklayers.h> // ✅ Contains D3D12_AUTO_BREADCRUMB_OP and related enums

// ✅ Utility Functions Namespace
namespace Utils {

    // ✅ Converts std::wstring to std::string
    std::string WideStringToString(const std::wstring& wstr);

    // ✅ Converts std::string to std::wstring
    std::wstring StringToWideString(const std::string& str);

    // ✅ Convert HRESULT to readable string
    std::string HrToString(HRESULT hr);
    
    //✅ Convert WideToUTF8 to readable string
    std::string WideToUTF8(const std::wstring& wstr);

	// ✅ UTF-8 -> UTF-16 helper
	std::wstring Utf8ToWide(const char* utf8) noexcept;
}

// ✅ GPU Selection Namespace
namespace GPUSelection {

    // ✅ Global GPU List (Extern to avoid multiple definitions)
    extern std::vector<std::wstring> gpuList;

    // ✅ Currently selected GPU
    extern Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedGPU;

    // ✅ List available GPUs
    void ListAvailableGPUs();

    // ✅ Select a GPU by its index
    Microsoft::WRL::ComPtr<IDXGIAdapter1> SelectGPUByIndex(int index);
}

// ✅ Convert D3D12_AUTO_BREADCRUMB_OP to String
const char* D3D12AutoBreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op);

// ✅ Get current date-time string for logging
std::string GetCurrentDateTimeString();

// ✅ Write DRED crash log to file
void WriteDredCrashLogToFile(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1* breadcrumbs,
    const D3D12_DRED_PAGE_FAULT_OUTPUT* pageFault);

// ✅ Log DRED messages to a file and main log
//void LogDredMessage(const std::string& message);

std::string GetCurrentDateTimeString();  // Ensure this is declared

void WriteDredCrashLogToFile(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1*, const D3D12_DRED_PAGE_FAULT_OUTPUT*);

void WriteDredCrashLogToFile(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1* breadcrumbs, const D3D12_DRED_PAGE_FAULT_OUTPUT* pageFault);

// Declaration (aka prototype)
void HandleDredDump(ID3D12Device* device, const std::string& caller = "");