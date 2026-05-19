#include "VaultObjectSystem.h"

namespace VaultObjectSystem
{
    VaultRingComponent* FindRingRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding)
    {
        if (!runtimeWorld)
            return nullptr;

        if (ringBinding.runtimeEntity == kInvalidRuntimeEntityId)
            ringBinding.runtimeEntity = runtimeWorld->FindBySourceSceneInstanceId(ringBinding.instanceId);

        return ringBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld->GetVaultRing(ringBinding.runtimeEntity)
            : nullptr;
    }

    RuntimeTransformComponent* FindRingRuntimeTransform(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding)
    {
        if (!runtimeWorld)
            return nullptr;

        if (ringBinding.runtimeEntity == kInvalidRuntimeEntityId)
            ringBinding.runtimeEntity = runtimeWorld->FindBySourceSceneInstanceId(ringBinding.instanceId);

        return ringBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld->GetTransform(ringBinding.runtimeEntity)
            : nullptr;
    }

    void SyncRingBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding, bool active, bool completed)
    {
        VaultRingComponent* runtimeRing = FindRingRuntimeComponent(runtimeWorld, ringBinding);
        if (!runtimeRing)
            return;

        runtimeRing->active = active;
        runtimeRing->completed = completed;
    }

    VaultCoreComponent* FindCoreRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding)
    {
        if (!runtimeWorld)
            return nullptr;

        if (coreBinding.runtimeEntity == kInvalidRuntimeEntityId)
            coreBinding.runtimeEntity = runtimeWorld->FindBySourceSceneInstanceId(coreBinding.instanceId);

        return coreBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld->GetVaultCore(coreBinding.runtimeEntity)
            : nullptr;
    }

    VaultExitComponent* FindExitRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding)
    {
        if (!runtimeWorld)
            return nullptr;

        if (exitBinding.runtimeEntity == kInvalidRuntimeEntityId)
            exitBinding.runtimeEntity = runtimeWorld->FindBySourceSceneInstanceId(exitBinding.instanceId);

        return exitBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld->GetVaultExit(exitBinding.runtimeEntity)
            : nullptr;
    }

    void SyncCoreBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding, VaultGameplayState& state, VaultMission& mission)
    {
        VaultCoreComponent* runtimeCore = FindCoreRuntimeComponent(runtimeWorld, coreBinding);
        if (!runtimeCore)
            return;

        state.coreUnlocked = runtimeCore->unlocked;
        mission.coreUnlocked = runtimeCore->unlocked;
    }

    void SyncCoreBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding, const VaultGameplayState& state, const VaultMission& mission)
    {
        VaultCoreComponent* runtimeCore = FindCoreRuntimeComponent(runtimeWorld, coreBinding);
        if (!runtimeCore)
            return;

        runtimeCore->unlocked = mission.coreUnlocked || state.coreUnlocked;
        runtimeCore->stabilized = mission.state == VaultMissionState::Completed ||
            mission.state == VaultMissionState::Escaped;
    }

    void SyncExitBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding)
    {
        VaultExitComponent* runtimeExit = FindExitRuntimeComponent(runtimeWorld, exitBinding);
        if (!runtimeExit)
            return;

        exitBinding.openOffsetY = runtimeExit->openOffsetY;
        exitBinding.opened = runtimeExit->opened;
    }

    void SyncExitBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding, const VaultMission& mission)
    {
        VaultExitComponent* runtimeExit = FindExitRuntimeComponent(runtimeWorld, exitBinding);
        if (!runtimeExit)
            return;

        runtimeExit->unlocked = mission.state == VaultMissionState::Completed ||
            mission.state == VaultMissionState::Escaped;
        runtimeExit->opened = exitBinding.opened;
        runtimeExit->openOffsetY = exitBinding.openOffsetY;
    }
}
