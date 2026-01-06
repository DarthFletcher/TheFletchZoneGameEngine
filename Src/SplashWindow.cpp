#include "SplashWindow.h"
#include <gdiplus.h>
using namespace Gdiplus;

HWND SplashWindow::splashHwnd = nullptr;
HBITMAP SplashWindow::splashBitmap = nullptr;
std::wstring SplashWindow::statusText = L"";
ULONG_PTR SplashWindow::gdiplusToken = 0;

// -----------------------------------------------------------
// Initialize GDI+ and create splash window
// -----------------------------------------------------------
bool SplashWindow::Create(const std::wstring& imagePath)
{
    // Init GDI+
    GdiplusStartupInput gdiInput;
    GdiplusStartup(&gdiplusToken, &gdiInput, nullptr);

    // Load splash PNG
    Bitmap bitmap(imagePath.c_str());
    if (bitmap.GetLastStatus() != Ok)
        return false;

    bitmap.GetHBITMAP(Color(0, 0, 0), &splashBitmap);

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"TFZ_SplashWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    // Calculate size
    int bmpWidth = bitmap.GetWidth();
    int bmpHeight = bitmap.GetHeight();

    // Center of the screen
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int x = (screenW - bmpWidth) / 2;
    int y = (screenH - bmpHeight) / 3;

    // Create borderless window
    splashHwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Splash",
        WS_POPUP,
        x, y,
        bmpWidth, bmpHeight + 50,
        nullptr, nullptr,
        wc.hInstance, nullptr
    );

    return splashHwnd != nullptr;
}

// -----------------------------------------------------------
// Show splash window
// -----------------------------------------------------------
void SplashWindow::Show()
{
    ShowWindow(splashHwnd, SW_SHOW);
    UpdateWindow(splashHwnd);
}

// -----------------------------------------------------------
// Close and clean up
// -----------------------------------------------------------
void SplashWindow::Close()
{
    if (splashBitmap)
    {
        DeleteObject(splashBitmap);
        splashBitmap = nullptr;
    }

    if (splashHwnd)
    {
        DestroyWindow(splashHwnd);
        splashHwnd = nullptr;
    }

    GdiplusShutdown(gdiplusToken);
}

// -----------------------------------------------------------
// Update status text under logo
// -----------------------------------------------------------
void SplashWindow::SetStatusText(const std::wstring& text)
{
    statusText = text;
    InvalidateRect(splashHwnd, nullptr, TRUE);
}

// -----------------------------------------------------------
// Window paint callback
// -----------------------------------------------------------
void SplashWindow::Paint(HDC hdc)
{
    RECT rect;
    GetClientRect(splashHwnd, &rect);

    // Black background
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rect, blackBrush);
    DeleteObject(blackBrush);

    // Draw PNG
    if (splashBitmap)
    {
        HDC memDC = CreateCompatibleDC(hdc);
        SelectObject(memDC, splashBitmap);

        BITMAP bmp;
        GetObject(splashBitmap, sizeof(bmp), &bmp);

        int x = (rect.right - bmp.bmWidth) / 2;
        int y = 0;

        BitBlt(hdc, x, y, bmp.bmWidth, bmp.bmHeight, memDC, 0, 0, SRCCOPY);
        DeleteDC(memDC);
    }

    // Draw status text
    if (!statusText.empty())
    {
        HFONT font = CreateFontW(
            20, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Segoe UI"
        );

        SelectObject(hdc, font);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);

        RECT tRect = { 0, rect.bottom - 40, rect.right, rect.bottom };
        DrawTextW(hdc, statusText.c_str(), -1, &tRect, DT_CENTER);

        DeleteObject(font);
    }
}

// -----------------------------------------------------------
// Win32 splash window proc
// -----------------------------------------------------------
LRESULT CALLBACK SplashWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
