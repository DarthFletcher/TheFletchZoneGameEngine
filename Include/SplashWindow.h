#pragma once

#include <windows.h>
#include <string>

// GDI+ headers in some Windows SDKs use min/max identifiers.
// Our engine defines NOMINMAX, so provide local macros for the GDI+ include only.
#ifndef min
#define TFZ_SPLASHWINDOW_MIN_DEFINED
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define TFZ_SPLASHWINDOW_MAX_DEFINED
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#include <gdiplus.h>

#ifdef TFZ_SPLASHWINDOW_MIN_DEFINED
#undef min
#undef TFZ_SPLASHWINDOW_MIN_DEFINED
#endif
#ifdef TFZ_SPLASHWINDOW_MAX_DEFINED
#undef max
#undef TFZ_SPLASHWINDOW_MAX_DEFINED
#endif

#pragma comment(lib, "gdiplus.lib")

class SplashWindow
{
public:
    static bool Create(const std::wstring& imagePath);
    static void Show();
    static void Close();
    static void SetStatusText(const std::wstring& text);

    // Diagnostics
    static HWND GetHwnd() { return splashHwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void Paint(HDC hdc);

private:
    static HWND splashHwnd;
    static HBITMAP splashBitmap;
    static std::wstring statusText;

    static ULONG_PTR gdiplusToken;
};
