#ifndef HINT_MACROS_H
#define HINT_MACROS_H

#include <stdexcept>   // For std::runtime_error
#include <string>      // For std::to_string
#include <sstream>     // For std::ostringstream (better error messages)
#include <comdef.h>    // For _com_error (HRESULT to string)
#include <wrl.h>       // For Microsoft::WRL::ComPtr
#include <dxgi1_6.h>   // For GPU selection
#include <vector>      // For storing available GPUs
#include <d3dx12.h>    // Fixes missing DirectX 12 macros
#include "Utils.h"     // For WideStringToString

// ✅ Error handling macro for DirectX calls
#define DX_CHECK(hr)                                      \
    if (FAILED(hr)) {                                     \
        _com_error err(hr);                               \
        std::ostringstream oss;                          \
        oss << "DirectX error at " << __FILE__           \
            << ": " << __LINE__                          \
            << " -> " << Utils::WideStringToString(err.ErrorMessage()); \
        throw std::runtime_error(oss.str());             \
    }

// ✅ Safe release macro (works with COM objects)
#define SAFE_RELEASE(p) \
    if ((p) != nullptr) { (p)->Release(); (p) = nullptr; }

// ✅ Convert HRESULT to string safely (for debugging/logging)
#define HR_TO_STRING(hr) (Utils::WideStringToString(_com_error(hr).ErrorMessage()))

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
