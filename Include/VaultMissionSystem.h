#pragma once

#include "Scene.h"
#include "VaultRuntime.h"

namespace VaultMissionSystem
{
    bool IsTerminalState(VaultMissionState state);
    bool IsCompletedOrEscaped(VaultMissionState state);
    bool CanRestartRun(const VaultMission& mission, bool hasLevelStartSnapshot);
    bool CanAdvanceToNextVault(const VaultMission& mission, bool hasNextVaultScene);
    bool ShouldFailFromDecay(const VaultMission& mission);

    void ResetForDiscoveredVault(VaultMission& mission, const VaultGameplaySettings& settings, int totalNodes);
    void UpdateNodeProgress(VaultMission& mission, int activeNodeCount, int totalNodes);
    void UpdateCoreAvailability(VaultMission& mission, bool coreUnlocked);
}
