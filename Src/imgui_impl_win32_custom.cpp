// imgui_impl_win32_custom.cpp - Custom Dear ImGui Win32 Backend
// Author: TheFletchZone Game Engine

#include "imgui_impl_win32_custom.h"
#include "Logger.h"
#include <windows.h>
#include <tchar.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <format>
#include <imgui_impl_dx12_custom.h>
#include <algorithm>
#include <windowsx.h>
#include <Utils.h>
#include <sstream>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

// Helper: Convert wide string to UTF-8 string (for logging)
static std::string WideToUTF8(const wchar_t* wstr)
{
    if (!wstr) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size_needed - 1, 0); // exclude null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size_needed, nullptr, nullptr);
    return result;
}

//===============================================================================
// Proper WndProc for ImGui platform windows | 🖱 Input Handling: WndProc Handler
//===============================================================================
LRESULT CALLBACK ImGuiPlatformWindow_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 1. Give ImGui the first shot at processing input
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    // 2. Real Win32 processing
    switch (msg)
    {
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    Logger::Log(LogLevel::Verbose,
        std::format("🪟 WndProc event msg=0x{:X} hwnd=0x{:X}", msg, (uintptr_t)hWnd));
}

// -----------------------------------------------------------------------------
// 📦 Static Variables
// -----------------------------------------------------------------------------
static HWND g_hWnd = nullptr;
static ATOM g_ImGuiPlatformWindowClass = 0;
static HINSTANCE g_hInstance = nullptr;
static int g_PlatformWindowsCreated = 0;

static const char* ImGui_ImplWin32_DebugMouseMessageName(UINT msg)
{
    switch (msg)
    {
    case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
    case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
    case WM_LBUTTONUP: return "WM_LBUTTONUP";
    case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
    case WM_RBUTTONUP: return "WM_RBUTTONUP";
    case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
    case WM_MBUTTONUP: return "WM_MBUTTONUP";
    case WM_XBUTTONDOWN: return "WM_XBUTTONDOWN";
    case WM_XBUTTONUP: return "WM_XBUTTONUP";
    case WM_CAPTURECHANGED: return "WM_CAPTURECHANGED";
    case WM_SETFOCUS: return "WM_SETFOCUS";
    case WM_KILLFOCUS: return "WM_KILLFOCUS";
    default: return "WM_UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// 📦 Custom platform backend data (Win32)
// -----------------------------------------------------------------------------
struct ImGui_ImplWin32_PlatformData
{
    HWND Hwnd = nullptr;
    double Time = 0.0;
    INT64 TicksPerSecond = 0;
    ImVec2 LastMousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    bool WantUpdateMonitors = true;
    bool WantUpdateDpi = true;
    int MouseButtonsDown = 0;
};

// -----------------------------------------------------------------------------
// 📦 Custom viewport backend data (Win32)
// -----------------------------------------------------------------------------
struct ImGui_ImplWin32_ViewportData
{
    HWND Hwnd = nullptr;
    bool WindowOwned = false;
    ImVec2 LastSize = ImVec2(0, 0);
    ImVec2 LastPos = ImVec2(0, 0);
    // Add more fields as needed for your backend
};


//====================================
// Imgui_ImpWin32_Int Function
//====================================
    bool ImGui_ImplWin32_Init(void* hwnd)
    {
        g_hWnd = static_cast<HWND>(hwnd);

        if (!IsWindow(g_hWnd)) {
            Logger::Log(LogLevel::Error, "❌ ImGui Win32 Init: Invalid HWND");
            return false;
        }

        ImGuiIO& io = ImGui::GetIO();

        // ✅ Allocate and initialize platform backend data
        auto* platform_data = new ImGui_ImplWin32_PlatformData();
        platform_data->Hwnd = g_hWnd;
        platform_data->Time = 0.0;
        platform_data->MouseButtonsDown = 0;
        QueryPerformanceFrequency((LARGE_INTEGER*)&platform_data->TicksPerSecond);

        io.BackendPlatformUserData = platform_data;
        io.BackendPlatformName = "imgui_impl_win32_custom";

        // ✅ Declare platform features
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
        io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;

        // ✅ Bind HWND and platform data to main viewport
        ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport)
        {
            main_viewport->PlatformHandle = g_hWnd;
            main_viewport->PlatformHandleRaw = g_hWnd;
            //main_viewport->PlatformUserData = platform_data;
        }
        else
        {
            Logger::Log(LogLevel::Error, "❌ Main viewport is null during ImGui_ImplWin32_Init!");
        }

        // ✅ Setup all required platform function callbacks
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        platform_io.Platform_CreateWindow = ImGui_ImplWin32_CreateWindow;
        platform_io.Platform_DestroyWindow = ImGui_ImplWin32_DestroyWindow;
        platform_io.Platform_ShowWindow = ImGui_ImplWin32_ShowWindow;
        platform_io.Platform_SetWindowPos = ImGui_ImplWin32_SetWindowPos;
        platform_io.Platform_GetWindowPos = ImGui_ImplWin32_GetWindowPos;
        platform_io.Platform_SetWindowSize = ImGui_ImplWin32_SetWindowSize;
        platform_io.Platform_GetWindowSize = ImGui_ImplWin32_GetWindowSize;
        platform_io.Platform_SetWindowFocus = ImGui_ImplWin32_SetWindowFocus;
        platform_io.Platform_GetWindowFocus = ImGui_ImplWin32_GetWindowFocus;
        platform_io.Platform_GetWindowMinimized = ImGui_ImplWin32_GetWindowMinimized;
        platform_io.Platform_SetWindowTitle = ImGui_ImplWin32_SetWindowTitle;
        platform_io.Platform_SetWindowAlpha = ImGui_ImplWin32_SetWindowAlpha;
        platform_io.Platform_GetWindowDpiScale = ImGui_ImplWin32_GetWindowDpiScale;
        platform_io.Platform_UpdateWindow = ImGui_ImplWin32_UpdateWindow;
        platform_io.Platform_RenderWindow = ImGui_ImplWin32_RenderWindow;
        platform_io.Platform_SwapBuffers = ImGui_ImplWin32_SwapBuffers;
        platform_io.Platform_OnChangedViewport = ImGui_ImplWin32_OnChangedViewport;

        // ✅ Detect monitor DPI / bounds for viewports
        ImGui_ImplWin32_UpdateMonitors();

        Logger::Log(LogLevel::Info, std::format("✅ ImGui Win32 backend initialized with HWND: 0x{:X}", (uintptr_t)g_hWnd));
        return true;
    }

//===================================
// ImGui_ImplWin32_GetClientSize
//===================================
static ImVec2 ImGui_ImplWin32_GetClientSize(HWND hwnd)
{
    RECT rect;
    ::GetClientRect(hwnd, &rect);
    return ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));
}

