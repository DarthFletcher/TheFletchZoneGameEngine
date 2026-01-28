#include "ShaderCompiler.h"

#include <windows.h>
#include <wrl.h>
#include <comdef.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <d3dcompiler.h>
#include <directx-dxc/dxcapi.h>

#pragma comment(lib, "dxcompiler.lib")

namespace fs = std::filesystem;

static fs::path GetExeDir()
{
    wchar_t exePathW[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return fs::current_path();
    return fs::path(exePathW).parent_path();
}

static fs::path ResolvePathAgainstExeDir(const std::wstring& p)
{
    fs::path path(p);
    if (path.empty())
        return path;
    if (path.is_absolute())
        return path;
    return GetExeDir() / path;
}

static std::string NarrowUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), size, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static std::wstring ToWString(const std::string& s)
{
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), size);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static uint64_t Fnv1a64(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t HashString64(const std::wstring& w)
{
    return Fnv1a64(w.data(), w.size() * sizeof(wchar_t));
}

static uint64_t HashOptions(const ShaderCompileOptions& o)
{
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ull;
    };

    mix(o.enableDebugInfo ? 1ull : 0ull);
    mix(o.disableOptimizations ? 2ull : 0ull);
    mix(o.treatWarningsAsErrors ? 4ull : 0ull);

    for (auto& d : o.additionalIncludeDirs)
        mix(HashString64(d));

    return h;
}

static std::wstring MakeCacheFileName(const fs::path& sourcePath, const std::wstring& entry, const std::wstring& profile, const ShaderCompileOptions& opts)
{
    const std::wstring stem = sourcePath.stem().wstring();

    uint64_t optHash = HashOptions(opts);

    wchar_t buf[64] = {};
    swprintf_s(buf, L"%016llX", static_cast<unsigned long long>(optHash));

    std::wstring name = stem + L"_" + entry + L"_" + profile + L"_" + buf + L".cso";
    for (auto& ch : name)
    {
        if (ch == L':' || ch == L'\\' || ch == L'/' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L'\"' || ch == L'?')
            ch = L'_';
    }
    return name;
}

static bool ReadFileBinary(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamsize sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(out.data()), sz))
        return false;
    return true;
}

static bool WriteFileBinary(const fs::path& p, const void* data, size_t size)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return (bool)f;
}

static Microsoft::WRL::ComPtr<ID3DBlob> BlobFromBytes(const void* data, size_t size)
{
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    if (FAILED(D3DCreateBlob(size, &blob)))
        throw std::runtime_error("D3DCreateBlob failed");
    memcpy(blob->GetBufferPointer(), data, size);
    return blob;
}

bool IsDXCAvailable()
{
    // We rely on dxcompiler.dll being loadable at runtime.
    HMODULE mod = LoadLibraryW(L"dxcompiler.dll");
    if (!mod)
        return false;
    FreeLibrary(mod);
    return true;
}

