#include "RuntimeWorld.h"

#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <utility>

void RuntimeWorld::Clear()
{
    nextEntityId = 1;
    entities.clear();
    entityIndexById.clear();
    sourceSceneInstanceToRuntime.clear();
    transforms.clear();
    cameras.clear();
    meshRenderers.clear();
    playerControllers.clear();
    triggerVolumes.clear();
    vaultNodes.clear();
    vaultCores.clear();
    vaultRings.clear();
    vaultExits.clear();
    vaultLords.clear();
}

bool RuntimeWorld::CloneFromScene()
{
    Clear();

    const std::vector<SceneInstance>& sceneInstances = Scene::GetInstances();
    entities.reserve(sceneInstances.size());
    bool coreAssigned = false;

    for (const SceneInstance& sceneInstance : sceneInstances)
    {
        if (!Scene::IsInstanceActiveInHierarchy(sceneInstance.instanceId))
            continue;

        const RuntimeEntityId entityId = CreateEntity(sceneInstance.name, sceneInstance.instanceId);

        RuntimeTransformComponent transform{};
        transform.position = sceneInstance.position;
        transform.rotation = sceneInstance.rotation;
        transform.scale = sceneInstance.scale;
        AddTransform(entityId, transform);

        if (sceneInstance.primitive != ScenePrimitive::Empty)
        {
            RuntimeMeshRendererComponent meshRenderer{};
            meshRenderer.visible = sceneInstance.visible;
            meshRenderer.materialIndex = sceneInstance.materialIndex;
            meshRenderer.primitive = sceneInstance.primitive;
            AddMeshRenderer(entityId, meshRenderer);
        }

        if (sceneInstance.camera.enabled)
        {
            RuntimeCameraComponent camera{};
            camera.enabled = sceneInstance.camera.enabled;
            camera.isMain = sceneInstance.camera.isMain;
            camera.fovY = sceneInstance.camera.fovY;
            camera.nearClip = sceneInstance.camera.nearClip;
            camera.farClip = sceneInstance.camera.farClip;
            AddCamera(entityId, camera);
        }

        AddGameplayComponentsFromSceneInstance(entityId, sceneInstance, coreAssigned);
    }

    for (const SceneInstance& sceneInstance : sceneInstances)
    {
        const RuntimeEntityId entityId = FindBySourceSceneInstanceId(sceneInstance.instanceId);
        if (entityId == kInvalidRuntimeEntityId)
            continue;

        const RuntimeEntityId parentId = FindBySourceSceneInstanceId(sceneInstance.parentInstanceId);
        if (RuntimeEntity* entity = GetEntity(entityId))
            entity->parent = parentId;
        if (RuntimeTransformComponent* transform = GetTransform(entityId))
            transform->parent = parentId;
    }

    return true;
}

RuntimeEntityId RuntimeWorld::CreateEntity(const std::string& name, uint32_t sourceSceneInstanceId)
{
    const RuntimeEntityId entityId = AllocateEntityId();

    RuntimeEntity entity{};
    entity.id = entityId;
    entity.sourceSceneInstanceId = sourceSceneInstanceId;
    entity.name = name;

    entityIndexById[entityId] = entities.size();
    entities.push_back(std::move(entity));

    if (sourceSceneInstanceId != 0)
        sourceSceneInstanceToRuntime[sourceSceneInstanceId] = entityId;

    return entityId;
}

bool RuntimeWorld::DestroyEntity(RuntimeEntityId entityId)
{
    const auto indexIt = entityIndexById.find(entityId);
    if (indexIt == entityIndexById.end())
        return false;

    const size_t removedIndex = indexIt->second;
    const RuntimeEntity removedEntity = entities[removedIndex];
    const RuntimeEntity movedEntity = entities.back();

    if (removedIndex != entities.size() - 1)
    {
        entities[removedIndex] = movedEntity;
        entityIndexById[movedEntity.id] = removedIndex;
    }

    entities.pop_back();
    entityIndexById.erase(entityId);
    if (removedEntity.sourceSceneInstanceId != 0)
        sourceSceneInstanceToRuntime.erase(removedEntity.sourceSceneInstanceId);

    for (RuntimeEntity& entity : entities)
    {
        if (entity.parent == entityId)
            entity.parent = kInvalidRuntimeEntityId;
    }
    for (auto& [id, transform] : transforms)
    {
        if (transform.parent == entityId)
            transform.parent = kInvalidRuntimeEntityId;
    }

    RemoveComponents(entityId);
    return true;
}