//===================================
// ImGui_ImplWin32_SanitizeViewportSize
//===================================
static ImVec2 SanitizeViewportSize(const ImVec2& size)
{
    ImVec2 safe_size = size;

    // Clamp to minimum safe size
    const float MIN_WIDTH = 64.0f;
    const float MIN_HEIGHT = 64.0f;
    const float MAX_SIZE = 10000.0f;

    // Check for NaN or extreme values
    if (_isnan(safe_size.x) || safe_size.x <= 0.0f || safe_size.x > MAX_SIZE)
        safe_size.x = 640.0f;
    else if (safe_size.x < MIN_WIDTH)
        safe_size.x = MIN_WIDTH;

    if (_isnan(safe_size.y) || safe_size.y <= 0.0f || safe_size.y > MAX_SIZE)
        safe_size.y = 480.0f;
    else if (safe_size.y < MIN_HEIGHT)
        safe_size.y = MIN_HEIGHT;

    // Optional: Log viewport size corrections
    if (safe_size.x != size.x || safe_size.y != size.y)
    {
        Logger::Log(LogLevel::Warning, std::format(
            "⚠️ Viewport size sanitized: original=({:.1f},{:.1f}) → safe=({:.1f},{:.1f})",
            size.x, size.y, safe_size.x, safe_size.y));
    }

    return safe_size;
}

//===================================
// ImGui_ImpWin32_Shutdown
//===================================
void ImGui_ImplWin32_Shutdown()
{
    Logger::Log(LogLevel::Info, "🔻 ImGui Win32 backend shutdown.");

    if (g_ImGuiPlatformWindowClass != 0)
    {
        UnregisterClass(_T("ImGui Platform"), g_hInstance);
        g_ImGuiPlatformWindowClass = 0;
    }

    ImGuiIO& io = ImGui::GetIO();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        for (int i = 0; i < platform_io.Viewports.Size; ++i)
        {
            ImGuiViewport* viewport = platform_io.Viewports[i];
            if (viewport)
            {
                if (viewport->PlatformUserData)
                {
                    Logger::Log(LogLevel::Debug, std::format("🧯 [Win32] Destroying viewport #{}...", i));
                    ImGui_ImplWin32_DestroyWindow(viewport); // Handles safe cleanup
                    Logger::Log(LogLevel::Debug, std::format("🧽 Viewport #{} platform window released.", i));
                }

                // ✅ Always clear pointer even if already null
                viewport->PlatformUserData = nullptr;
            }
        }
    }

    // 🧼 Clear main backend user data (only if not already destroyed)
    if (io.BackendPlatformUserData)
    {
        IM_DELETE(static_cast<ImGui_ImplWin32_PlatformData*>(io.BackendPlatformUserData));
        io.BackendPlatformUserData = nullptr;
    }

    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos);

    g_hWnd = nullptr;

    Logger::Log(LogLevel::Info, "✅ ImGui Win32 platform backend fully shut down.");
}


//==============================================
// New Frame Function: ImGui_ImplWin32_NewFrame
//==============================================
void ImGui_ImplWin32_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_PlatformData* bd = (ImGui_ImplWin32_PlatformData*)io.BackendPlatformUserData;

    if (!bd || !bd->Hwnd || !::IsWindow(bd->Hwnd))
    {
        Logger::Log(LogLevel::Error, "❌ ImGui_ImplWin32_NewFrame: BackendPlatformUserData or HWND is invalid.");
        return;
    }

    // 🕒 Calculate DeltaTime
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);

    double now = (double)current_time.QuadPart / (double)bd->TicksPerSecond;
    io.DeltaTime = (bd->Time > 0.0) ? (float)(now - bd->Time) : (1.0f / 60.0f); // Fallback: assume 60 FPS
    bd->Time = now;

    // 📐 Update display size
    RECT rect;
    GetClientRect(bd->Hwnd, &rect);
    io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    // IMPORTANT:
    // Keep framebuffer scale at 1.0 for this engine's rendering path.
    // DPI is handled via font/style scaling elsewhere; updating DisplayFramebufferScale here
    // causes ImGui to effectively double-scale on high DPI and can look "zoomed" on maximize.
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // 🖱️ Update mouse position in client space through the event queue API.
    ImVec2 mousePos(-FLT_MAX, -FLT_MAX);
    POINT pos;
    if (GetCursorPos(&pos) && ScreenToClient(bd->Hwnd, &pos))
        mousePos = ImVec2((float)pos.x, (float)pos.y);

    if (mousePos.x != bd->LastMousePos.x || mousePos.y != bd->LastMousePos.y)
    {
        io.AddMousePosEvent(mousePos.x, mousePos.y);
        if (bd->MouseButtonsDown != 0)
        {
            static int s_newFrameMouseTraceCounter = 0;
            if ((++s_newFrameMouseTraceCounter % 8) == 0)
            {
                Logger::Log(LogLevel::Verbose, std::format(
                    "🖱 NewFrame AddMousePosEvent hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({:.1f}, {:.1f}) last=({:.1f}, {:.1f})",
                    (uintptr_t)bd->Hwnd,
                    (uintptr_t)::GetCapture(),
                    bd->MouseButtonsDown,
                    mousePos.x,
                    mousePos.y,
                    bd->LastMousePos.x,
                    bd->LastMousePos.y));
            }
        }
        bd->LastMousePos = mousePos;
    }

    // 🩺 Throttled platform health log (helps diagnose "draws happen but nothing visible" by verifying platform state)
    {
        static int s_win32HealthCounter = 0;
        if ((++s_win32HealthCounter % 120) == 0)
        {
            ImGuiViewport* mainVp = ImGui::GetMainViewport();
            const HWND mainVpHwnd = mainVp ? (HWND)mainVp->PlatformHandle : nullptr;
            const HWND mainVpHwndRaw = mainVp ? (HWND)mainVp->PlatformHandleRaw : nullptr;

            const bool mainVpMatchesBackend = (mainVpHwnd == bd->Hwnd) && (mainVpHwndRaw == bd->Hwnd);

            std::ostringstream oss;
            oss << "🪟 Win32Health"
                << " | Frame=" << ImGui::GetFrameCount()
                << " DT=" << io.DeltaTime
                << " Display=" << (int)io.DisplaySize.x << "x" << (int)io.DisplaySize.y
                << " FBScale=" << io.DisplayFramebufferScale.x << "x" << io.DisplayFramebufferScale.y
                << " Hwnd=0x" << std::hex << (uintptr_t)bd->Hwnd << std::dec;

            if (mainVp)
            {
                oss << " MainVpID=0x" << std::hex << (UINT64)mainVp->ID << std::dec
                    << " MainVpHwnd=0x" << std::hex << (uintptr_t)mainVpHwnd << std::dec
                    << " MainVpHwndRaw=0x" << std::hex << (uintptr_t)mainVpHwndRaw << std::dec
                    << " MainVpSize=" << (int)mainVp->Size.x << "x" << (int)mainVp->Size.y
                    << " MainVpDpiScale=" << mainVp->DpiScale;
            }
            else
            {
                oss << " MainVpID=<null>";
            }

            oss << " VpHWNDMatch=" << (mainVpMatchesBackend ? "true" : "false")
                << " WantCapture(M,K)=" << (io.WantCaptureMouse ? "1" : "0") << "," << (io.WantCaptureKeyboard ? "1" : "0")
                << " WantTextInput=" << (io.WantTextInput ? "1" : "0")
                << " MousePos=" << (std::isfinite(io.MousePos.x) ? (int)io.MousePos.x : -99999) << "," << (std::isfinite(io.MousePos.y) ? (int)io.MousePos.y : -99999)
                << " MouseDown=" << (io.MouseDown[0] ? "1" : "0") << (io.MouseDown[1] ? "1" : "0") << (io.MouseDown[2] ? "1" : "0")
                << " Config(Viewports)=" << ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) ? "1" : "0")
                << " Backend(PlatformUserData)=" << (io.BackendPlatformUserData ? "set" : "null");

            Logger::Log(LogLevel::Info, oss.str(), "ImGuiWin32");

            if (mainVp && !mainVpMatchesBackend)
            {
                Logger::Log(LogLevel::Warning, std::format(
                    "⚠️ Win32Health: Main viewport HWND mismatch. BackendHWND=0x{:X} MainVpHWND=0x{:X} MainVpHWNDRaw=0x{:X}",
                    (uintptr_t)bd->Hwnd, (uintptr_t)mainVpHwnd, (uintptr_t)mainVpHwndRaw),
                    "ImGuiWin32");
            }
        }
    }
}

