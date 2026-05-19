#include "VaultMissionSystem.h"

namespace VaultMissionSystem
{
    bool IsTerminalState(VaultMissionState state)
    {
        return state == VaultMissionState::Failed || state == VaultMissionState::Escaped;
    }

    bool IsCompletedOrEscaped(VaultMissionState state)
    {
        return state == VaultMissionState::Completed || state == VaultMissionState::Escaped;
    }

    bool CanRestartRun(const VaultMission& mission, bool hasLevelStartSnapshot)
    {
        return hasLevelStartSnapshot &&
            (mission.state == VaultMissionState::Escaped || mission.state == VaultMissionState::Failed);
    }

    bool CanAdvanceToNextVault(const VaultMission& mission, bool hasNextVaultScene)
    {
        return mission.state == VaultMissionState::Escaped && hasNextVaultScene;
    }

    bool ShouldFailFromDecay(const VaultMission& mission)
    {
        return !IsTerminalState(mission.state) &&
            mission.state != VaultMissionState::Completed &&
            mission.decayedNodes >= mission.maxDecayedNodes;
    }

    void ResetForDiscoveredVault(VaultMission& mission, const VaultGameplaySettings& settings, int totalNodes)
    {
        mission.state = VaultMissionState::ActivatingNodes;
        mission.totalNodes = totalNodes;
        mission.activatedNodes = 0;
        mission.decayedNodes = 0;
        mission.maxDecayedNodes = settings.maxDecayedNodes;
        mission.nodeDecayDuration = settings.nodeDecayDuration;
        mission.nodeDecayWarningSeconds = settings.nodeDecayWarningSeconds;
        mission.coreUnlocked = false;
    }

    void UpdateNodeProgress(VaultMission& mission, int activeNodeCount, int totalNodes)
    {
        mission.totalNodes = totalNodes;
        mission.activatedNodes = activeNodeCount;
    }

    void UpdateCoreAvailability(VaultMission& mission, bool coreUnlocked)
    {
        mission.coreUnlocked = coreUnlocked;
        if (mission.state != VaultMissionState::Completed &&
            mission.state != VaultMissionState::Escaped &&
            mission.state != VaultMissionState::Failed)
        {
            mission.state = coreUnlocked ? VaultMissionState::CoreUnlocked : VaultMissionState::ActivatingNodes;
        }
    }
}
