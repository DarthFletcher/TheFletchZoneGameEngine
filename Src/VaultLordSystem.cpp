#include "VaultLordSystem.h"

#include <algorithm>
#include <cmath>

namespace VaultLordSystem
{
    namespace
    {
        float Distance(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    PresenceUpdateResult UpdatePresence(RuntimeWorld& runtimeWorld, const DirectX::XMFLOAT3& playerPosition)
    {
        PresenceUpdateResult result{};

        for (auto& [entityId, vaultLord] : runtimeWorld.GetVaultLords())
        {
            ++result.updatedCount;

            if (!vaultLord.enabled)
            {
                vaultLord.active = false;
                vaultLord.threatLevel = 0.0f;
                vaultLord.distanceToPlayer = 0.0f;
                continue;
            }

            const RuntimeTransformComponent* transform = runtimeWorld.GetTransform(entityId);
            if (!transform || vaultLord.influenceRadius <= 0.0f)
            {
                vaultLord.active = false;
                vaultLord.threatLevel = 0.0f;
                vaultLord.distanceToPlayer = 0.0f;
                continue;
            }

            const float distance = Distance(playerPosition, transform->position);
            vaultLord.distanceToPlayer = distance;

            const float normalizedThreat = 1.0f - (distance / vaultLord.influenceRadius);
            vaultLord.threatLevel = std::clamp(normalizedThreat, 0.0f, 1.0f);
            vaultLord.active = distance <= vaultLord.influenceRadius;
            if (vaultLord.active)
                vaultLord.discovered = true;

            if (vaultLord.threatLevel > result.strongestThreatLevel)
            {
                result.strongestThreatLevel = vaultLord.threatLevel;
                result.strongestThreatEntity = entityId;
            }
        }

        return result;
    }

    ThreatSignal FindStrongestThreatSignal(const RuntimeWorld& runtimeWorld)
    {
        ThreatSignal signal{};

        for (const auto& [entityId, vaultLord] : runtimeWorld.GetVaultLords())
        {
            if (!vaultLord.enabled || vaultLord.threatLevel <= 0.0f)
                continue;

            const RuntimeTransformComponent* transform = runtimeWorld.GetTransform(entityId);
            if (!transform)
                continue;

            if (!signal.hasSignal || vaultLord.threatLevel > signal.threatLevel)
            {
                signal.hasSignal = true;
                signal.entity = entityId;
                signal.position = transform->position;
                signal.threatLevel = vaultLord.threatLevel;
                signal.discovered = vaultLord.discovered;
            }
        }

        return signal;
    }
}