//=======================================================
// Multi-viewport support functions
//=======================================================
void ImGui_ImplWin32_CreateWindow(ImGuiViewport* viewport)
{
    static const wchar_t* kImGuiPlatformWindowClass = L"TheFletchZoneImGuiWndClass";
    static bool windowClassRegistered = false;

    if (!windowClassRegistered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = ImGuiPlatformWindow_WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = nullptr;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kImGuiPlatformWindowClass;

        ATOM classAtom = RegisterClassExW(&wc);
        if (!classAtom)
        {
            DWORD err = GetLastError();
            Logger::Log(LogLevel::Error,
                std::format("❌ Failed to register ImGui window class! Error={} HR=0x{:08X}",
                    err, HRESULT_FROM_WIN32(err)));
            return;
        }

        windowClassRegistered = true;
        Logger::Log(LogLevel::Info,
            std::format("✅ Registered ImGui Win32 window class: {}", WideToUTF8(kImGuiPlatformWindowClass)));
    }

    if (viewport->PlatformHandle != nullptr)
    {
        Logger::Log(LogLevel::Debug,
            std::format("⚠️ Window already exists for viewport 0x{:X}, skipping", viewport->ID));
        return;
    }

    ImVec2 size = viewport->Size;
    ImVec2 pos = viewport->Pos;

    // --- Sanitize width/height ---
    if (size.x <= 0.0f || size.x > 10000.0f || _isnan(size.x))
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ Viewport {} invalid width={} → default 640", viewport->ID, size.x));
        size.x = 640.0f;
    }
    if (size.y <= 0.0f || size.y > 10000.0f || _isnan(size.y))
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ Viewport {} invalid height={} → default 480", viewport->ID, size.y));
        size.y = 480.0f;
    }

    // Use real styles and pre-adjust outer size so requested client size is honored (prevents 32px client bugs)
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME);
    DWORD ex_style = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;

    RECT r = { 0, 0, (LONG)std::lround(size.x), (LONG)std::lround(size.y) };
    ::AdjustWindowRectEx(&r, style, FALSE, ex_style);
    int outerW = r.right - r.left;
    int outerH = r.bottom - r.top;

    HWND hwnd = ::CreateWindowExW(
        ex_style,
        kImGuiPlatformWindowClass,
        L"ImGui Platform Window",
        style,
        (int)pos.x, (int)pos.y,
        outerW, outerH,
        nullptr, nullptr,
        GetModuleHandle(nullptr), nullptr);

    if (!hwnd)
    {
        DWORD err = GetLastError();
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateWindowExW failed! Error={} HR=0x{:08X} (Viewport={} Size={}x{} Pos={}x{})",
                err, HRESULT_FROM_WIN32(err),
                viewport->ID, size.x, size.y, pos.x, pos.y));
        return;
    }

    Logger::Log(LogLevel::Info,
        std::format("🧱 ImGui Win32 window created: HWND=0x{:X} Size={}x{} Pos={}x{}",
            (uintptr_t)hwnd, (int)size.x, (int)size.y, (int)pos.x, (int)pos.y));

    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(viewport));
    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    viewport->PlatformHandle = hwnd;
    viewport->PlatformHandleRaw = hwnd;

    ImGui_ImplWin32_ViewportData* vd = IM_NEW(ImGui_ImplWin32_ViewportData)();
    vd->Hwnd = hwnd;
    viewport->PlatformUserData = vd;
}

//=========================================================
// Destroy Window Function
//=========================================================
void ImGui_ImplWin32_DestroyWindow(ImGuiViewport* viewport)
{
    if (!viewport)
        return;

    ImGuiIO& io = ImGui::GetIO();

    Logger::Log(LogLevel::Debug, std::format(
        "🧯 [Win32] Destroying Platform Window — Viewport ID={}, HWND=0x{:X}",
        viewport->ID, reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));

    // ✅ DO NOT call ::DestroyWindow here — ImGui handles that itself!
    // Leave it to ImGui Platform backend to call when it owns the HWND.

    // ✅ Prevent double delete (e.g., main viewport shares with BackendPlatformUserData)
    if (viewport->PlatformUserData && viewport->PlatformUserData != io.BackendPlatformUserData)
    {
        IM_DELETE(static_cast<ImGui_ImplWin32_ViewportData*>(viewport->PlatformUserData));
    }

    // ✅ Always null these to avoid use-after-free later
    viewport->PlatformUserData = nullptr;
    viewport->RendererUserData = nullptr;
    viewport->PlatformHandle = nullptr;
    viewport->PlatformHandleRaw = nullptr;

    Logger::Log(LogLevel::Debug, std::format(
        "🧽 [Win32] Platform Window ID={} fully destroyed and released.",
        viewport->ID));
}

//=====================================================
// Get DPI Scale for HWND Function
// =====================================================
float ImGui_ImplWin32_GetWindowDpiScale(ImGuiViewport* viewport)
{
    if (!viewport)
    {
        Logger::Log(LogLevel::Error, "❌ ImGui viewport is NULL in GetWindowDpiScale!");
        return 1.0f;
    }

    if (!viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ PlatformHandle is NULL — defaulting DPI scale to 1.0");
        return 1.0f;
    }

    float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(viewport->PlatformHandle);

    Logger::Log(LogLevel::Verbose, std::format("📐 DPI Scale for HWND=0x{:X} is {:.2f}",
        reinterpret_cast<uintptr_t>(viewport->PlatformHandle), scale));

    return scale;
}

//=====================================================
// Show Window Function
//=====================================================
void ImGui_ImplWin32_ShowWindow(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ Cannot show window: viewport or HWND is null.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

    Logger::Log(LogLevel::Info, std::format("👁️ Showing platform window: HWND=0x{:X}", (uintptr_t)hwnd));
    ::ShowWindow(hwnd, SW_SHOW);
}


