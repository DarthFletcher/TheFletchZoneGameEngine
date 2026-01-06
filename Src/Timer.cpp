#include "Timer.h"

void Timer::Initialize() {
    lastTime = std::chrono::steady_clock::now();
}

void Timer::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
}

float Timer::GetDeltaTime() const {
    return deltaTime;
}

void Timer::Start()
{

}

void Timer::Tick()
{
    // Timer logic here
}
