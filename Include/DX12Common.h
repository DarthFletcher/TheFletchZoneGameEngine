#pragma once

// DirectX 12 + DXGI + WRL ONLY.
// Never include this from Win32-only translation units.

#include "Win32Common.h"

#pragma push_macro("Format")
#pragma push_macro("Layout")
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("string")

#ifdef Format
#undef Format
#endif
#ifdef Layout
#undef Layout
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef string
#undef string
#endif

#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <directx/d3dx12.h> // Windows SDK version

#pragma pop_macro("string")
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("Layout")
#pragma pop_macro("Format")