//===================================================================
// Set Window Position Function (Safe + Loop-Protected)
//===================================================================
void ImGui_ImplWin32_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning,
            "⚠️ SetWindowPos: viewport or PlatformHandle is null.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ SetWindowPos: Invalid HWND for ViewportID=0x{:X}", viewport->ID));
        return;
    }

    // ---------------------------------------------------------------
    // Reject bogus / uninitialized coordinates (happens on creation)
    // ---------------------------------------------------------------
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y))
    {
        Logger::Log(LogLevel::Trace,
            std::format("↩️ Ignoring SetWindowPos with NaN ({:.2f}, {:.2f})", pos.x, pos.y));
        return;
    }

    // Tiny negative values or +inf can appear during monitor transitions.
    if (pos.x < -30000 || pos.x > 30000 ||
        pos.y < -30000 || pos.y > 30000)
    {
        Logger::Log(LogLevel::Trace,
            std::format("↩️ Ignoring extreme SetWindowPos({:.2f}, {:.2f})", pos.x, pos.y));
        return;
    }

    // ---------------------------------------------------------------
    // Read current window position (screen coords)
    // ---------------------------------------------------------------
    RECT rect{};
    ::GetWindowRect(hwnd, &rect);
    int curX = rect.left;
    int curY = rect.top;

    // ---------------------------------------------------------------
    // Prevent redundant SetWindowPos calls (loop protection)
    // ImGui calls SetWindowPos *every frame*, so avoid flicker.
    // ---------------------------------------------------------------
    if (std::abs(curX - (int)pos.x) <= 1 &&
        std::abs(curY - (int)pos.y) <= 1)
    {
        Logger::Log(LogLevel::Trace,
            std::format("⏭️ Skipping SetWindowPos — window already at ({}, {})",
                curX, curY));
        return;
    }

    // ---------------------------------------------------------------
    // Apply the position safely
    // ---------------------------------------------------------------
    BOOL ok = ::SetWindowPos(
        hwnd, nullptr,
        (int)pos.x, (int)pos.y,
        0, 0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE
    );

    if (!ok)
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ Failed to position HWND=0x{:X} to ({}, {})",
                (uintptr_t)hwnd, (int)pos.x, (int)pos.y));
    }
    else
    {
        Logger::Log(LogLevel::Debug,
            std::format("📍 Positioned HWND=0x{:X} to ({}, {})",
                (uintptr_t)hwnd, (int)pos.x, (int)pos.y));
    }
}



//===============================================================
// Get Window Postition Function 
//===============================================================
ImVec2 ImGui_ImplWin32_GetWindowPos(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetWindowPos failed: null viewport or HWND.");
        return ImVec2(0, 0);
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    RECT rect = {};
    if (::GetWindowRect(hwnd, &rect))
    {
        Logger::Log(LogLevel::Debug, std::format("📍 HWND=0x{:X} position: ({}, {})", (uintptr_t)hwnd, rect.left, rect.top));
        return ImVec2((float)rect.left, (float)rect.top);
    }
    else
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to get window rect for HWND=0x{:X}", (uintptr_t)hwnd));
        return ImVec2(0, 0);
    }
}


//=====================================================================
// GetWindowRect (TheFletchZone — Fully Sanitized Edition)
//=====================================================================
void ImGui_ImplWin32_GetWindowRect(ImGuiViewport* viewport, ImVec2* out_pos, ImVec2* out_size)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetWindowRect: Null viewport or HWND.");
        *out_pos = ImVec2(0, 0);
        *out_size = ImVec2(1280, 720);
        return;
    }

    HWND hwnd = (HWND)viewport->PlatformHandle;

    RECT rect{};
    if (!::GetWindowRect(hwnd, &rect))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ GetWindowRect failed for HWND=0x{:X}", (uintptr_t)hwnd));

        *out_pos = ImVec2(0, 0);
        *out_size = ImVec2(1280, 720);
        return;
    }

    float w = (float)(rect.right - rect.left);
    float h = (float)(rect.bottom - rect.top);

    // ---------------------------------------------------------
    // 🔥 SANITY CHECK: Windows sometimes reports garbage (32 px)
    // ---------------------------------------------------------
    if (!std::isfinite(w) || w < 64.0f || w > 8192.0f)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ GetWindowRect reported INVALID WIDTH {} → Clamping to 128",
                w));
        w = 128.0f;
    }

    if (!std::isfinite(h) || h < 64.0f || h > 8192.0f)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ GetWindowRect reported INVALID HEIGHT {} → Clamping to 720",
                h));
        h = 720.0f;
    }

    *out_pos = ImVec2((float)rect.left, (float)rect.top);
    *out_size = ImVec2(w, h);

    Logger::Log(LogLevel::Debug,
        std::format("📐 GetWindowRect: HWND=0x{:X} Pos=({}, {}) Size=({}x{})",
            (uintptr_t)hwnd, rect.left, rect.top, (int)w, (int)h));
}


//=====================================================================
// SetWindowSize — FINAL FIXED VERSION (ImGui-safe, DX12-safe)
//=====================================================================
void ImGui_ImplWin32_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning,
            "⚠️ SetWindowSize skipped — null viewport or HWND.");
        return;
    }

    HWND hwnd = (HWND)viewport->PlatformHandle;
    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ SetWindowSize: Invalid HWND (ViewportID=0x{:X})",
                viewport->ID));
        return;
    }

    //-------------------------------------------------------------------------
    // 1. Read requested size (DO NOT TOUCH viewport->Size)
    //-------------------------------------------------------------------------
    float reqW = size.x;
    float reqH = size.y;

    //-------------------------------------------------------------------------
    // 2. Sanitize requested values ONLY
    //-------------------------------------------------------------------------
    constexpr float MIN_SIZE = 64.0f;
    constexpr float MAX_SIZE = 8192.0f;

    if (!std::isfinite(reqW) || reqW < MIN_SIZE || reqW > MAX_SIZE)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ Invalid requested width {:.2f} → clamping to 1280", reqW));
        reqW = 1280.0f;
    }

    if (!std::isfinite(reqH) || reqH < MIN_SIZE || reqH > MAX_SIZE)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ Invalid requested height {:.2f} → clamping to 720", reqH));
        reqH = 720.0f;
    }

    //-------------------------------------------------------------------------
    // 3. Avoid resize loops (compare CLIENT size)
    //-------------------------------------------------------------------------
    RECT cur{};
    ::GetClientRect(hwnd, &cur);

    int curW = cur.right - cur.left;
    int curH = cur.bottom - cur.top;

    if (abs(curW - (int)reqW) <= 1 && abs(curH - (int)reqH) <= 1)
    {
        Logger::Log(LogLevel::Trace,
            std::format("↩ SetWindowSize skipped — already {}x{} (requested {}x{})",
                curW, curH, (int)reqW, (int)reqH));
        return;
    }

    //-------------------------------------------------------------------------
    // 4. Compute final window rect (DPI + borders)
    //-------------------------------------------------------------------------
    RECT rect = { 0, 0, (int)reqW, (int)reqH };

    DWORD style = (DWORD)::GetWindowLongPtrW(hwnd, GWL_STYLE);
    DWORD exStyle = (DWORD)::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    int finalW = rect.right - rect.left;
    int finalH = rect.bottom - rect.top;

    Logger::Log(LogLevel::Debug,
        std::format("📏 SetWindowSize: HWND=0x{:X} | Client={}x{} → Window={}x{}",
            (uintptr_t)hwnd, (int)reqW, (int)reqH, finalW, finalH));

    //-------------------------------------------------------------------------
    // 5. Apply resize (no move, no z-order)
    //-------------------------------------------------------------------------
    if (!::SetWindowPos(hwnd, nullptr, 0, 0,
        finalW, finalH,
        SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ SetWindowPos failed on HWND=0x{:X}",
                (uintptr_t)hwnd));
        return;
    }

    //-------------------------------------------------------------------------
    // 6. Force redraw
    //-------------------------------------------------------------------------
    ::InvalidateRect(hwnd, nullptr, TRUE);

    Logger::Log(LogLevel::Verbose,
        std::format("✅ Window resized to {}x{} (ViewportID=0x{:X})",
            finalW, finalH, viewport->ID));
}

