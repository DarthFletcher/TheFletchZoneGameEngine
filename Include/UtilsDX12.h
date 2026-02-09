#pragma once

#include "DX12Common.h"
#include "Utils.h"

// DX12-specific utilities split out of `Utils.h` to keep Win32-only TUs clean.

// ? Convert D3D12_AUTO_BREADCRUMB_OP to String
const char* D3D12AutoBreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op);

// ? Write DRED crash log to file
void WriteDredCrashLogToFile(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1* breadcrumbs,
    const D3D12_DRED_PAGE_FAULT_OUTPUT* pageFault);

// Declaration (aka prototype)
void HandleDredDump(ID3D12Device* device, const std::string& caller = "");
