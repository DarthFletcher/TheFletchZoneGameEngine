#pragma once

#include <cstdint>

#include "Scene.h"
#include "VaultRuntime.h"

class RuntimeWorld;

struct VaultDiscoveryContext
{
    RuntimeWorld* runtimeWorld = nullptr;
    uint32_t playerInstanceId = 0;
    VaultGameplaySettings gameplaySettings{};
};

struct VaultDiscoveryResult
{
    bool discoveredFromRuntimeWorld = false;
};

VaultDiscoveryResult BuildVaultGameplayBindings(
    const VaultDiscoveryContext& context,
    VaultGameplayState& outState);
