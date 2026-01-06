#pragma once

class Game {
public:
    bool Initialize(); // ✅ Ensure it matches the cpp function signature
    void Update(float deltaTime);
    void Shutdown();
};
