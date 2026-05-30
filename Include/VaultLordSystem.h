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

    PresenceUpdateResult UpdatePresence(RuntimeWorld& runtimeWorld, const DirectX::XMFLOAT3& playerPosition);
}
