#include "VaultDiscovery.h"

#include "RuntimeWorld.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static SceneInstance* FindInstanceById(uint32_t instanceId)
    {
        if (instanceId == 0)
            return nullptr;

        for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
        {
            SceneInstance* instance = Scene::GetInstance(instanceIndex);
            if (instance && instance->instanceId == instanceId)
                return instance;
        }

        return nullptr;
    }

    static RuntimeEntityId FindRuntimeEntityForSceneInstanceId(const RuntimeWorld* runtimeWorld, uint32_t sceneInstanceId)
    {
        if (!runtimeWorld || sceneInstanceId == 0)
            return kInvalidRuntimeEntityId;

        return runtimeWorld->FindBySourceSceneInstanceId(sceneInstanceId);
    }

    static SceneInstance* FindSourceInstanceForRuntimeEntity(
        const RuntimeWorld& runtimeWorld,
        RuntimeEntityId entityId,
        uint32_t playerInstanceId)
    {
        const RuntimeEntity* entity = runtimeWorld.GetEntity(entityId);
        if (!entity || entity->sourceSceneInstanceId == 0 || entity->sourceSceneInstanceId == playerInstanceId)
            return nullptr;

        SceneInstance* instance = FindInstanceById(entity->sourceSceneInstanceId);
        if (!instance || instance->camera.enabled)
            return nullptr;

        return instance;
    }

    static bool IsVaultExitName(const std::string& value)
    {
        const std::string lowered = ToLower(value);
        return lowered.find("exit") != std::string::npos ||
            lowered.find("door") != std::string::npos ||
            lowered.find("gate") != std::string::npos ||
            lowered.find("barrier") != std::string::npos;
    }

    static VaultGameplayState::NodeType InferVaultNodeType(const std::string& value)
    {
        const std::string lowered = ToLower(value);
        if (lowered.find("fragile") != std::string::npos)
            return VaultGameplayState::NodeType::Fragile;
        if (lowered.find("corrupt") != std::string::npos)
            return VaultGameplayState::NodeType::Corrupted;
        if (lowered.find("slow") != std::string::npos || lowered.find("stabilize") != std::string::npos)
            return VaultGameplayState::NodeType::SlowStabilize;
        return VaultGameplayState::NodeType::Normal;
    }

    static VaultType ClassifyVaultType(const SceneInstance& instance, bool coreAlreadyAssigned)
    {
        if (instance.vaultType != VaultType::None)
            return instance.vaultType;

        const std::string prefabPath = ToLower(instance.prefabSourcePath);
        if (!prefabPath.empty())
        {
            if (prefabPath.find("vaultcore") != std::string::npos)
                return VaultType::Core;
            if (prefabPath.find("vaultring") != std::string::npos)
                return VaultType::Ring;
            if (prefabPath.find("vaultnode") != std::string::npos)
                return VaultType::Node;
        }

        switch (instance.primitive)
        {
        case ScenePrimitive::Torus:
        case ScenePrimitive::Cylinder:
            return VaultType::Ring;
        case ScenePrimitive::Sphere:
            return coreAlreadyAssigned ? VaultType::Node : VaultType::Core;
        case ScenePrimitive::Empty:
            return VaultType::None;
        case ScenePrimitive::Cube:
        case ScenePrimitive::Plane:
        case ScenePrimitive::Capsule:
        case ScenePrimitive::Cone:
        default:
            return VaultType::Node;
        }
    }

    static float GetNodeDecayDuration(VaultGameplayState::NodeType nodeType, const VaultGameplaySettings& settings)
    {
        return (nodeType == VaultGameplayState::NodeType::Fragile)
            ? settings.nodeDecayDuration * 0.65f
            : settings.nodeDecayDuration;
    }

    static float GetNodeStabilizeDuration(VaultGameplayState::NodeType nodeType)
    {
        return (nodeType == VaultGameplayState::NodeType::SlowStabilize) ? 1.8f : 0.0f;
    }

    static void AddNodeBinding(
        VaultGameplayState& outState,
        const SceneInstance& instance,
        VaultGameplayState::NodeType nodeType,
        const VaultGameplaySettings& settings,
        RuntimeEntityId runtimeEntity = kInvalidRuntimeEntityId)
    {
        outState.nodes.push_back({
            instance.instanceId,
            nodeType,
            VaultGameplayState::NodeState::Inactive,
            0.0f,
            GetNodeDecayDuration(nodeType, settings),
            GetNodeStabilizeDuration(nodeType),
            0.0f,
            false,
            instance.materialIndex,
            runtimeEntity });
    }

    static bool BuildFromRuntimeWorld(const VaultDiscoveryContext& context, VaultGameplayState& outState)
    {
        RuntimeWorld* runtimeWorld = context.runtimeWorld;
        if (!runtimeWorld)
            return false;

        for (const auto& [entityId, exitComponent] : runtimeWorld->GetVaultExits())
        {
            (void)exitComponent;
            if (outState.exit.instanceId != 0)
                break;

            if (SceneInstance* instance = FindSourceInstanceForRuntimeEntity(*runtimeWorld, entityId, context.playerInstanceId))
            {
                outState.exit.instanceId = instance->instanceId;
                outState.exit.originalMaterialIndex = instance->materialIndex;
                outState.exit.originalPosition = instance->position;
                outState.exit.runtimeEntity = entityId;
            }
        }

        for (const auto& [entityId, coreComponent] : runtimeWorld->GetVaultCores())
        {
            (void)coreComponent;
            if (outState.core.instanceId != 0)
                break;

            if (SceneInstance* instance = FindSourceInstanceForRuntimeEntity(*runtimeWorld, entityId, context.playerInstanceId))
            {
                outState.core.instanceId = instance->instanceId;
                outState.core.originalMaterialIndex = instance->materialIndex;
                outState.core.runtimeEntity = entityId;
            }
        }

        for (const auto& [entityId, ringComponent] : runtimeWorld->GetVaultRings())
        {
            (void)ringComponent;
            if (SceneInstance* instance = FindSourceInstanceForRuntimeEntity(*runtimeWorld, entityId, context.playerInstanceId))
                outState.rings.push_back({ instance->instanceId, instance->materialIndex, instance->rotation, entityId });
        }

        for (const auto& [entityId, nodeComponent] : runtimeWorld->GetVaultNodes())
        {
            if (SceneInstance* instance = FindSourceInstanceForRuntimeEntity(*runtimeWorld, entityId, context.playerInstanceId))
                AddNodeBinding(outState, *instance, ToVaultNodeType(nodeComponent.type), context.gameplaySettings, entityId);
        }

        return outState.core.instanceId != 0 || !outState.nodes.empty() || !outState.rings.empty() || outState.exit.instanceId != 0;
    }

    static void BuildFromSceneFallback(const VaultDiscoveryContext& context, VaultGameplayState& outState)
    {
        for (const SceneInstance& instance : Scene::GetInstances())
        {
            if (instance.instanceId == context.playerInstanceId || instance.camera.enabled)
                continue;

            if (outState.exit.instanceId == 0 && IsVaultExitName(instance.name))
            {
                outState.exit.instanceId = instance.instanceId;
                outState.exit.originalMaterialIndex = instance.materialIndex;
                outState.exit.originalPosition = instance.position;
                outState.exit.runtimeEntity = FindRuntimeEntityForSceneInstanceId(context.runtimeWorld, instance.instanceId);
                continue;
            }

            const VaultType vaultType = ClassifyVaultType(instance, outState.core.instanceId != 0);
            if (vaultType == VaultType::None)
                continue;

            switch (vaultType)
            {
            case VaultType::Core:
                if (outState.core.instanceId == 0)
                {
                    outState.core.instanceId = instance.instanceId;
                    outState.core.originalMaterialIndex = instance.materialIndex;
                    outState.core.runtimeEntity = FindRuntimeEntityForSceneInstanceId(context.runtimeWorld, instance.instanceId);
                }
                else
                {
                    AddNodeBinding(outState, instance, InferVaultNodeType(instance.name), context.gameplaySettings,
                        FindRuntimeEntityForSceneInstanceId(context.runtimeWorld, instance.instanceId));
                }
                break;
            case VaultType::Ring:
                outState.rings.push_back({
                    instance.instanceId,
                    instance.materialIndex,
                    instance.rotation,
                    FindRuntimeEntityForSceneInstanceId(context.runtimeWorld, instance.instanceId) });
                break;
            case VaultType::Node:
                AddNodeBinding(outState, instance, InferVaultNodeType(instance.name), context.gameplaySettings,
                    FindRuntimeEntityForSceneInstanceId(context.runtimeWorld, instance.instanceId));
                break;
            case VaultType::None:
            default:
                break;
            }
        }
    }
}

VaultDiscoveryResult BuildVaultGameplayBindings(
    const VaultDiscoveryContext& context,
    VaultGameplayState& outState)
{
    VaultDiscoveryResult result{};
    result.discoveredFromRuntimeWorld = BuildFromRuntimeWorld(context, outState);

    if (!result.discoveredFromRuntimeWorld)
        BuildFromSceneFallback(context, outState);

    return result;
}
