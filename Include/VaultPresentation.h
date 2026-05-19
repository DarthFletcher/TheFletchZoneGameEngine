#pragma once

#include <string>

#include "VaultRuntime.h"

namespace VaultPresentation
{
    bool IsTutorialScene(const std::string& sceneFilenameLower);
    const char* GetProgressionLabel(const std::string& sceneFilenameLower);
    const char* GetTutorialHeader(const std::string& sceneFilenameLower);
    const char* GetTutorialHintPrimary(const std::string& sceneFilenameLower, const VaultMission& mission, bool hasUrgentDecayingNode);
    const char* GetTutorialHintSecondary(const std::string& sceneFilenameLower, VaultMissionState missionState);
    const char* GetNextVaultActionLabel(const std::string& sceneFilenameLower);
    const char* GetNextVaultButtonLabel(bool isFinalVaultScene);
    const char* GetMissionObjectiveText(VaultMissionState state);
    const char* GetContextHintTitle(VaultContextHintState::HintType hintType);
    const char* GetContextHintText(VaultContextHintState::HintType hintType);
    const char* GetEndOverlayTitle(VaultMissionState state, bool isFinalVaultScene);
    const char* GetEndOverlaySubtitle(VaultMissionState state, bool isFinalVaultScene);
    const char* GetPresentationBannerText(VaultPresentationState::BannerType bannerType);
    float GetPresentationBannerAlpha(const VaultPresentationState& presentation, float startDurationSeconds, float escapeDurationSeconds);
    float GetFailPulseAlpha(float failPulseTimer, float failPulseDurationSeconds);
}
