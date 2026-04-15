#pragma once

#include <cstdint>

class Game {
public:
    bool Initialize(); // ✅ Ensure it matches the cpp function signature
    void Update(float deltaTime);
    void Shutdown();

    bool HasInteractionTarget() const;
    uint32_t GetInteractionTargetId() const;
    int GetVaultActiveNodeCount() const;
    int GetVaultTotalNodeCount() const;
    bool IsVaultCoreUnlocked() const;
};