//===========================================================
// Get Window Size Function (READ-ONLY, No Loop Risk, TFZ-Safe)
//===========================================================
ImVec2 ImGui_ImplWin32_GetWindowSize(ImGuiViewport* viewport)
{
    if (!viewport)
    {
        Logger::Log(LogLevel::Warning,
            "⚠️ GetWindowSize failed: null viewport.");
        return ImVec2(1280, 720);
    }

    if (!viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning,
            "⚠️ GetWindowSize failed: null HWND.");
        return ImVec2(1280, 720);
    }

    HWND hwnd = (HWND)viewport->PlatformHandle;
    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ GetWindowSize: Invalid HWND=0x{:X}",
                (uintptr_t)hwnd));
        return ImVec2(1280, 720);
    }

    RECT rect{};
    if (!::GetClientRect(hwnd, &rect))
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ GetClientRect failed for HWND=0x{:X}",
                (uintptr_t)hwnd));
        return ImVec2(1280, 720);
    }

    // Raw client size
    float w = (float)(rect.right - rect.left);
    float h = (float)(rect.bottom - rect.top);

    // --------------------------------------------------------------------
    // Clamp insane values but DO NOT write viewport->Size (ImGui owns it)
    // --------------------------------------------------------------------
    const float MIN_SIZE = 64.0f;
    const float MAX_SIZE = 8192.0f;

    if (!std::isfinite(w) || w < MIN_SIZE || w > MAX_SIZE)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ GetWindowSize invalid width {} → 1280", w));
        w = 1280.0f;
    }

    if (!std::isfinite(h) || h < MIN_SIZE || h > MAX_SIZE)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ GetWindowSize invalid height {} → 720", h));
        h = 720.0f;
    }

    Logger::Log(LogLevel::Debug,
        std::format("📐 GetWindowSize: HWND=0x{:X}, client={}x{}",
            (uintptr_t)hwnd, (int)w, (int)h));

    // -----------------------------------------------------
    // RETURN only — DO NOT modify viewport->Size ever here.
    // -----------------------------------------------------
    return ImVec2(w, h);
}

//==========================================================
// Set Window Focus Function
//==========================================================
void ImGui_ImplWin32_SetWindowFocus(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ SetWindowFocus failed: null viewport or HWND.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ SetWindowFocus skipped: HWND=0x{:X} is not a valid window.", reinterpret_cast<uintptr_t>(hwnd)));
        return;
    }

    if (!::IsWindowVisible(hwnd) || !::IsWindowEnabled(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ SetWindowFocus skipped: HWND=0x{:X} is not visible or not enabled.", reinterpret_cast<uintptr_t>(hwnd)));
        return;
    }

    Logger::Log(LogLevel::Info, std::format("🎯 Setting focus to HWND=0x{:X}", reinterpret_cast<uintptr_t>(hwnd)));
    ::SetFocus(hwnd);
}


//==========================================================
// Get Window Focus Function
//==========================================================
bool ImGui_ImplWin32_GetWindowFocus(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetWindowFocus failed: null viewport or HWND.");
        return false;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    HWND fg = GetForegroundWindow();

    bool focused = (fg == hwnd);

    Logger::Log(LogLevel::Debug, std::format(
        "🔍 Window Focus Check — HWND=0x{:X}, Foreground=0x{:X}, Focused={}",
        reinterpret_cast<uintptr_t>(hwnd),
        reinterpret_cast<uintptr_t>(fg),
        focused ? "true" : "false"
    ));

    return focused;
}


//===============================================================
// Get Window Minimized Function
//===============================================================
bool ImGui_ImplWin32_GetWindowMinimized(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetWindowMinimized failed: viewport or HWND is null.");
        return false;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    bool minimized = IsIconic(hwnd);

    Logger::Log(LogLevel::Debug, std::format(
        "🔍 Window Minimized Check — HWND=0x{:X}, Minimized={}",
        reinterpret_cast<uintptr_t>(hwnd),
        minimized ? "true" : "false"
    ));

    return minimized;
}


//===============================================================
// Set Window Title
//===============================================================
void ImGui_ImplWin32_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
    if (!viewport || !viewport->PlatformHandle || !title)
    {
        Logger::Log(LogLevel::Warning, "⚠️ SetWindowTitle failed: invalid viewport or title.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ Invalid HWND for SetWindowTitle: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd)));
        return;
    }

    Logger::Log(LogLevel::Debug, std::format("🏷️ Setting window title: '{}'", title));
    SetWindowTextA(hwnd, title);
}


///===============================================================
// Set Window Alpha Function
//===============================================================
void ImGui_ImplWin32_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ SetWindowAlpha failed: viewport or PlatformHandle is null.");
        return;
    }

    HWND hwnd = (HWND)viewport->PlatformHandle;
    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ Invalid HWND in SetWindowAlpha: 0x{:X}", (uintptr_t)hwnd));
        return;
    }

    alpha = std::clamp(alpha, 0.0f, 1.0f);

    LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (!(style & WS_EX_LAYERED))
    {
        SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        Logger::Log(LogLevel::Debug, "✨ WS_EX_LAYERED style enabled for ImGui window");
    }

    BYTE alphaByte = static_cast<BYTE>(alpha * 255.0f);
    BOOL success = SetLayeredWindowAttributes(hwnd, 0, alphaByte, LWA_ALPHA);
    if (!success)
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to set window alpha: alpha = {}, HWND = 0x{:X}", alpha, (uintptr_t)hwnd));
    }
    else
    {
        Logger::Log(LogLevel::Debug, std::format("🫧 Window alpha set to {:.2f} ({}%)", alpha, (int)(alpha * 100)));
    }
}

