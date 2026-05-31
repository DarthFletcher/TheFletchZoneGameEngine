#pragma once

#include <cstddef>

#include <DirectXMath.h>

#include "RuntimeWorld.h"

namespace VaultLordSystem
{
    struct PresenceUpdateResult
    {
        size_t updatedCount = 0;
        float strongestThreatLevel = 0.0f;
        RuntimeEntityId strongestThreatEntity = kInvalidRuntimeEntityId;
    };

    struct ThreatSignal
    {
        bool hasSignal = false;
        RuntimeEntityId entity = kInvalidRuntimeEntityId;
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        float threatLevel = 0.0f;
        bool discovered = false;
    };

    PresenceUpdateResult UpdatePresence(RuntimeWorld& runtimeWorld, const DirectX::XMFLOAT3& playerPosition);
    ThreatSignal FindStrongestThreatSignal(const RuntimeWorld& runtimeWorld);
}
