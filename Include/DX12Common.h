#pragma once

// ---- Inclusion-order tripwire ----
// DX12Common.h should be included before any Win32 headers in our own DX translation units.
// Leave this as an opt-in strict diagnostic to avoid breaking external headers that may
// legitimately include this after Windows is already included.
#if defined(DX12COMMON_ENFORCE_INCLUDE_ORDER) && defined(_WINDOWS_)
#error DX12Common.h must be included before any Win32 headers. Include DX12Common.h first in DX translation units.
#endif

// ---- Windows-only build tripwires ----
#if defined(__linux__) || defined(__WSL__) || defined(__wsl__)
#error Detected Linux/WSL preprocessor macros in a Windows build. Fix include paths/toolchain; DirectX-Headers WSL stubs must not be used.
#endif

// The Windows SDK COM headers (e.g. `ocidl.h`) rely on a MIDL-style `interface` macro.
// Define it only for the duration of including Win32 headers.
#ifndef interface
#define interface struct
#endif

// Always bring in Windows types/macro settings via our centralized Win32 header.
#include "Win32Common.h"

// ---- Tripwire: if Windows SDK d3d12.h was already included, stop. ----
#ifdef _D3D12_H_
#error Windows SDK d3d12.h was included before DirectX-Headers. Remove <d3d12.h> includes and route all D3D12 usage through DX12Common.h.
#endif

// ---- Macro firewall around DirectX-Headers ----

#pragma push_macro("Format")
#pragma push_macro("Layout")
#pragma push_macro("min")
#pragma push_macro("max")

#undef Format
#undef Layout
#undef min
#undef max

#if defined(string)
#pragma push_macro("string")
#undef string
#define DX12COMMON_RESTORE_string 1
#endif

// Must be defined before including <directx/d3dx12.h>.
#ifndef D3DX12_NO_PROPERTY_FORMAT_TABLE_HELPERS
#define D3DX12_NO_PROPERTY_FORMAT_TABLE_HELPERS
#endif

#ifndef D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#endif

#ifndef D3DX12_NO_STATE_OBJECT_HELPERS
#define D3DX12_NO_STATE_OBJECT_HELPERS
#endif

// DXGI types must be available before including D3DX12 helpers.
#include <directx/dxgiformat.h>

// D3DX12 property format table expects these enums from DirectX-Headers' d3dcommon.h.
#include <directx/d3dcommon.h>

// DirectX-Headers D3D12
#include <directx/d3d12.h>

// DirectX-Headers umbrella for D3DX12 helpers
#include <directx/d3dx12.h>

// ---- Restore macros ----
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("Layout")
#pragma pop_macro("Format")

#if defined(DX12COMMON_RESTORE_string)
#undef DX12COMMON_RESTORE_string
#pragma pop_macro("string")
#endif