//=================================================================
// Update Window Function 
//=================================================================
void ImGui_ImplWin32_UpdateWindow(ImGuiViewport* viewport)
{
    if (!viewport || !viewport->PlatformHandle)
    {
        Logger::Log(LogLevel::Warning, "⚠️ UpdateWindow called with null viewport or PlatformHandle.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);

    if (!::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ UpdateWindow called with invalid HWND: 0x{:X}", reinterpret_cast<uintptr_t>(hwnd)));
        return;
    }

    Logger::Log(LogLevel::Info, std::format("🔄 InvalidateRect called for HWND=0x{:X}", reinterpret_cast<uintptr_t>(hwnd)));
    InvalidateRect(hwnd, nullptr, FALSE);
}


//===================================================================
// OnChangedViewport — handle DPI and monitor adjustments
//===================================================================
void ImGui_ImplWin32_OnChangedViewport(ImGuiViewport* viewport)
{
    if (!viewport)
    {
        Logger::Log(LogLevel::Warning, "⚠️ OnChangedViewport called with null viewport.");
        return;
    }

    // ✅ Guard: PlatformHandle must be valid
    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    if (!viewport || !viewport->PlatformHandle || !::IsWindow((HWND)viewport->PlatformHandle))
    {
        Logger::Log(LogLevel::Debug,
            std::format("⛔ Skipping OnChangedViewport — Invalid HWND (0x{:X})",
                reinterpret_cast<uintptr_t>(viewport ? viewport->PlatformHandle : 0)));
        return;
    }

    Logger::Log(LogLevel::Info, std::format("🔄 OnChangedViewport triggered for Viewport ID={} | HWND=0x{:X}", viewport->ID, (uintptr_t)hwnd));

    // ✅ Get the DPI for the monitor the window is now on
    UINT dpiX = 96, dpiY = 96;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor)
    {
        HRESULT hr = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (SUCCEEDED(hr))
        {
            float newScale = dpiX / 96.0f;
            if (viewport->DpiScale != newScale)
            {
                Logger::Log(LogLevel::Info, std::format("🖥️ Monitor DPI changed: {} → {} | Scale: {:.2f}", (int)(viewport->DpiScale * 96.0f), dpiX, newScale));
                viewport->DpiScale = newScale;
            }
            else
            {
                Logger::Log(LogLevel::Debug, std::format("📏 DPI unchanged | DPI={} Scale={:.2f}", dpiX, viewport->DpiScale));
            }
        }
        else
        {
            Logger::Log(LogLevel::Warning, "⚠️ Failed to get DPI for monitor. Defaulting scale to 1.0");
            viewport->DpiScale = 1.0f;
        }
    }

    // 🔄 Recalculate monitor list + work areas
    ImGui_ImplWin32_UpdateMonitors();
}

//=====================================================================
// Render Window Function
//=====================================================================
void ImGui_ImplWin32_RenderWindow(ImGuiViewport* viewport, void*)
{
    if (!viewport)
    {
        Logger::Log(LogLevel::Warning, "⚠️ ImGui_ImplWin32_RenderWindow called with null viewport.");
        return;
    }

    HWND hwnd = static_cast<HWND>(viewport->PlatformHandle);
    if (!hwnd || !::IsWindow(hwnd))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ Cannot render window — invalid HWND (0x{:X})", (uintptr_t)hwnd));
        return;
    }

    // DX12 handles rendering in ImGui_ImplDX12_RenderWindow, so we log and exit safely.
    Logger::Log(LogLevel::Verbose, std::format("🎮 ImGui Win32 RenderWindow pass-through: HWND=0x{:X}", (uintptr_t)hwnd));
}

//=====================================================================
// Swap Buffers Function
//=====================================================================
void ImGui_ImplWin32_SwapBuffers(ImGuiViewport* viewport, void*)
{
    if (viewport == nullptr)
        return;

    HWND hwnd = (HWND)viewport->PlatformHandle;
    if (hwnd && IsWindow(hwnd))
    {
        HDC hdc = GetDC(hwnd);
        if (hdc)
        {
            ::SwapBuffers(hdc);
            ReleaseDC(hwnd, hdc);
        }
    }
}


//=====================================================
// DPI Utilities
//=====================================================
float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd)
{
    if (!hwnd)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetDpiScaleForHwnd: NULL hwnd provided. Returning default scale = 1.0");
        return 1.0f;
    }

    HMONITOR monitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetDpiScaleForHwnd: Failed to get HMONITOR from HWND. Returning default scale = 1.0");
        return 1.0f;
    }

    UINT dpiX = 96, dpiY = 96;
    HRESULT hr = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ GetDpiForMonitor failed (HRESULT=0x{:08X}). Returning default scale = 1.0", (UINT)hr));
        return 1.0f;
    }

    float scale = static_cast<float>(dpiX) / 96.0f;
    Logger::Log(LogLevel::Verbose, std::format("📐 DPI Scale for HWND=0x{:X} => {} (dpiX = {})", reinterpret_cast<uintptr_t>(hwnd), scale, dpiX));
    return scale;
}

//=====================================================
// Get DPI Scale For Monitor Function
//=====================================================
static float ImGui_ImplWin32_GetDpiScaleForMonitor(void* monitor)
{
    if (!monitor)
    {
        Logger::Log(LogLevel::Warning, "⚠️ GetDpiScaleForMonitor: NULL monitor handle. Returning default scale = 1.0");
        return 1.0f;
    }

    UINT dpiX = 96, dpiY = 96;
    HRESULT hr = GetDpiForMonitor((HMONITOR)monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ GetDpiForMonitor failed (HRESULT=0x{:08X}). Returning default scale = 1.0", (UINT)hr));
        return 1.0f;
    }

    float scale = static_cast<float>(dpiX) / 96.0f;
    Logger::Log(LogLevel::Verbose, std::format("📐 DPI Scale for monitor {} => {} (dpiX = {})", (uintptr_t)monitor, scale, dpiX));
    return scale;
}


//=========================
// Function declaration
//=========================

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM dwData);

//=====================================
// Update Monitors Function
//=====================================
void ImGui_ImplWin32_UpdateMonitors()
{
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Monitors.clear();

    Logger::Log(LogLevel::Info, "🖥️ Enumerating connected display monitors...");

    if (!EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&platform_io)))
    {
        Logger::Log(LogLevel::Error, "❌ EnumDisplayMonitors failed during monitor update.");
    }
    else
    {
        Logger::Log(LogLevel::Info, std::format("✅ {} monitor(s) detected and updated.", platform_io.Monitors.Size));
    }
}


    //=================================================================================
    // Monitor Enum Proc Function 
    //=================================================================================
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData)
    {
        ImGuiPlatformIO* platform_io = reinterpret_cast<ImGuiPlatformIO*>(dwData);
        if (!platform_io || !lprcMonitor || !hMonitor)
        {
            Logger::Log(LogLevel::Warning, "⚠️ Invalid parameters passed to MonitorEnumProc.");
            return TRUE;
        }

        MONITORINFOEX monitor_info = {};
        monitor_info.cbSize = sizeof(MONITORINFOEX);
        if (!GetMonitorInfo(hMonitor, &monitor_info))
        {
            Logger::Log(LogLevel::Warning, "⚠️ GetMonitorInfo failed for monitor.");
            return TRUE;
        }

        ImGuiPlatformMonitor monitor;
        monitor.MainPos = ImVec2((float)monitor_info.rcMonitor.left, (float)monitor_info.rcMonitor.top);
        monitor.MainSize = ImVec2((float)(monitor_info.rcMonitor.right - monitor_info.rcMonitor.left),
            (float)(monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top));
        monitor.WorkPos = ImVec2((float)monitor_info.rcWork.left, (float)monitor_info.rcWork.top);
        monitor.WorkSize = ImVec2((float)(monitor_info.rcWork.right - monitor_info.rcWork.left),
            (float)(monitor_info.rcWork.bottom - monitor_info.rcWork.top));
        monitor.DpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(hMonitor);
        monitor.PlatformHandle = hMonitor;

        platform_io->Monitors.push_back(monitor);

        Logger::Log(LogLevel::Debug, std::format(
            "🖥️ Monitor: {}x{} at ({},{}) | DPI Scale: {:.2f}",
            (int)monitor.MainSize.x, (int)monitor.MainSize.y,
            (int)monitor.MainPos.x, (int)monitor.MainPos.y,
            monitor.DpiScale
        ));

        return TRUE;
    }