Microsoft::WRL::ComPtr<ID3DBlob> CompileShaderDXC(
    const std::wstring& filename,
    const std::wstring& entryPoint,
    const std::wstring& targetProfile,
    const ShaderCompileOptions& options)
{
    fs::path sourcePath = ResolvePathAgainstExeDir(filename);

    if (sourcePath.empty())
        throw std::runtime_error("CompileShaderDXC: empty filename");

    if (!fs::exists(sourcePath))
        throw std::runtime_error("CompileShaderDXC: file not found: " + NarrowUtf8(sourcePath.wstring()));

    // Cache lookup (simple: compare timestamps).
    fs::path cacheDir = ResolvePathAgainstExeDir(options.cacheDirectory);
    fs::path cachePath;
    if (options.enableCache && !cacheDir.empty())
    {
        cachePath = cacheDir / MakeCacheFileName(sourcePath, entryPoint, targetProfile, options);

        std::error_code ec;
        const auto srcTime = fs::last_write_time(sourcePath, ec);
        if (!ec && fs::exists(cachePath))
        {
            const auto cacheTime = fs::last_write_time(cachePath, ec);
            if (!ec && cacheTime >= srcTime)
            {
                std::vector<uint8_t> bytes;
                if (ReadFileBinary(cachePath, bytes))
                    return BlobFromBytes(bytes.data(), bytes.size());
            }
        }
    }

    Microsoft::WRL::ComPtr<IDxcUtils> utils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr) || !utils)
        throw std::runtime_error("DXC: failed to create IDxcUtils");

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr) || !compiler)
        throw std::runtime_error("DXC: failed to create IDxcCompiler3");

    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    hr = utils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr) || !includeHandler)
        throw std::runtime_error("DXC: failed to create default include handler");

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
    UINT32 codePage = DXC_CP_UTF8;
    hr = utils->LoadFile(sourcePath.c_str(), &codePage, &sourceBlob);
    if (FAILED(hr) || !sourceBlob)
        throw std::runtime_error("DXC: failed to load source file: " + NarrowUtf8(sourcePath.wstring()));

    DxcBuffer sourceBuf{};
    sourceBuf.Ptr = sourceBlob->GetBufferPointer();
    sourceBuf.Size = sourceBlob->GetBufferSize();
    sourceBuf.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> argvOwned;
    argvOwned.reserve(16 + options.additionalIncludeDirs.size());

    argvOwned.push_back(L"-E");
    argvOwned.push_back(entryPoint);
    argvOwned.push_back(L"-T");
    argvOwned.push_back(targetProfile);

    argvOwned.push_back(DXC_ARG_ENABLE_STRICTNESS);

    if (options.treatWarningsAsErrors)
        argvOwned.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);

    if (options.enableDebugInfo)
    {
        argvOwned.push_back(DXC_ARG_DEBUG);
        argvOwned.push_back(DXC_ARG_DEBUG_NAME_FOR_SOURCE);
        argvOwned.push_back(DXC_ARG_DEBUG_NAME_FOR_BINARY);
        argvOwned.push_back(L"-Qembed_debug");
    }

    if (options.disableOptimizations)
        argvOwned.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
    else
        argvOwned.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);

    // Include directories
    for (auto& incDir : options.additionalIncludeDirs)
    {
        argvOwned.push_back(L"-I");
        argvOwned.push_back(ResolvePathAgainstExeDir(incDir).wstring());
    }

    // Default include dir: the shader's folder.
    argvOwned.push_back(L"-I");
    argvOwned.push_back(sourcePath.parent_path().wstring());

    // Build LPCWSTR array
    std::vector<LPCWSTR> argv;
    argv.reserve(argvOwned.size());
    for (auto& s : argvOwned)
        argv.push_back(s.c_str());

    Microsoft::WRL::ComPtr<IDxcResult> result;
    hr = compiler->Compile(&sourceBuf, argv.data(), (UINT)argv.size(), includeHandler.Get(), IID_PPV_ARGS(&result));
    if (FAILED(hr) || !result)
        throw std::runtime_error("DXC: compile call failed");

    HRESULT status = S_OK;
    result->GetStatus(&status);

    if (FAILED(status))
    {
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
        Microsoft::WRL::ComPtr<IDxcBlobWide> errorsName;
        (void)result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), &errorsName);

        std::string errStr;
        if (errors && errors->GetStringPointer())
            errStr.assign(errors->GetStringPointer(), errors->GetStringPointer() + errors->GetStringLength());

        OutputDebugStringA(errStr.c_str());
        throw std::runtime_error("DXC shader compilation failed for " + NarrowUtf8(sourcePath.wstring()) + ":\n" + errStr);
    }

    Microsoft::WRL::ComPtr<IDxcBlob> object;
    Microsoft::WRL::ComPtr<IDxcBlobWide> objName;
    hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), &objName);
    if (FAILED(hr) || !object)
        throw std::runtime_error("DXC: missing DXC_OUT_OBJECT output");

    // Copy result into an ID3DBlob to match existing pipeline code.
    auto compiled = BlobFromBytes(object->GetBufferPointer(), object->GetBufferSize());

    if (options.enableCache && !cachePath.empty())
        (void)WriteFileBinary(cachePath, compiled->GetBufferPointer(), compiled->GetBufferSize());

    return compiled;
}
