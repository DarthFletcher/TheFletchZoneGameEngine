#include "Timer.h"

// =====================================================
// Timer implementation using std::chrono for high-resolution timing.
// =====================================================
void Timer::Initialize() {
    lastTime = std::chrono::steady_clock::now();
}

// Call this every frame to update the delta time.
void Timer::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
}

// Get the time elapsed since the last Update() call.
float Timer::GetDeltaTime() const {
    return deltaTime;
}

// Convenience method to initialize the timer at the start of the application.
void Timer::Start()
{
    Initialize();
}

// Convenience method to update the timer each frame, can be called from a central game loop or editor tick.
void Timer::Tick()
{
    Update();
}