bool RuntimeWorld::IsAlive(RuntimeEntityId entityId) const
{
    return entityIndexById.find(entityId) != entityIndexById.end();
}

size_t RuntimeWorld::GetEntityCount() const
{
    return entities.size();
}

RuntimeWorldStats RuntimeWorld::GetStats() const
{
    RuntimeWorldStats stats{};
    stats.entities = entities.size();
    stats.transforms = transforms.size();
    stats.cameras = cameras.size();
    stats.meshRenderers = meshRenderers.size();
    stats.playerControllers = playerControllers.size();
    stats.triggerVolumes = triggerVolumes.size();
    stats.vaultNodes = vaultNodes.size();
    stats.vaultCores = vaultCores.size();
    stats.vaultRings = vaultRings.size();
    stats.vaultExits = vaultExits.size();
    stats.vaultLords = vaultLords.size();
    return stats;
}

const std::vector<RuntimeEntity>& RuntimeWorld::GetEntities() const
{
    return entities;
}

RuntimeEntity* RuntimeWorld::GetEntity(RuntimeEntityId entityId)
{
    const auto it = entityIndexById.find(entityId);
    if (it == entityIndexById.end())
        return nullptr;
    return &entities[it->second];
}

const RuntimeEntity* RuntimeWorld::GetEntity(RuntimeEntityId entityId) const
{
    const auto it = entityIndexById.find(entityId);
    if (it == entityIndexById.end())
        return nullptr;
    return &entities[it->second];
}

RuntimeEntityId RuntimeWorld::FindBySourceSceneInstanceId(uint32_t sceneInstanceId) const
{
    const auto it = sourceSceneInstanceToRuntime.find(sceneInstanceId);
    return it != sourceSceneInstanceToRuntime.end() ? it->second : kInvalidRuntimeEntityId;
}

RuntimeEntityId RuntimeWorld::FindMainCameraEntity() const
{
    for (const auto& [entityId, camera] : cameras)
    {
        if (camera.enabled && camera.isMain)
            return entityId;
    }
    return kInvalidRuntimeEntityId;
}

RuntimeEntityId RuntimeWorld::FindFirstPlayerControllerEntity() const
{
    return playerControllers.empty() ? kInvalidRuntimeEntityId : playerControllers.begin()->first;
}

bool RuntimeWorld::SyncTransformToScene(RuntimeEntityId entityId) const
{
    const RuntimeEntity* entity = GetEntity(entityId);
    if (!entity || entity->sourceSceneInstanceId == 0)
        return false;

    const RuntimeTransformComponent* transform = GetTransform(entityId);
    if (!transform)
        return false;

    for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
    {
        SceneInstance* sceneInstance = Scene::GetInstance(instanceIndex);
        if (!sceneInstance || sceneInstance->instanceId != entity->sourceSceneInstanceId)
            continue;

        sceneInstance->position = transform->position;
        sceneInstance->rotation = transform->rotation;
        sceneInstance->scale = transform->scale;
        return true;
    }

    return false;
}

size_t RuntimeWorld::SyncAllTransformsToScene() const
{
    size_t syncedCount = 0;
    for (const auto& [entityId, transform] : transforms)
    {
        (void)transform;
        if (SyncTransformToScene(entityId))
            ++syncedCount;
    }
    return syncedCount;
}

RuntimeTransformComponent& RuntimeWorld::AddTransform(RuntimeEntityId entityId, const RuntimeTransformComponent& component)
{
    return transforms[entityId] = component;
}

