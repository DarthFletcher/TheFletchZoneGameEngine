#pragma once

#include "RuntimeWorld.h"
#include "VaultRuntime.h"

namespace VaultObjectSystem
{
    VaultRingComponent* FindRingRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding);
    RuntimeTransformComponent* FindRingRuntimeTransform(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding);
    void SyncRingBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::RingBinding& ringBinding, bool active, bool completed);

    VaultCoreComponent* FindCoreRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding);
    VaultExitComponent* FindExitRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding);

    void SyncCoreBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding, VaultGameplayState& state, VaultMission& mission);
    void SyncCoreBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::CoreBinding& coreBinding, const VaultGameplayState& state, const VaultMission& mission);

    void SyncExitBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding);
    void SyncExitBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::ExitBinding& exitBinding, const VaultMission& mission);
}
