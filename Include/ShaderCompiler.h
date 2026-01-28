#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <vector>

struct ShaderCompileOptions
{
    bool enableDebugInfo = false;         // -Zi, -Qembed_debug
    bool disableOptimizations = false;    // -Od (else -O3)
    bool treatWarningsAsErrors = false;   // -WX
    bool enableCache = true;

    // Relative or absolute directory. If relative, resolved against the executable directory.
    std::wstring cacheDirectory = L"ShaderCache";

    // Additional include directories passed to DXC as -I. (Optional)
    std::vector<std::wstring> additionalIncludeDirs;
};

bool IsDXCAvailable();

// Compile HLSL with DXC. Returns DXIL container blob.
Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderDXC(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile,
    const ShaderCompileOptions& options);
