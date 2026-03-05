#pragma once

// Debug-only runtime isolation toggles shared across translation units.
// Defined in `Src/Graphics.cpp`.

#ifndef NDEBUG
extern bool g_r_skipScene;
extern bool g_r_skipImGui;
extern bool g_r_skipSplashUpload;
extern bool g_r_skipDebugLines;
extern bool g_r_nearlyEmptyFrame;
extern bool g_r_postExecuteFenceWait;
#endif
