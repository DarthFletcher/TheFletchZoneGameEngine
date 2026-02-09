// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"

// Ensure basic Windows types/macros are defined before pulling in the rest of the Windows SDK.
// This avoids IntelliSense/build parse errors in some configurations (e.g. ULONG undefined in wincrypt/bcrypt).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <sdkddkver.h>
#include <winnt.h>
#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
