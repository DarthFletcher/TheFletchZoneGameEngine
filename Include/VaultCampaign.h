#pragma once

#include <string>

namespace VaultCampaign
{
    std::string GetSceneFilenameLower(const std::string& scenePath);
    const char* GetNextVaultScenePath(const std::string& sceneFilenameLower);
    bool IsFinalVaultScene(const std::string& sceneFilenameLower);
    bool ShouldAutoAdvanceToNextVault(const std::string& sceneFilenameLower);
}
