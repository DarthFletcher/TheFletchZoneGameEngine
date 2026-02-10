#include "ShaderUtils.h"
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <cctype>

#include "ShaderCompiler.h"

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* source, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        throw std::runtime_error("Failed to compile shader");
    }
    return shaderBlob;
}

static bool IsSM6Profile(const char* target)
{
    if (!target) return false;
    std::string t(target);
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    // Common SM6 profiles: vs_6_0, ps_6_6, cs_6_0, lib_6_3, etc.
    return t.find("_6_") != std::string::npos;
}

static std::wstring WidenAscii(const char* s)
{
    if (!s) return {};
    std::wstring w;
    while (*s)
        w.push_back((wchar_t)(unsigned char)*s++);
    return w;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromFile(const wchar_t* filename, const char* entryPoint, const char* target)
{
    // Prefer DXC automatically for SM6 profiles.
    if (IsSM6Profile(target))
    {
        ShaderCompileOptions opts;
#if defined(_DEBUG)
        opts.enableDebugInfo = true;
        opts.disableOptimizations = true;
#else
        opts.enableDebugInfo = false;
        opts.disableOptimizations = false;
#endif
        opts.treatWarningsAsErrors = false;
        opts.enableCache = true;

        return CompileShaderDXC(
            filename ? std::wstring(filename) : std::wstring(),
            entryPoint ? WidenAscii(entryPoint) : std::wstring(L"main"),
            target ? WidenAscii(target) : std::wstring(L"ps_6_0"),
            opts);
    }

    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // Resolve candidate paths deterministically so we can report exactly what was tried.
    std::filesystem::path original = filename ? std::filesystem::path(filename) : std::filesystem::path();
    std::filesystem::path exeResolved;
    std::filesystem::path cwdResolved;

    if (filename && !original.is_absolute())
    {
        wchar_t exePathW[MAX_PATH] = {};
        DWORD exeLen = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
        if (exeLen > 0 && exeLen < MAX_PATH)
        {
            const std::filesystem::path exeDir = std::filesystem::path(exePathW).parent_path();
            exeResolved = exeDir / original;
        }

        cwdResolved = std::filesystem::absolute(original);
    }

    auto exists = [](const std::filesystem::path& p) -> bool
    {
        if (p.empty()) return false;
        std::error_code ec;
        return std::filesystem::exists(p, ec);
    };

    auto tryCompile = [&](const wchar_t* file) -> HRESULT
    {
        return D3DCompileFromFile(
            file,
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            target,
            flags,
            0,
            &shaderBlob,
            &errorBlob);
    };

    // Step 1: if an absolute or caller-provided path exists, try it first.
    HRESULT hr = E_FAIL;
    if (filename && (original.is_absolute() || exists(original)))
        hr = tryCompile(original.c_str());
    else
        hr = tryCompile(filename);

    // Step 2: if relative and failed, prefer EXE-resolved.
    if (FAILED(hr) && filename && !original.is_absolute() && !exeResolved.empty() && exists(exeResolved))
    {
        shaderBlob.Reset();
        errorBlob.Reset();
        hr = tryCompile(exeResolved.c_str());
    }

    // Step 3: if still failing, try CWD-resolved.
    if (FAILED(hr) && filename && !original.is_absolute() && !cwdResolved.empty() && exists(cwdResolved))
    {
        shaderBlob.Reset();
        errorBlob.Reset();
        hr = tryCompile(cwdResolved.c_str());
    }

    if (FAILED(hr))
    {
        std::string errors;
        if (errorBlob && errorBlob->GetBufferPointer() && errorBlob->GetBufferSize() > 0)
        {
            errors.assign(
                static_cast<const char*>(errorBlob->GetBufferPointer()),
                static_cast<size_t>(errorBlob->GetBufferSize()));
            OutputDebugStringA(errors.c_str());
        }

        char hrHex[11] = {};
        sprintf_s(hrHex, "0x%08X", static_cast<unsigned int>(hr));

        wchar_t cwdW[MAX_PATH] = {};
        DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH, cwdW);
        std::wstring wcwd = (cwdLen > 0 && cwdLen < MAX_PATH) ? std::wstring(cwdW) : L"";

        auto narrow = [](const std::wstring& w) -> std::string {
            if (w.empty()) return {};
            int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (sizeNeeded <= 1) return {};
            std::string out(static_cast<size_t>(sizeNeeded - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), sizeNeeded, nullptr, nullptr);
            return out;
        };

        const std::string fileN = filename ? narrow(std::wstring(filename)) : std::string("(null)");
        const std::string cwdN = narrow(wcwd);

        std::string msg = "Failed to compile shader from file: " + fileN +
            " entryPoint=" + (entryPoint ? entryPoint : "(null)") +
            " target=" + (target ? target : "(null)") +
            " hr=" + hrHex;

        if (!cwdN.empty())
            msg += " cwd=" + cwdN;

        if (!original.empty())
            msg += " originalExists=" + std::string(exists(original) ? "1" : "0");
        if (!exeResolved.empty())
            msg += " exeResolved=" + narrow(exeResolved.wstring()) + " exeExists=" + (exists(exeResolved) ? "1" : "0");
        if (!cwdResolved.empty())
            msg += " cwdResolved=" + narrow(cwdResolved.wstring()) + " cwdExists=" + (exists(cwdResolved) ? "1" : "0");

        if (!errors.empty())
            msg += "\n" + errors;

        // Also push the full message to the debugger output.
        OutputDebugStringA((msg + "\n").c_str());

        throw std::runtime_error(msg);
    }

    return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderFromRelativeFile(const wchar_t* relativePath, const char* entryPoint, const char* target)
{
    namespace fs = std::filesystem;

    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        throw std::runtime_error("GetModuleFileNameW failed while resolving shader path");

    fs::path base = fs::path(exePath).parent_path();
    fs::path full = base / fs::path(relativePath);

    return CompileShaderFromFile(full.c_str(), entryPoint, target);
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderDXCFromRelativeFile(
    const wchar_t* relativePath,
    const wchar_t* entryPoint,
    const wchar_t* targetProfile,
    const ShaderCompileOptions& options)
{
    namespace fs = std::filesystem;

    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        throw std::runtime_error("GetModuleFileNameW failed while resolving shader path");

    fs::path base = fs::path(exePath).parent_path();
    fs::path full = base / fs::path(relativePath);

    return CompileShaderDXC(full.wstring(), entryPoint ? std::wstring(entryPoint) : std::wstring(L"main"), targetProfile ? std::wstring(targetProfile) : std::wstring(L"vs_6_0"), options);
}