//==========================================
// Enable Dpi Awareness Function
//==========================================
void ImGui_ImplWin32_EnableDpiAwareness()
{
    bool success = false;

    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore)
    {
        typedef HRESULT(WINAPI* SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);
        auto setProcessDpiAwareness = (SetProcessDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (setProcessDpiAwareness)
        {
            HRESULT hr = setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            if (SUCCEEDED(hr))
            {
                Logger::Log(LogLevel::Info, "🧠 DPI Awareness set: Per-Monitor (via SetProcessDpiAwareness)");
                success = true;
            }
            else
            {
                Logger::Log(LogLevel::Warning, std::format("⚠️ SetProcessDpiAwareness failed: HRESULT=0x{:08X}", (UINT)hr));
            }
        }
        else
        {
            Logger::Log(LogLevel::Warning, "⚠️ SetProcessDpiAwareness not found in shcore.dll.");
        }

        FreeLibrary(shcore);
    }

    // Fallback to legacy DPI awareness (Windows 7)
    if (!success)
    {
        if (SetProcessDPIAware())
            Logger::Log(LogLevel::Info, "🧠 DPI Awareness set: System DPI (via SetProcessDPIAware)");
        else
            Logger::Log(LogLevel::Error, "❌ SetProcessDPIAware failed.");
    }
}

//==========================================================
// Enable Alpha Compositing Function
//==========================================================
void ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd)
{
    if (!hwnd)
    {
        Logger::Log(LogLevel::Error, "❌ HWND is null — cannot enable alpha compositing.");
        return;
    }

    BOOL compositionEnabled = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&compositionEnabled)))
    {
        Logger::Log(LogLevel::Warning, "⚠️ DwmIsCompositionEnabled failed.");
        return;
    }

    if (!compositionEnabled)
    {
        Logger::Log(LogLevel::Warning, "⚠️ DWM composition is not enabled on this system.");
        return;
    }

    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = TRUE;
    bb.hRgnBlur = nullptr;

    HRESULT hr = DwmEnableBlurBehindWindow((HWND)hwnd, &bb);
    if (SUCCEEDED(hr))
    {
        Logger::Log(LogLevel::Info, "✨ Alpha compositing (blur behind) enabled for ImGui window.");
    }
    else
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to enable blur behind window: HRESULT=0x{:08X}", (UINT)hr));
    }
}

//==========================================================
// Update Mouse Cursor Function
//==========================================================
static void ImGui_ImplWin32_UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
    {
        Logger::Log(LogLevel::Verbose, "🖱️ Skipping cursor update due to NoMouseCursorChange flag.");
        return;
    }

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        Logger::Log(LogLevel::Verbose, "🖱️ Hiding system cursor.");
        SetCursor(nullptr);
        return;
    }

    // Map ImGui cursor to Win32 cursor ID
    LPCTSTR win_cursor_id = IDC_ARROW;
    switch (imgui_cursor)
    {
    case ImGuiMouseCursor_Arrow:        win_cursor_id = IDC_ARROW; break;
    case ImGuiMouseCursor_TextInput:    win_cursor_id = IDC_IBEAM; break;
    case ImGuiMouseCursor_ResizeAll:    win_cursor_id = IDC_SIZEALL; break;
    case ImGuiMouseCursor_ResizeEW:     win_cursor_id = IDC_SIZEWE; break;
    case ImGuiMouseCursor_ResizeNS:     win_cursor_id = IDC_SIZENS; break;
    case ImGuiMouseCursor_ResizeNESW:   win_cursor_id = IDC_SIZENESW; break;
    case ImGuiMouseCursor_ResizeNWSE:   win_cursor_id = IDC_SIZENWSE; break;
    case ImGuiMouseCursor_Hand:         win_cursor_id = IDC_HAND; break;
    case ImGuiMouseCursor_NotAllowed:   win_cursor_id = IDC_NO; break;
    }

    HCURSOR hCursor = LoadCursor(nullptr, win_cursor_id);
    if (hCursor)
    {
        SetCursor(hCursor);
        Logger::Log(LogLevel::Verbose, std::format("🖱️ Cursor updated to {}", imgui_cursor));
    }
    else
    {
        Logger::Log(LogLevel::Warning, std::format("⚠️ Failed to load cursor ID: {}", (int)imgui_cursor));
    }
}

//================================================================
// WndProc Function: ImGui_ImplWin32_WndProcHandler
//================================================================

