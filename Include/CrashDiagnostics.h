#pragma once

// CrashDiagnostics - minimal runtime crash capture for Windows builds.
// Captures:
//  - unhandled SEH / C++ terminate
//  - minidump (.dmp)
//  - best-effort logging of exception info + stack trace (DbgHelp)
//
// Usage:
//   CrashDiagnostics::Initialize(L"CrashDumps");
//
namespace CrashDiagnostics
{
    // Call once early during startup. `dumpDir` may be relative.
    void Initialize(const wchar_t* dumpDir = L"CrashDumps");
}
