#include "VaultPresentation.h"

#include <algorithm>

namespace VaultPresentation
{
    bool IsTutorialScene(const std::string& sceneFilenameLower)
    {
        return sceneFilenameLower == "vault_intro.scene" || sceneFilenameLower == "main.scene";
    }

    const char* GetProgressionLabel(const std::string& sceneFilenameLower)
    {
        if (sceneFilenameLower == "vault_intro.scene" || sceneFilenameLower == "main.scene")
            return "Vault 1 / 3";
        if (sceneFilenameLower == "vault_traversal.scene")
            return "Vault 2 / 3";
        if (sceneFilenameLower == "vault_priority.scene")
            return "Vault 3 / 3";
        return nullptr;
    }

    const char* GetTutorialHeader(const std::string& sceneFilenameLower)
    {
        return IsTutorialScene(sceneFilenameLower) ? "Tutorial" : nullptr;
    }

    const char* GetTutorialHintPrimary(const std::string& sceneFilenameLower, const VaultMission& mission, bool hasUrgentDecayingNode)
    {
        if (!IsTutorialScene(sceneFilenameLower))
            return nullptr;

        switch (mission.state)
        {
        case VaultMissionState::ActivatingNodes:
            if (mission.activatedNodes <= 0)
                return "Step 1: Find a nearby node and press E to activate it.";
            if (hasUrgentDecayingNode)
                return "If a node turns red, go back and refresh it before it fully decays.";
            return "Keep all nodes active at the same time to unlock the core.";
        case VaultMissionState::CoreUnlocked:
            return "Step 2: The core is unlocked. Move to it and press E to stabilize it.";
        case VaultMissionState::Completed:
            return "Step 3: The exit is open. Walk through the gate area to escape.";
        case VaultMissionState::Failed:
            return "Too many nodes decayed. Retry and keep a tighter loop between nodes.";
        case VaultMissionState::Escaped:
            return "Tutorial complete. Vault 2 adds more traversal pressure.";
        case VaultMissionState::Inactive:
        default:
            return "Learn the loop: nodes -> core -> exit.";
        }
    }

    const char* GetTutorialHintSecondary(const std::string& sceneFilenameLower, VaultMissionState missionState)
    {
        if (!IsTutorialScene(sceneFilenameLower))
            return nullptr;

        switch (missionState)
        {
        case VaultMissionState::ActivatingNodes:
            return "RMB look | WASD move | Shift sprint | E interact | Red means urgent";
        case VaultMissionState::CoreUnlocked:
            return "When every node stays active together, the core becomes interactable.";
        case VaultMissionState::Completed:
            return "The gate raises upward, but the escape trigger stays on the floor path below it.";
        case VaultMissionState::Failed:
            return "Orange means stable. Red means urgent. Press R to restart anytime.";
        case VaultMissionState::Escaped:
            return "Press N, click Next Vault, or wait for the auto-advance.";
        case VaultMissionState::Inactive:
        default:
            return "Nodes decay slowly here so you can learn the rhythm.";
        }
    }

    const char* GetNextVaultActionLabel(const std::string& sceneFilenameLower)
    {
        if (sceneFilenameLower == "vault_intro.scene" || sceneFilenameLower == "main.scene")
            return "Press N / A for Vault 2";
        if (sceneFilenameLower == "vault_traversal.scene")
            return "Press N / A for Vault 3";
        if (sceneFilenameLower == "vault_priority.scene")
            return "Press N / A to Restart From Vault 1";
        return nullptr;
    }

    const char* GetNextVaultButtonLabel(bool isFinalVaultScene)
    {
        return isFinalVaultScene ? "Restart Campaign" : "Next Vault";
    }

    const char* GetMissionObjectiveText(VaultMissionState state)
    {
        switch (state)
        {
        case VaultMissionState::ActivatingNodes: return "Objective: Activate all vault nodes";
        case VaultMissionState::CoreUnlocked: return "Objective: Reach and stabilize the vault core";
        case VaultMissionState::Completed: return "Objective: Reach the opened exit";
        case VaultMissionState::Escaped: return "Vault Escaped!";
        case VaultMissionState::Failed: return "Mission Failed";
        case VaultMissionState::Inactive:
        default: return "Objective: Enter the vault";
        }
    }

    const char* GetContextHintTitle(VaultContextHintState::HintType hintType)
    {
        switch (hintType)
        {
        case VaultContextHintState::HintType::SlowNode: return "New Node: Slow Stabilize";
        case VaultContextHintState::HintType::FragileNode: return "New Node: Fragile";
        case VaultContextHintState::HintType::None:
        default: return nullptr;
        }
    }

    const char* GetContextHintText(VaultContextHintState::HintType hintType)
    {
        switch (hintType)
        {
        case VaultContextHintState::HintType::SlowNode:
            return "Start it with E or A, then stay nearby until the node fully stabilizes.";
        case VaultContextHintState::HintType::FragileNode:
            return "Fragile nodes decay faster than normal ones, so refresh them first when routing gets tight.";
        case VaultContextHintState::HintType::None:
        default:
            return nullptr;
        }
    }

    const char* GetEndOverlayTitle(VaultMissionState state, bool isFinalVaultScene)
    {
        switch (state)
        {
        case VaultMissionState::Escaped: return isFinalVaultScene ? "GAME COMPLETE" : "ESCAPE SUCCESSFUL";
        case VaultMissionState::Failed: return "SYSTEM FAILURE";
        default: return nullptr;
        }
    }

    const char* GetEndOverlaySubtitle(VaultMissionState state, bool isFinalVaultScene)
    {
        switch (state)
        {
        case VaultMissionState::Escaped:
            return isFinalVaultScene
                ? "All three vaults are clear. Would you like to restart from the beginning?"
                : "The core is stabilized and the vault has been cleared.";
        case VaultMissionState::Failed: return "Too many vault nodes decayed. Press R to retry.";
        default: return nullptr;
        }
    }

    const char* GetPresentationBannerText(VaultPresentationState::BannerType bannerType)
    {
        switch (bannerType)
        {
        case VaultPresentationState::BannerType::Start: return "Vault Initialized";
        case VaultPresentationState::BannerType::Escape: return "Escape Successful";
        case VaultPresentationState::BannerType::None:
        default: return nullptr;
        }
    }

    float GetPresentationBannerAlpha(const VaultPresentationState& presentation, float startDurationSeconds, float escapeDurationSeconds)
    {
        if (presentation.bannerTimer <= 0.0f || presentation.bannerType == VaultPresentationState::BannerType::None)
            return 0.0f;

        const float duration = (presentation.bannerType == VaultPresentationState::BannerType::Escape)
            ? escapeDurationSeconds
            : startDurationSeconds;
        return std::clamp(presentation.bannerTimer / duration, 0.0f, 1.0f);
    }

    float GetFailPulseAlpha(float failPulseTimer, float failPulseDurationSeconds)
    {
        if (failPulseTimer <= 0.0f)
            return 0.0f;
        return std::clamp(failPulseTimer / failPulseDurationSeconds, 0.0f, 1.0f);
    }
}
