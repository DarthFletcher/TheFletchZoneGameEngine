#pragma once
#include <string>

namespace SplashScreen
{
    // Ensures the splash image is uploaded to the GPU (safe to call every frame)
    void EnsureGPUTexture();

    bool Initialize(const char* imagePath);
    void SetStatusText(const char* text);
    void Render();
    void Shutdown();
    void ShowForSeconds(float seconds);
    void Show();
    void RequestFinish();
    void SetMinimumShowTime(float seconds);
    bool IsVisible();
    void DrawSplash();
    void Update(float dt);
    bool IsFinished();
}