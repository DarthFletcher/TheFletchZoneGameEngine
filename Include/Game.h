#pragma once

#include <cstdint>

enum class VaultMissionState : uint8_t
{
    Inactive = 0,
    ActivatingNodes,
    CoreUnlocked,
    Completed,
    Escaped,
    Failed
};

class Game {
public:
    bool Initialize(); // ✅ Ensure it matches the cpp function signature
    void Update(float deltaTime);
    void Shutdown();

    VaultMissionState GetVaultMissionState() const;
    const char* GetVaultMissionObjectiveText() const;
    bool HasInteractionTarget() const;
    uint32_t GetInteractionTargetId() const;
    const char* GetInteractionPrompt() const;
    const char* GetInteractionActionLabel() const;
    bool HasVaultWarning() const;
    const char* GetVaultWarningText() const;
    int GetVaultActiveNodeCount() const;
    int GetVaultTotalNodeCount() const;
    bool CanRestartVaultRun() const;
    bool IsVaultCoreUnlocked() const;
    bool IsVaultMissionCompleted() const;
    bool IsVaultMissionEscaped() const;
    bool IsVaultMissionFailed() const;
    const char* GetVaultEndOverlayTitle() const;
    const char* GetVaultEndOverlaySubtitle() const;
    bool HasVaultPresentationBanner() const;
    const char* GetVaultPresentationBannerText() const;
    float GetVaultPresentationBannerAlpha() const;
    bool HasVaultFailPulse() const;
    float GetVaultFailPulseAlpha() const;
};