static void ImGui_ImplWin32_UpdateKeyModifiers(ImGuiIO& io)
{
    io.AddKeyEvent(ImGuiMod_Ctrl,  (::GetKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (::GetKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt,   (::GetKeyState(VK_MENU) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (::GetKeyState(VK_LWIN) & 0x8000) != 0 || (::GetKeyState(VK_RWIN) & 0x8000) != 0);
}

static ImGuiKey ImGui_ImplWin32_VkToImGuiKey(WPARAM vk)
{
    switch (vk)
    {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;

    case VK_NUMPAD0: return ImGuiKey_Keypad0;
    case VK_NUMPAD1: return ImGuiKey_Keypad1;
    case VK_NUMPAD2: return ImGuiKey_Keypad2;
    case VK_NUMPAD3: return ImGuiKey_Keypad3;
    case VK_NUMPAD4: return ImGuiKey_Keypad4;
    case VK_NUMPAD5: return ImGuiKey_Keypad5;
    case VK_NUMPAD6: return ImGuiKey_Keypad6;
    case VK_NUMPAD7: return ImGuiKey_Keypad7;
    case VK_NUMPAD8: return ImGuiKey_Keypad8;
    case VK_NUMPAD9: return ImGuiKey_Keypad9;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;

    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;

    case '0': return ImGuiKey_0;
    case '1': return ImGuiKey_1;
    case '2': return ImGuiKey_2;
    case '3': return ImGuiKey_3;
    case '4': return ImGuiKey_4;
    case '5': return ImGuiKey_5;
    case '6': return ImGuiKey_6;
    case '7': return ImGuiKey_7;
    case '8': return ImGuiKey_8;
    case '9': return ImGuiKey_9;

    case 'A': return ImGuiKey_A;
    case 'B': return ImGuiKey_B;
    case 'C': return ImGuiKey_C;
    case 'D': return ImGuiKey_D;
    case 'E': return ImGuiKey_E;
    case 'F': return ImGuiKey_F;
    case 'G': return ImGuiKey_G;
    case 'H': return ImGuiKey_H;
    case 'I': return ImGuiKey_I;
    case 'J': return ImGuiKey_J;
    case 'K': return ImGuiKey_K;
    case 'L': return ImGuiKey_L;
    case 'M': return ImGuiKey_M;
    case 'N': return ImGuiKey_N;
    case 'O': return ImGuiKey_O;
    case 'P': return ImGuiKey_P;
    case 'Q': return ImGuiKey_Q;
    case 'R': return ImGuiKey_R;
    case 'S': return ImGuiKey_S;
    case 'T': return ImGuiKey_T;
    case 'U': return ImGuiKey_U;
    case 'V': return ImGuiKey_V;
    case 'W': return ImGuiKey_W;
    case 'X': return ImGuiKey_X;
    case 'Y': return ImGuiKey_Y;
    case 'Z': return ImGuiKey_Z;

    case VK_F1: return ImGuiKey_F1;
    case VK_F2: return ImGuiKey_F2;
    case VK_F3: return ImGuiKey_F3;
    case VK_F4: return ImGuiKey_F4;
    case VK_F5: return ImGuiKey_F5;
    case VK_F6: return ImGuiKey_F6;
    case VK_F7: return ImGuiKey_F7;
    case VK_F8: return ImGuiKey_F8;
    case VK_F9: return ImGuiKey_F9;
    case VK_F10: return ImGuiKey_F10;
    case VK_F11: return ImGuiKey_F11;
    case VK_F12: return ImGuiKey_F12;
    default: return ImGuiKey_None;
    }
}

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!ImGui::GetCurrentContext())
        return 0;

    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_PlatformData* bd = static_cast<ImGui_ImplWin32_PlatformData*>(io.BackendPlatformUserData);

    switch (msg)
    {
        // =========================
        // 🎡 Mouse Wheel Handling
        // =========================
    case WM_MOUSEWHEEL:
    {
        float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        io.AddMouseWheelEvent(0.0f, delta);
        return 0;
    }
    case WM_MOUSEHWHEEL:
    {
        float delta = -(float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        io.AddMouseWheelEvent(delta, 0.0f);
        return 0;
    }

    // =========================
    // 🖱 Mouse Button Handling
    // =========================
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    {
        int button = 0;
        if (msg == WM_LBUTTONDOWN)      button = 0;
        else if (msg == WM_RBUTTONDOWN) button = 1;
        else if (msg == WM_MBUTTONDOWN) button = 2;
        else if (msg == WM_XBUTTONDOWN) button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4);

        io.AddMouseButtonEvent(button, true);
        if (bd)
            bd->MouseButtonsDown |= (1 << button);
        Logger::Log(LogLevel::Verbose, std::format(
            "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
            ImGui_ImplWin32_DebugMouseMessageName(msg),
            (uintptr_t)hWnd,
            (uintptr_t)::GetCapture(),
            bd ? bd->MouseButtonsDown : 0,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            (uintptr_t)wParam,
            (uintptr_t)lParam));
        return 0;
    }

    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    {
        int button = 0;
        if (msg == WM_LBUTTONUP)      button = 0;
        else if (msg == WM_RBUTTONUP) button = 1;
        else if (msg == WM_MBUTTONUP) button = 2;
        else if (msg == WM_XBUTTONUP) button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4);

        io.AddMouseButtonEvent(button, false);
        if (bd)
            bd->MouseButtonsDown &= ~(1 << button);
        Logger::Log(LogLevel::Verbose, std::format(
            "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
            ImGui_ImplWin32_DebugMouseMessageName(msg),
            (uintptr_t)hWnd,
            (uintptr_t)::GetCapture(),
            bd ? bd->MouseButtonsDown : 0,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            (uintptr_t)wParam,
            (uintptr_t)lParam));
        return 0;
    }

    case WM_CAPTURECHANGED:
        Logger::Log(LogLevel::Verbose, std::format(
            "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
            ImGui_ImplWin32_DebugMouseMessageName(msg),
            (uintptr_t)hWnd,
            (uintptr_t)::GetCapture(),
            bd ? bd->MouseButtonsDown : 0,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            (uintptr_t)wParam,
            (uintptr_t)lParam));
        if (bd)
            bd->MouseButtonsDown = 0;
        return 0;

        // =========================
        // 🎯 Mouse Position Tracking
        // =========================
    case WM_MOUSEMOVE:
    {
        const float x = (float)GET_X_LPARAM(lParam);
        const float y = (float)GET_Y_LPARAM(lParam);
        io.AddMousePosEvent(x, y);
        if (bd)
        {
            bd->LastMousePos = ImVec2(x, y);
            if (bd->MouseButtonsDown != 0)
            {
                static int s_wndProcMouseMoveTraceCounter = 0;
                if ((++s_wndProcMouseMoveTraceCounter % 8) == 0)
                {
                    Logger::Log(LogLevel::Verbose, std::format(
                        "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
                        ImGui_ImplWin32_DebugMouseMessageName(msg),
                        (uintptr_t)hWnd,
                        (uintptr_t)::GetCapture(),
                        bd->MouseButtonsDown,
                        GET_X_LPARAM(lParam),
                        GET_Y_LPARAM(lParam),
                        (uintptr_t)wParam,
                        (uintptr_t)lParam));
                }
            }
        }
        return 0;
    }

    // =========================
    // 🎹 Keyboard Input
    // =========================
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        ImGui_ImplWin32_UpdateKeyModifiers(io);
        const ImGuiKey key = ImGui_ImplWin32_VkToImGuiKey(wParam);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, true);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        ImGui_ImplWin32_UpdateKeyModifiers(io);
        const ImGuiKey key = ImGui_ImplWin32_VkToImGuiKey(wParam);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, false);
        return 0;
    }

    case WM_CHAR:
        if (wParam > 0 && wParam < 0x10000)
        {
            io.AddInputCharacterUTF16((ImWchar)wParam);
            return 0;
        }
        break;

        // =========================
        // 🧠 Window Focus Events
        // =========================
    case WM_SETFOCUS:
        io.AddFocusEvent(true);
        Logger::Log(LogLevel::Verbose, std::format(
            "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
            ImGui_ImplWin32_DebugMouseMessageName(msg),
            (uintptr_t)hWnd,
            (uintptr_t)::GetCapture(),
            bd ? bd->MouseButtonsDown : 0,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            (uintptr_t)wParam,
            (uintptr_t)lParam));
        return 0;

    case WM_KILLFOCUS:
        io.AddFocusEvent(false);
        Logger::Log(LogLevel::Verbose, std::format(
            "🖱 WndProc {} hwnd=0x{:X} capture=0x{:X} buttons=0x{:X} pos=({}, {}) wParam=0x{:X} lParam=0x{:X}",
            ImGui_ImplWin32_DebugMouseMessageName(msg),
            (uintptr_t)hWnd,
            (uintptr_t)::GetCapture(),
            bd ? bd->MouseButtonsDown : 0,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
            (uintptr_t)wParam,
            (uintptr_t)lParam));
        if (bd)
            bd->MouseButtonsDown = 0;
        return 0;
    }

    return 0;
}