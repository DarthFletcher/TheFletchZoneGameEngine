#include "CrashDiagnostics.h"

#include "Logger.h"

#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <format>
#include <string>
#include <mutex>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    std::wstring g_dumpDir;
    std::once_flag g_initOnce;

    static std::wstring GetTimestamp()
    {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        return std::format(L"{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }

    static std::wstring MakeDumpPath()
    {
        DWORD pid = ::GetCurrentProcessId();
        return std::filesystem::path(g_dumpDir) / std::format(L"TFZ_{}_{}.dmp", pid, GetTimestamp());
    }

    static void EnsureDumpDirExists()
    {
        if (g_dumpDir.empty())
            g_dumpDir = L"CrashDumps";

        std::error_code ec;
        std::filesystem::create_directories(g_dumpDir, ec);
    }

    static void LogStackTraceFromContext(CONTEXT* ctx)
    {
        if (!ctx)
            return;

        HANDLE process = ::GetCurrentProcess();
        HANDLE thread = ::GetCurrentThread();

        ::SymInitialize(process, nullptr, TRUE);

        STACKFRAME64 frame{};
        DWORD machineType = 0;

    #if defined(_M_X64)
        machineType = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx->Rip;
        frame.AddrFrame.Offset = ctx->Rbp;
        frame.AddrStack.Offset = ctx->Rsp;
    #elif defined(_M_IX86)
        machineType = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx->Eip;
        frame.AddrFrame.Offset = ctx->Ebp;
        frame.AddrStack.Offset = ctx->Esp;
    #elif defined(_M_ARM64)
        machineType = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = ctx->Pc;
        frame.AddrFrame.Offset = ctx->Fp;
        frame.AddrStack.Offset = ctx->Sp;
    #else
        return;
    #endif

        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);

        Logger::Log(LogLevel::Critical, "---- Crash stack trace (best effort) ----", "Crash");

        for (int i = 0; i < 64; i++)
        {
            if (!::StackWalk64(machineType, process, thread, &frame, ctx, nullptr,
                ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr))
            {
                break;
            }

            if (frame.AddrPC.Offset == 0)
                break;

            DWORD64 addr = frame.AddrPC.Offset;

            std::string location;
            DWORD displacement = 0;
            if (::SymGetLineFromAddr64(process, addr, &displacement, &line))
            {
                location = std::format("{}({}):{}", line.FileName ? line.FileName : "?", line.LineNumber, displacement);
            }
            else
            {
                location = "?";
            }

            DWORD64 symDisp = 0;
            std::string name = "?";
            if (::SymFromAddr(process, addr, &symDisp, sym))
            {
                name = std::format("{}+0x{:X}", sym->Name, static_cast<unsigned long long>(symDisp));
            }

            Logger::Log(LogLevel::Critical, std::format("#{:02} 0x{:016X} {} [{}]", i, static_cast<unsigned long long>(addr), name, location), "Crash");
        }

        ::SymCleanup(process);
    }

    static void WriteMiniDump(EXCEPTION_POINTERS* ep)
    {
        EnsureDumpDirExists();

        const std::wstring dumpPath = MakeDumpPath();
        HANDLE hFile = ::CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            Logger::Log(LogLevel::Critical, "Failed to create crash dump file.", "Crash");
            return;
        }

        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = ::GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;

        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithDataSegs |
            MiniDumpWithUnloadedModules |
            MiniDumpWithThreadInfo |
            MiniDumpWithHandleData);

        BOOL ok = ::MiniDumpWriteDump(
            ::GetCurrentProcess(),
            ::GetCurrentProcessId(),
            hFile,
            type,
            ep ? &mei : nullptr,
            nullptr,
            nullptr);

        ::CloseHandle(hFile);

        if (ok)
        {
            Logger::Log(LogLevel::Critical, std::format("Wrote crash dump: {}", std::filesystem::path(dumpPath).string()), "Crash");
        }
        else
        {
            Logger::Log(LogLevel::Critical, std::format("MiniDumpWriteDump failed (GetLastError={}).", (unsigned)::GetLastError()), "Crash");
        }
    }

    static LONG WINAPI UnhandledExceptionFilterFn(EXCEPTION_POINTERS* ep)
    {
        const unsigned code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        const unsigned long long addr = ep && ep->ExceptionRecord ? (unsigned long long)ep->ExceptionRecord->ExceptionAddress : 0ULL;

        Logger::Log(LogLevel::Critical, std::format("Unhandled exception 0x{:08X} at 0x{:016X}", code, addr), "Crash");

        if (ep)
            LogStackTraceFromContext(ep->ContextRecord);

        WriteMiniDump(ep);

        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void TerminateHandler()
    {
        Logger::Log(LogLevel::Critical, "std::terminate called.", "Crash");
        WriteMiniDump(nullptr);
        ::TerminateProcess(::GetCurrentProcess(), 1);
    }
}

namespace CrashDiagnostics
{
    void Initialize(const wchar_t* dumpDir)
    {
        std::call_once(g_initOnce, [dumpDir]() {
            g_dumpDir = (dumpDir && *dumpDir) ? dumpDir : L"CrashDumps";
            EnsureDumpDirExists();

            ::SetUnhandledExceptionFilter(UnhandledExceptionFilterFn);
            std::set_terminate(TerminateHandler);

            Logger::Log(LogLevel::Info, std::format("Crash diagnostics enabled. Dump dir: {}", std::filesystem::path(g_dumpDir).string()), "Crash");
        });
    }
}
