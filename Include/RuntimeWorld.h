#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "RuntimeComponents.h"

struct SceneInstance;

struct RuntimeEntity
{
    RuntimeEntityId id = kInvalidRuntimeEntityId;
    uint32_t sourceSceneInstanceId = 0;
    RuntimeEntityId parent = kInvalidRuntimeEntityId;
    std::string name;
};

struct RuntimeWorldStats
{
    size_t entities = 0;
    size_t transforms = 0;
    size_t cameras = 0;
    size_t meshRenderers = 0;
    size_t playerControllers = 0;
    size_t triggerVolumes = 0;
    size_t vaultNodes = 0;
    size_t vaultCores = 0;
    size_t vaultRings = 0;
    size_t vaultExits = 0;
};

class RuntimeWorld
{
public:
    RuntimeWorld() = default;

    void Clear();
    bool CloneFromScene();

    RuntimeEntityId CreateEntity(const std::string& name, uint32_t sourceSceneInstanceId = 0);
    bool DestroyEntity(RuntimeEntityId entityId);

    bool IsAlive(RuntimeEntityId entityId) const;
    size_t GetEntityCount() const;
    RuntimeWorldStats GetStats() const;
    const std::vector<RuntimeEntity>& GetEntities() const;

    RuntimeEntity* GetEntity(RuntimeEntityId entityId);
    const RuntimeEntity* GetEntity(RuntimeEntityId entityId) const;

    RuntimeEntityId FindBySourceSceneInstanceId(uint32_t sceneInstanceId) const;
    RuntimeEntityId FindMainCameraEntity() const;
    RuntimeEntityId FindFirstPlayerControllerEntity() const;

    bool SyncTransformToScene(RuntimeEntityId entityId) const;
    size_t SyncAllTransformsToScene() const;

    RuntimeTransformComponent& AddTransform(RuntimeEntityId entityId, const RuntimeTransformComponent& component = {});
    RuntimeCameraComponent& AddCamera(RuntimeEntityId entityId, const RuntimeCameraComponent& component = {});
    RuntimeMeshRendererComponent& AddMeshRenderer(RuntimeEntityId entityId, const RuntimeMeshRendererComponent& component = {});
    PlayerControllerComponent& AddPlayerController(RuntimeEntityId entityId, const PlayerControllerComponent& component = {});
    TriggerVolumeComponent& AddTriggerVolume(RuntimeEntityId entityId, const TriggerVolumeComponent& component = {});
    VaultNodeComponent& AddVaultNode(RuntimeEntityId entityId, const VaultNodeComponent& component = {});
    VaultCoreComponent& AddVaultCore(RuntimeEntityId entityId, const VaultCoreComponent& component = {});
    VaultRingComponent& AddVaultRing(RuntimeEntityId entityId, const VaultRingComponent& component = {});
    VaultExitComponent& AddVaultExit(RuntimeEntityId entityId, const VaultExitComponent& component = {});

    RuntimeTransformComponent* GetTransform(RuntimeEntityId entityId);
    const RuntimeTransformComponent* GetTransform(RuntimeEntityId entityId) const;
    RuntimeCameraComponent* GetCamera(RuntimeEntityId entityId);
    const RuntimeCameraComponent* GetCamera(RuntimeEntityId entityId) const;
    RuntimeMeshRendererComponent* GetMeshRenderer(RuntimeEntityId entityId);
    const RuntimeMeshRendererComponent* GetMeshRenderer(RuntimeEntityId entityId) const;
    PlayerControllerComponent* GetPlayerController(RuntimeEntityId entityId);
    const PlayerControllerComponent* GetPlayerController(RuntimeEntityId entityId) const;
    TriggerVolumeComponent* GetTriggerVolume(RuntimeEntityId entityId);
    const TriggerVolumeComponent* GetTriggerVolume(RuntimeEntityId entityId) const;
    VaultNodeComponent* GetVaultNode(RuntimeEntityId entityId);
    const VaultNodeComponent* GetVaultNode(RuntimeEntityId entityId) const;
    VaultCoreComponent* GetVaultCore(RuntimeEntityId entityId);
    const VaultCoreComponent* GetVaultCore(RuntimeEntityId entityId) const;
    VaultRingComponent* GetVaultRing(RuntimeEntityId entityId);
    const VaultRingComponent* GetVaultRing(RuntimeEntityId entityId) const;
    VaultExitComponent* GetVaultExit(RuntimeEntityId entityId);
    const VaultExitComponent* GetVaultExit(RuntimeEntityId entityId) const;

    const std::unordered_map<RuntimeEntityId, RuntimeTransformComponent>& GetTransforms() const;
    const std::unordered_map<RuntimeEntityId, RuntimeCameraComponent>& GetCameras() const;
    const std::unordered_map<RuntimeEntityId, RuntimeMeshRendererComponent>& GetMeshRenderers() const;
    const std::unordered_map<RuntimeEntityId, PlayerControllerComponent>& GetPlayerControllers() const;
    const std::unordered_map<RuntimeEntityId, TriggerVolumeComponent>& GetTriggerVolumes() const;
    const std::unordered_map<RuntimeEntityId, VaultNodeComponent>& GetVaultNodes() const;
    const std::unordered_map<RuntimeEntityId, VaultCoreComponent>& GetVaultCores() const;
    const std::unordered_map<RuntimeEntityId, VaultRingComponent>& GetVaultRings() const;
    const std::unordered_map<RuntimeEntityId, VaultExitComponent>& GetVaultExits() const;

private:
    RuntimeEntityId AllocateEntityId();
    void RemoveComponents(RuntimeEntityId entityId);
    void AddGameplayComponentsFromSceneInstance(RuntimeEntityId entityId, const SceneInstance& sceneInstance, bool& coreAssigned);

    RuntimeEntityId nextEntityId = 1;
    std::vector<RuntimeEntity> entities;
    std::unordered_map<RuntimeEntityId, size_t> entityIndexById;
    std::unordered_map<uint32_t, RuntimeEntityId> sourceSceneInstanceToRuntime;

    std::unordered_map<RuntimeEntityId, RuntimeTransformComponent> transforms;
    std::unordered_map<RuntimeEntityId, RuntimeCameraComponent> cameras;
    std::unordered_map<RuntimeEntityId, RuntimeMeshRendererComponent> meshRenderers;
    std::unordered_map<RuntimeEntityId, PlayerControllerComponent> playerControllers;
    std::unordered_map<RuntimeEntityId, TriggerVolumeComponent> triggerVolumes;
    std::unordered_map<RuntimeEntityId, VaultNodeComponent> vaultNodes;
    std::unordered_map<RuntimeEntityId, VaultCoreComponent> vaultCores;
    std::unordered_map<RuntimeEntityId, VaultRingComponent> vaultRings;
    std::unordered_map<RuntimeEntityId, VaultExitComponent> vaultExits;
};
