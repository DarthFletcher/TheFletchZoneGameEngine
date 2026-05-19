#pragma once

#include <cstdint>

#include "RuntimeWorld.h"
#include "VaultRuntime.h"

namespace VaultNodeSystem
{
    VaultGameplayState::NodeBinding* FindNodeBindingById(VaultGameplayState& state, uint32_t instanceId);
    const VaultGameplayState::NodeBinding* FindNodeBindingById(const VaultGameplayState& state, uint32_t instanceId);
    const VaultGameplayState::NodeBinding* FindMostUrgentDecayingNode(const VaultGameplayState& state);

    VaultNodeComponent* FindRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding);
    void SyncBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding);
    void SyncBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding);
}
