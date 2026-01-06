#pragma once

#include <chrono>

class Timer {
public:
    void Initialize();
    void Update();
    float GetDeltaTime() const;
    void Start();
    void Tick();

private:
    std::chrono::steady_clock::time_point lastTime;
    float deltaTime = 0.0f;
};