RuntimeCameraComponent& RuntimeWorld::AddCamera(RuntimeEntityId entityId, const RuntimeCameraComponent& component)
{
    return cameras[entityId] = component;
}

RuntimeMeshRendererComponent& RuntimeWorld::AddMeshRenderer(RuntimeEntityId entityId, const RuntimeMeshRendererComponent& component)
{
    return meshRenderers[entityId] = component;
}

PlayerControllerComponent& RuntimeWorld::AddPlayerController(RuntimeEntityId entityId, const PlayerControllerComponent& component)
{
    return playerControllers[entityId] = component;
}

TriggerVolumeComponent& RuntimeWorld::AddTriggerVolume(RuntimeEntityId entityId, const TriggerVolumeComponent& component)
{
    return triggerVolumes[entityId] = component;
}

VaultNodeComponent& RuntimeWorld::AddVaultNode(RuntimeEntityId entityId, const VaultNodeComponent& component)
{
    return vaultNodes[entityId] = component;
}

VaultCoreComponent& RuntimeWorld::AddVaultCore(RuntimeEntityId entityId, const VaultCoreComponent& component)
{
    return vaultCores[entityId] = component;
}

VaultRingComponent& RuntimeWorld::AddVaultRing(RuntimeEntityId entityId, const VaultRingComponent& component)
{
    return vaultRings[entityId] = component;
}

VaultExitComponent& RuntimeWorld::AddVaultExit(RuntimeEntityId entityId, const VaultExitComponent& component)
{
    return vaultExits[entityId] = component;
}

VaultLordComponent& RuntimeWorld::AddVaultLord(RuntimeEntityId entityId, const VaultLordComponent& component)
{
    return vaultLords[entityId] = component;
}

RuntimeTransformComponent* RuntimeWorld::GetTransform(RuntimeEntityId entityId)
{
    const auto it = transforms.find(entityId);
    return it != transforms.end() ? &it->second : nullptr;
}

const RuntimeTransformComponent* RuntimeWorld::GetTransform(RuntimeEntityId entityId) const
{
    const auto it = transforms.find(entityId);
    return it != transforms.end() ? &it->second : nullptr;
}

RuntimeCameraComponent* RuntimeWorld::GetCamera(RuntimeEntityId entityId)
{
    const auto it = cameras.find(entityId);
    return it != cameras.end() ? &it->second : nullptr;
}

const RuntimeCameraComponent* RuntimeWorld::GetCamera(RuntimeEntityId entityId) const
{
    const auto it = cameras.find(entityId);
    return it != cameras.end() ? &it->second : nullptr;
}

RuntimeMeshRendererComponent* RuntimeWorld::GetMeshRenderer(RuntimeEntityId entityId)
{
    const auto it = meshRenderers.find(entityId);
    return it != meshRenderers.end() ? &it->second : nullptr;
}

const RuntimeMeshRendererComponent* RuntimeWorld::GetMeshRenderer(RuntimeEntityId entityId) const
{
    const auto it = meshRenderers.find(entityId);
    return it != meshRenderers.end() ? &it->second : nullptr;
}

PlayerControllerComponent* RuntimeWorld::GetPlayerController(RuntimeEntityId entityId)
{
    const auto it = playerControllers.find(entityId);
    return it != playerControllers.end() ? &it->second : nullptr;
}

const PlayerControllerComponent* RuntimeWorld::GetPlayerController(RuntimeEntityId entityId) const
{
    const auto it = playerControllers.find(entityId);
    return it != playerControllers.end() ? &it->second : nullptr;
}

TriggerVolumeComponent* RuntimeWorld::GetTriggerVolume(RuntimeEntityId entityId)
{
    const auto it = triggerVolumes.find(entityId);
    return it != triggerVolumes.end() ? &it->second : nullptr;
}

const TriggerVolumeComponent* RuntimeWorld::GetTriggerVolume(RuntimeEntityId entityId) const
{
    const auto it = triggerVolumes.find(entityId);
    return it != triggerVolumes.end() ? &it->second : nullptr;
}

