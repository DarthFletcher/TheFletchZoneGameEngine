#ifndef HINT_MACROS_H
#define HINT_MACROS_H

#include <stdexcept>   // For std::runtime_error
#include <string>
#include <sstream>     // For std::ostringstream (better error messages)
#include <vector>

#include "DX12Common.h"
#include "Utils.h"     // For WideStringToString

namespace Dx12Common
{
    inline std::string HrToString(HRESULT hr)
    {
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        LPWSTR buffer = nullptr;
        const DWORD len = ::FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

        std::wstring msg;
        if (len && buffer)
        {
            msg.assign(buffer, buffer + len);
            ::LocalFree(buffer);
        }

        if (msg.empty())
        {
            std::wostringstream hex;
            hex << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
            msg = hex.str();
        }

        return Utils::WideStringToString(msg);
    }
}

// ✅ Error handling macro for DirectX calls
#define DX_CHECK(hr)                                      \
    if (FAILED(hr)) {                                     \
        std::ostringstream oss;                           \
        oss << "DirectX error at " << __FILE__            \
            << ": " << __LINE__                           \
            << " -> " << Dx12Common::HrToString(hr);      \
        throw std::runtime_error(oss.str());              \
    }

// ✅ Safe release macro (works with COM objects)
#define SAFE_RELEASE(p) \
    if ((p) != nullptr) { (p)->Release(); (p) = nullptr; }

// ✅ Convert HRESULT to string safely (for debugging/logging)
#define HR_TO_STRING(hr) (Dx12Common::HrToString(hr))

// ✅ Safe release for WRL smart pointers (ComPtr)
#define SAFE_RELEASE_COMPTR(p) (p.Reset())

// ✅ ImGui Debug Assertion Macro (safe in release builds)
#ifdef _DEBUG
#include "imgui_internal.h"
#define IMGUI_DEBUG_ASSERT(expr, msg) \
        do { \
            if (!(expr)) { \
                IM_ASSERT(expr && msg); \
            } \
        } while (0)
#else
#define IMGUI_DEBUG_ASSERT(expr, msg) ((void)(expr))
#endif

#endif // HINT_MACROS_H
