//=========================================================================
// 🎮 imgui_impl_win32_custom.h - Custom Dear ImGui Win32 Platform Backend
//=========================================================================

#pragma once

// ✅ Required for Win32 types like HWND, LRESULT, etc.
#include <windows.h>
#include <tchar.h>        // Optional: for TCHAR/_T macros if used anywhere

#include "imgui.h"        // Dear ImGui core (required for ImGuiViewport, etc.)

//=============================================================================
// 🧱 Core ImGui Win32 Platform API
//=============================================================================
IMGUI_IMPL_API bool     ImGui_ImplWin32_Init(void* hwnd);
IMGUI_IMPL_API void     ImGui_ImplWin32_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplWin32_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplWin32_UpdateMonitors();

//=============================================================================
// 🪟 Multi-Viewport Platform Interface Functions
//=============================================================================
IMGUI_IMPL_API void     ImGui_ImplWin32_CreateWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_DestroyWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_ShowWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos);
IMGUI_IMPL_API ImVec2   ImGui_ImplWin32_GetWindowPos(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_SetWindowSize(ImGuiViewport* viewport, ImVec2 size);
IMGUI_IMPL_API ImVec2   ImGui_ImplWin32_GetWindowSize(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_SetWindowFocus(ImGuiViewport* viewport);
IMGUI_IMPL_API bool     ImGui_ImplWin32_GetWindowFocus(ImGuiViewport* viewport);
IMGUI_IMPL_API bool     ImGui_ImplWin32_GetWindowMinimized(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_SetWindowTitle(ImGuiViewport* viewport, const char* title);
IMGUI_IMPL_API void     ImGui_ImplWin32_SetWindowAlpha(ImGuiViewport* viewport, float alpha);
IMGUI_IMPL_API float    ImGui_ImplWin32_GetWindowDpiScale(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_UpdateWindow(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_OnChangedViewport(ImGuiViewport* viewport);
IMGUI_IMPL_API void     ImGui_ImplWin32_RenderWindow(ImGuiViewport* viewport, void* render_arg);
IMGUI_IMPL_API void     ImGui_ImplWin32_SwapBuffers(ImGuiViewport* viewport, void* user_data);


//=============================================================================
// 🧭 DPI Awareness + Utilities
//=============================================================================
IMGUI_IMPL_API float    ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd);
IMGUI_IMPL_API void     ImGui_ImplWin32_EnableDpiAwareness();

//=============================================================================
// 🖱 Input + Message Handling
//=============================================================================
IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//=============================================================================
// ✨ Optional: Alpha Compositing (Transparency)
//=============================================================================
IMGUI_IMPL_API void     ImGui_ImplWin32_EnableAlphaCompositing(void* hwnd);