VaultNodeComponent* RuntimeWorld::GetVaultNode(RuntimeEntityId entityId)
{
    const auto it = vaultNodes.find(entityId);
    return it != vaultNodes.end() ? &it->second : nullptr;
}

const VaultNodeComponent* RuntimeWorld::GetVaultNode(RuntimeEntityId entityId) const
{
    const auto it = vaultNodes.find(entityId);
    return it != vaultNodes.end() ? &it->second : nullptr;
}

VaultCoreComponent* RuntimeWorld::GetVaultCore(RuntimeEntityId entityId)
{
    const auto it = vaultCores.find(entityId);
    return it != vaultCores.end() ? &it->second : nullptr;
}

const VaultCoreComponent* RuntimeWorld::GetVaultCore(RuntimeEntityId entityId) const
{
    const auto it = vaultCores.find(entityId);
    return it != vaultCores.end() ? &it->second : nullptr;
}

VaultRingComponent* RuntimeWorld::GetVaultRing(RuntimeEntityId entityId)
{
    const auto it = vaultRings.find(entityId);
    return it != vaultRings.end() ? &it->second : nullptr;
}

const VaultRingComponent* RuntimeWorld::GetVaultRing(RuntimeEntityId entityId) const
{
    const auto it = vaultRings.find(entityId);
    return it != vaultRings.end() ? &it->second : nullptr;
}

VaultExitComponent* RuntimeWorld::GetVaultExit(RuntimeEntityId entityId)
{
    const auto it = vaultExits.find(entityId);
    return it != vaultExits.end() ? &it->second : nullptr;
}

const VaultExitComponent* RuntimeWorld::GetVaultExit(RuntimeEntityId entityId) const
{
    const auto it = vaultExits.find(entityId);
    return it != vaultExits.end() ? &it->second : nullptr;
}

VaultLordComponent* RuntimeWorld::GetVaultLord(RuntimeEntityId entityId)
{
    const auto it = vaultLords.find(entityId);
    return it != vaultLords.end() ? &it->second : nullptr;
}

const VaultLordComponent* RuntimeWorld::GetVaultLord(RuntimeEntityId entityId) const
{
    const auto it = vaultLords.find(entityId);
    return it != vaultLords.end() ? &it->second : nullptr;
}

const std::unordered_map<RuntimeEntityId, RuntimeTransformComponent>& RuntimeWorld::GetTransforms() const
{
    return transforms;
}

const std::unordered_map<RuntimeEntityId, RuntimeCameraComponent>& RuntimeWorld::GetCameras() const
{
    return cameras;
}

const std::unordered_map<RuntimeEntityId, RuntimeMeshRendererComponent>& RuntimeWorld::GetMeshRenderers() const
{
    return meshRenderers;
}

const std::unordered_map<RuntimeEntityId, PlayerControllerComponent>& RuntimeWorld::GetPlayerControllers() const
{
    return playerControllers;
}

const std::unordered_map<RuntimeEntityId, TriggerVolumeComponent>& RuntimeWorld::GetTriggerVolumes() const
{
    return triggerVolumes;
}

const std::unordered_map<RuntimeEntityId, VaultNodeComponent>& RuntimeWorld::GetVaultNodes() const
{
    return vaultNodes;
}

const std::unordered_map<RuntimeEntityId, VaultCoreComponent>& RuntimeWorld::GetVaultCores() const
{
    return vaultCores;
}

const std::unordered_map<RuntimeEntityId, VaultRingComponent>& RuntimeWorld::GetVaultRings() const
{
    return vaultRings;
}

const std::unordered_map<RuntimeEntityId, VaultExitComponent>& RuntimeWorld::GetVaultExits() const
{
    return vaultExits;
}

std::unordered_map<RuntimeEntityId, VaultLordComponent>& RuntimeWorld::GetVaultLords()
{
    return vaultLords;
}

const std::unordered_map<RuntimeEntityId, VaultLordComponent>& RuntimeWorld::GetVaultLords() const
{
    return vaultLords;
}

