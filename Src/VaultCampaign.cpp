#include "VaultCampaign.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace VaultCampaign
{
    std::string GetSceneFilenameLower(const std::string& scenePath)
    {
        std::string filename = std::filesystem::path(scenePath).filename().string();
        std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return filename;
    }

    const char* GetNextVaultScenePath(const std::string& sceneFilenameLower)
    {
        if (sceneFilenameLower == "vault_intro.scene" || sceneFilenameLower == "main.scene")
            return "Assets/Scenes/Vault_Traversal.scene";
        if (sceneFilenameLower == "vault_traversal.scene")
            return "Assets/Scenes/Vault_Priority.scene";
        if (sceneFilenameLower == "vault_priority.scene")
            return "Assets/Scenes/Main.scene";
        return nullptr;
    }

    bool IsFinalVaultScene(const std::string& sceneFilenameLower)
    {
        return sceneFilenameLower == "vault_priority.scene";
    }

    bool ShouldAutoAdvanceToNextVault(const std::string& sceneFilenameLower)
    {
        return GetNextVaultScenePath(sceneFilenameLower) != nullptr && !IsFinalVaultScene(sceneFilenameLower);
    }
}
