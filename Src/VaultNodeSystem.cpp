#include "VaultNodeSystem.h"

namespace VaultNodeSystem
{
    VaultGameplayState::NodeBinding* FindNodeBindingById(VaultGameplayState& state, uint32_t instanceId)
    {
        for (VaultGameplayState::NodeBinding& node : state.nodes)
        {
            if (node.instanceId == instanceId)
                return &node;
        }
        return nullptr;
    }

    const VaultGameplayState::NodeBinding* FindNodeBindingById(const VaultGameplayState& state, uint32_t instanceId)
    {
        for (const VaultGameplayState::NodeBinding& node : state.nodes)
        {
            if (node.instanceId == instanceId)
                return &node;
        }
        return nullptr;
    }

    const VaultGameplayState::NodeBinding* FindMostUrgentDecayingNode(const VaultGameplayState& state)
    {
        const VaultGameplayState::NodeBinding* bestNode = nullptr;
        for (const VaultGameplayState::NodeBinding& node : state.nodes)
        {
            if (node.state != VaultGameplayState::NodeState::Decaying)
                continue;
            if (!bestNode || node.decayTimer < bestNode->decayTimer)
                bestNode = &node;
        }
        return bestNode;
    }

    VaultNodeComponent* FindRuntimeComponent(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding)
    {
        if (!runtimeWorld)
            return nullptr;

        if (nodeBinding.runtimeEntity == kInvalidRuntimeEntityId)
            nodeBinding.runtimeEntity = runtimeWorld->FindBySourceSceneInstanceId(nodeBinding.instanceId);

        return nodeBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld->GetVaultNode(nodeBinding.runtimeEntity)
            : nullptr;
    }

    void SyncBindingFromRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding)
    {
        VaultNodeComponent* runtimeNode = FindRuntimeComponent(runtimeWorld, nodeBinding);
        if (!runtimeNode)
            return;

        nodeBinding.type = ToVaultNodeType(runtimeNode->type);
        nodeBinding.state = ToVaultNodeState(runtimeNode->state);
        nodeBinding.decayTimer = runtimeNode->decayTimer;
        nodeBinding.decayDuration = runtimeNode->decayDuration;
        nodeBinding.stabilizeDuration = runtimeNode->stabilizeDuration;
        nodeBinding.stabilizeProgress = runtimeNode->stabilizeProgress;
        nodeBinding.warningPlayed = runtimeNode->warningPlayed;
    }

    void SyncBindingToRuntime(RuntimeWorld* runtimeWorld, VaultGameplayState::NodeBinding& nodeBinding)
    {
        VaultNodeComponent* runtimeNode = FindRuntimeComponent(runtimeWorld, nodeBinding);
        if (!runtimeNode)
            return;

        runtimeNode->type = ToRuntimeNodeType(nodeBinding.type);
        runtimeNode->state = ToRuntimeNodeState(nodeBinding.state);
        runtimeNode->decayTimer = nodeBinding.decayTimer;
        runtimeNode->decayDuration = nodeBinding.decayDuration;
        runtimeNode->stabilizeDuration = nodeBinding.stabilizeDuration;
        runtimeNode->stabilizeProgress = nodeBinding.stabilizeProgress;
        runtimeNode->warningPlayed = nodeBinding.warningPlayed;
    }
}