RuntimeEntityId RuntimeWorld::AllocateEntityId()
{
    return nextEntityId++;
}

void RuntimeWorld::RemoveComponents(RuntimeEntityId entityId)
{
    transforms.erase(entityId);
    cameras.erase(entityId);
    meshRenderers.erase(entityId);
    playerControllers.erase(entityId);
    triggerVolumes.erase(entityId);
    vaultNodes.erase(entityId);
    vaultCores.erase(entityId);
    vaultRings.erase(entityId);
    vaultExits.erase(entityId);
    vaultLords.erase(entityId);
}

void RuntimeWorld::AddGameplayComponentsFromSceneInstance(RuntimeEntityId entityId, const SceneInstance& sceneInstance, bool& coreAssigned)
{
    auto toLower = [](std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };

    const std::string loweredName = toLower(sceneInstance.name);
    const std::string loweredPrefabPath = toLower(sceneInstance.prefabSourcePath);

    if (loweredName.find("vaultlord") != std::string::npos ||
        loweredPrefabPath.find("vaultlord") != std::string::npos)
    {
        AddVaultLord(entityId);
        return;
    }

    if (!sceneInstance.camera.enabled &&
        (loweredName == "vaultrunner" || loweredName == "player"))
    {
        AddPlayerController(entityId);
    }

    if (loweredName.find("exit") != std::string::npos ||
        loweredName.find("door") != std::string::npos ||
        loweredName.find("gate") != std::string::npos ||
        loweredName.find("barrier") != std::string::npos)
    {
        AddVaultExit(entityId);
        AddTriggerVolume(entityId);
        return;
    }

    VaultType vaultType = sceneInstance.vaultType;
    if (vaultType == VaultType::None)
    {
        if (loweredName.find("vaultcore") != std::string::npos)
            vaultType = VaultType::Core;
        else if (loweredName.find("vaultnode") != std::string::npos)
            vaultType = VaultType::Node;
        else if (loweredName.find("vaultring") != std::string::npos)
            vaultType = VaultType::Ring;
    }

    if (vaultType == VaultType::None && !loweredPrefabPath.empty())
    {
        if (loweredPrefabPath.find("vaultcore") != std::string::npos)
            vaultType = VaultType::Core;
        else if (loweredPrefabPath.find("vaultnode") != std::string::npos)
            vaultType = VaultType::Node;
        else if (loweredPrefabPath.find("vaultring") != std::string::npos)
            vaultType = VaultType::Ring;
    }

    if (vaultType == VaultType::None && sceneInstance.primitive == ScenePrimitive::Sphere)
        vaultType = coreAssigned ? VaultType::Node : VaultType::Core;

    if (vaultType == VaultType::Core)
    {
        if (!coreAssigned)
        {
            AddVaultCore(entityId);
            coreAssigned = true;
        }
        else
        {
            AddVaultNode(entityId);
        }
    }
    else if (vaultType == VaultType::Node)
    {
        VaultNodeComponent node{};
        if (loweredName.find("fragile") != std::string::npos)
        {
            node.type = VaultNodeComponent::Type::Fragile;
            node.decayDuration = 9.0f * 0.65f;
        }
        else if (loweredName.find("corrupt") != std::string::npos)
        {
            node.type = VaultNodeComponent::Type::Corrupted;
        }
        else if (loweredName.find("relay") != std::string::npos)
        {
            node.type = VaultNodeComponent::Type::Relay;
        }
        else if (loweredName.find("hidden") != std::string::npos || loweredName.find("weak") != std::string::npos)
        {
            node.type = VaultNodeComponent::Type::Hidden;
        }
        else if (loweredName.find("slow") != std::string::npos || loweredName.find("stabilize") != std::string::npos)
        {
            node.type = VaultNodeComponent::Type::SlowStabilize;
            node.stabilizeDuration = 1.8f;
        }
        AddVaultNode(entityId, node);
    }
    else if (vaultType == VaultType::Ring)
    {
        AddVaultRing(entityId);
    }
}
