#pragma once

// Windows-only common include.
// Use this for non-DX code that needs Win32 APIs/types.
// DX12/DXGI/DirectX-Headers must go through `DX12Common.h`.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
