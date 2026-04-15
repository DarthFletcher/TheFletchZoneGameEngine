#include "Game.h"

#include "Engine.h"
#include "Graphics.h"
#include "MaterialManager.h"
#include "Scene.h"
#include "logger.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

extern Engine* g_engineInstance;

namespace
{
    struct PlayerController
    {
        uint32_t instanceId = 0;
        float moveSpeed = 5.0f;
        float lookSensitivity = 0.0025f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float cameraHeight = 1.6f;
        bool initializedLook = false;
    };

    struct VaultGameplayState
    {
        struct NodeBinding
        {
            uint32_t instanceId = 0;
            bool active = false;
            int originalMaterialIndex = 0;
        };

        struct RingBinding
        {
            uint32_t instanceId = 0;
            int originalMaterialIndex = 0;
            DirectX::XMFLOAT3 originalRotation{ 0.0f, 0.0f, 0.0f };
        };

        struct CoreBinding
        {
            uint32_t instanceId = 0;
            int originalMaterialIndex = 0;
        };

        std::vector<NodeBinding> nodes;
        std::vector<RingBinding> rings;
        CoreBinding core{};
        int nodeInactiveMaterial = 0;
        int nodeActiveMaterial = 0;
        int coreInactiveMaterial = 0;
        int coreActiveMaterial = 0;
        int ringInactiveMaterial = 0;
        int ringActiveMaterial = 0;
        bool initialized = false;
        bool coreUnlocked = false;
        bool unlockedLogged = false;
    };

    static PlayerController g_PlayerController{};
    static uint32_t g_InteractionTargetId = 0;
    static VaultGameplayState g_VaultState{};
    static bool g_RuntimeWasPlaying = false;

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

    static SceneInstance* AcquirePlayerInstance()
    {
        if (SceneInstance* existing = FindInstanceById(g_PlayerController.instanceId))
        {
            if (!existing->camera.enabled)
                return existing;
        }

        const uint32_t selectedId = Scene::GetSelectedInstanceId();
        if (SceneInstance* selected = FindInstanceById(selectedId))
        {
            if (!selected->camera.enabled)
            {
                g_PlayerController.instanceId = selected->instanceId;
                g_PlayerController.initializedLook = false;
                return selected;
            }
        }

        for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
        {
            SceneInstance* instance = Scene::GetInstance(instanceIndex);
            if (!instance || instance->camera.enabled)
                continue;

            g_PlayerController.instanceId = instance->instanceId;
            g_PlayerController.initializedLook = false;
            return instance;
        }

        g_PlayerController.instanceId = 0;
        g_PlayerController.initializedLook = false;
        return nullptr;
    }

    static float DistanceSquared(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static int FindMaterialIndexByName(const std::string& name)
    {
        MaterialManager& materials = MaterialManager::GetInstance();
        for (int i = 0; i < materials.GetMaterialCount(); ++i)
        {
            const std::string* materialName = materials.GetMaterialNameByIndex(i);
            if (materialName && *materialName == name)
                return i;
        }
        return -1;
    }

    static int GetOrCreateVaultMaterialIndex(const std::string& name, const DirectX::XMFLOAT3& color, float metallic = 0.0f, float roughness = 0.85f)
    {
        MaterialManager& materials = MaterialManager::GetInstance();
        Material* material = materials.GetMaterial(name);
        if (!material)
            material = materials.CreateMaterial(name);
        if (!material)
            return 0;

        material->SetBaseColor(color);
        material->SetFloat("metallic", metallic);
        material->SetFloat("roughness", roughness);

        const int index = FindMaterialIndexByName(name);
        return index >= 0 ? index : 0;
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

    static void ResetVaultRuntimeState(bool restoreSceneInstances)
    {
        if (restoreSceneInstances)
        {
            for (const VaultGameplayState::NodeBinding& node : g_VaultState.nodes)
            {
                if (SceneInstance* instance = FindInstanceById(node.instanceId))
                    instance->materialIndex = node.originalMaterialIndex;
            }
            for (const VaultGameplayState::RingBinding& ring : g_VaultState.rings)
            {
                if (SceneInstance* instance = FindInstanceById(ring.instanceId))
                {
                    instance->materialIndex = ring.originalMaterialIndex;
                    instance->rotation = ring.originalRotation;
                }
            }
            if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
                core->materialIndex = g_VaultState.core.originalMaterialIndex;

            Scene::RebuildRenderInstancesFromSceneData();
            Scene::MarkInstancesDirty();
        }

        g_InteractionTargetId = 0;
        g_VaultState = {};
    }

    static bool AreVaultBindingsValid()
    {
        if (!g_VaultState.initialized)
            return false;
        if (g_VaultState.core.instanceId != 0 && !FindInstanceById(g_VaultState.core.instanceId))
            return false;
        for (const VaultGameplayState::NodeBinding& node : g_VaultState.nodes)
        {
            if (!FindInstanceById(node.instanceId))
                return false;
        }
        for (const VaultGameplayState::RingBinding& ring : g_VaultState.rings)
        {
            if (!FindInstanceById(ring.instanceId))
                return false;
        }
        return true;
    }

    static void EnsureVaultMaterials()
    {
        g_VaultState.nodeInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Inactive", { 0.24f, 0.26f, 0.34f }, 0.0f, 0.95f);
        g_VaultState.nodeActiveMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Active", { 1.00f, 0.60f, 0.18f }, 0.1f, 0.35f);
        g_VaultState.coreInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Inactive", { 0.18f, 0.20f, 0.26f }, 0.0f, 0.95f);
        g_VaultState.coreActiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Active", { 1.00f, 0.82f, 0.32f }, 0.25f, 0.22f);
        g_VaultState.ringInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Inactive", { 0.32f, 0.24f, 0.18f }, 0.0f, 0.90f);
        g_VaultState.ringActiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Active", { 1.00f, 0.48f, 0.12f }, 0.15f, 0.32f);
    }

    static void DiscoverVaultObjects(uint32_t playerInstanceId)
    {
        EnsureVaultMaterials();

        VaultGameplayState newState{};
        newState.nodeInactiveMaterial = g_VaultState.nodeInactiveMaterial;
        newState.nodeActiveMaterial = g_VaultState.nodeActiveMaterial;
        newState.coreInactiveMaterial = g_VaultState.coreInactiveMaterial;
        newState.coreActiveMaterial = g_VaultState.coreActiveMaterial;
        newState.ringInactiveMaterial = g_VaultState.ringInactiveMaterial;
        newState.ringActiveMaterial = g_VaultState.ringActiveMaterial;

        for (const SceneInstance& instance : Scene::GetInstances())
        {
            if (instance.instanceId == playerInstanceId || instance.camera.enabled)
                continue;

            const VaultType vaultType = ClassifyVaultType(instance, newState.core.instanceId != 0);
            if (vaultType == VaultType::None)
                continue;

            switch (vaultType)
            {
            case VaultType::Core:
                if (newState.core.instanceId == 0)
                {
                    newState.core.instanceId = instance.instanceId;
                    newState.core.originalMaterialIndex = instance.materialIndex;
                }
                else
                {
                    newState.nodes.push_back({ instance.instanceId, false, instance.materialIndex });
                }
                break;
            case VaultType::Ring:
                newState.rings.push_back({ instance.instanceId, instance.materialIndex, instance.rotation });
                break;
            case VaultType::Node:
                newState.nodes.push_back({ instance.instanceId, false, instance.materialIndex });
                break;
            case VaultType::None:
            default:
                break;
            }
        }

        g_VaultState = std::move(newState);
        g_VaultState.initialized = true;
        g_VaultState.coreUnlocked = false;
        g_VaultState.unlockedLogged = false;
    }

    static void UpdateVaultVisualState(float deltaTime)
    {
        int activeNodeCount = 0;
        for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            SceneInstance* node = FindInstanceById(nodeBinding.instanceId);
            if (!node)
                continue;

            const bool isActive = nodeBinding.active;
            node->materialIndex = isActive ? g_VaultState.nodeActiveMaterial : g_VaultState.nodeInactiveMaterial;
            if (isActive)
                ++activeNodeCount;
        }

        const int totalNodes = static_cast<int>(g_VaultState.nodes.size());
        const int ringCount = static_cast<int>(g_VaultState.rings.size());
        const float ringProgress = (totalNodes > 0) ? static_cast<float>(activeNodeCount) / static_cast<float>(totalNodes) : 0.0f;
        const int completedRingCount = (ringCount > 0)
            ? (std::clamp)(static_cast<int>(std::floor(ringProgress * ringCount + 1e-4f)), 0, ringCount)
            : 0;

        for (int ringIndex = 0; ringIndex < ringCount; ++ringIndex)
        {
            const VaultGameplayState::RingBinding& ringBinding = g_VaultState.rings[(size_t)ringIndex];
            SceneInstance* ring = FindInstanceById(ringBinding.instanceId);
            if (!ring)
                continue;

            const bool isActiveRing = ringIndex < completedRingCount;
            ring->materialIndex = isActiveRing ? g_VaultState.ringActiveMaterial : g_VaultState.ringInactiveMaterial;
            const float spinSpeed = 0.35f + ringProgress * (0.85f + 0.15f * static_cast<float>(ringIndex));
            ring->rotation.y += deltaTime * spinSpeed;
        }

        const bool coreUnlocked = totalNodes > 0 && activeNodeCount >= totalNodes;
        g_VaultState.coreUnlocked = coreUnlocked;
        if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
            core->materialIndex = coreUnlocked ? g_VaultState.coreActiveMaterial : g_VaultState.coreInactiveMaterial;

        if (coreUnlocked && !g_VaultState.unlockedLogged)
        {
            g_VaultState.unlockedLogged = true;
            Logger::Log(LogLevel::Info, "Vault Core activated.", "[Game]");
        }
    }
}

bool Game::Initialize() {
    ResetVaultRuntimeState(false);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
    return true;
}

void Game::Update(float deltaTime) {
    g_InteractionTargetId = 0;

    if (!g_engineInstance)
        return;

    const Engine::State engineState = Engine::GetState();
    const bool runtimeActive = (engineState == Engine::State::Playing || engineState == Engine::State::Paused);
    if (!runtimeActive)
    {
        if (g_RuntimeWasPlaying)
        {
            ResetVaultRuntimeState(true);
            g_PlayerController = {};
            g_RuntimeWasPlaying = false;
        }
        return;
    }

    g_RuntimeWasPlaying = true;

    if (engineState == Engine::State::Paused)
        return;

    Graphics& graphics = Graphics::GetInstance();
    if (!graphics.IsGameViewportInputAllowed())
        return;

    SceneInstance* playerInstance = AcquirePlayerInstance();
    SceneInstance* mainCamera = Scene::GetMainCameraInstanceMutable();
    if (!playerInstance || !mainCamera || !mainCamera->camera.enabled)
        return;

    if (!AreVaultBindingsValid())
        DiscoverVaultObjects(playerInstance->instanceId);

    if (!g_PlayerController.initializedLook)
    {
        g_PlayerController.yaw = playerInstance->rotation.y;
        g_PlayerController.pitch = mainCamera->rotation.x;
        g_PlayerController.initializedLook = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool lookActive = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (lookActive)
    {
        g_PlayerController.yaw += io.MouseDelta.x * g_PlayerController.lookSensitivity;
        g_PlayerController.pitch -= io.MouseDelta.y * g_PlayerController.lookSensitivity;
        g_PlayerController.pitch = (std::clamp)(g_PlayerController.pitch, DirectX::XMConvertToRadians(-85.0f), DirectX::XMConvertToRadians(85.0f));
    }

    const bool moveForward = ImGui::IsKeyDown(ImGuiKey_W) || g_engineInstance->GetInput().IsKeyPressed('W');
    const bool moveBackward = ImGui::IsKeyDown(ImGuiKey_S) || g_engineInstance->GetInput().IsKeyPressed('S');
    const bool moveLeft = ImGui::IsKeyDown(ImGuiKey_A) || g_engineInstance->GetInput().IsKeyPressed('A');
    const bool moveRight = ImGui::IsKeyDown(ImGuiKey_D) || g_engineInstance->GetInput().IsKeyPressed('D');
    const bool speedBoost = io.KeyShift || g_engineInstance->GetInput().IsKeyPressed(VK_SHIFT);

    float moveSpeed = g_PlayerController.moveSpeed;
    if (speedBoost)
        moveSpeed *= 3.0f;

    const float yaw = g_PlayerController.yaw;
    const DirectX::XMFLOAT3 forward = {
        std::sinf(yaw),
        0.0f,
        std::cosf(yaw)
    };
    const DirectX::XMFLOAT3 right = {
        std::cosf(yaw),
        0.0f,
        -std::sinf(yaw)
    };

    DirectX::XMFLOAT3 moveDelta{ 0.0f, 0.0f, 0.0f };

    if (moveForward)
    {
        moveDelta.x += forward.x;
        moveDelta.y += forward.y;
        moveDelta.z += forward.z;
    }
    if (moveBackward)
    {
        moveDelta.x -= forward.x;
        moveDelta.y -= forward.y;
        moveDelta.z -= forward.z;
    }
    if (moveLeft)
    {
        moveDelta.x -= right.x;
        moveDelta.y -= right.y;
        moveDelta.z -= right.z;
    }
    if (moveRight)
    {
        moveDelta.x += right.x;
        moveDelta.y += right.y;
        moveDelta.z += right.z;
    }
    if (moveDelta.x != 0.0f || moveDelta.y != 0.0f || moveDelta.z != 0.0f)
    {
        DirectX::XMVECTOR move = DirectX::XMLoadFloat3(&moveDelta);
        move = DirectX::XMVector3Normalize(move);
        move = DirectX::XMVectorScale(move, moveSpeed * deltaTime);
        DirectX::XMStoreFloat3(&moveDelta, move);

        playerInstance->position.x += moveDelta.x;
        playerInstance->position.y += moveDelta.y;
        playerInstance->position.z += moveDelta.z;
    }

    playerInstance->rotation.y = g_PlayerController.yaw;
    mainCamera->position = {
        playerInstance->position.x,
        playerInstance->position.y + g_PlayerController.cameraHeight,
        playerInstance->position.z
    };
    mainCamera->rotation.x = g_PlayerController.pitch;
    mainCamera->rotation.y = g_PlayerController.yaw;
    mainCamera->rotation.z = 0.0f;

    constexpr float kInteractRange = 2.5f;
    const float interactRangeSq = kInteractRange * kInteractRange;
    uint32_t bestTargetId = 0;
    float bestDistSq = interactRangeSq;
    for (const VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
    {
        SceneInstance* instance = FindInstanceById(nodeBinding.instanceId);
        if (!instance || instance->instanceId == playerInstance->instanceId)
            continue;
        if (nodeBinding.active)
            continue;

        const float distSq = DistanceSquared(playerInstance->position, instance->position);
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestTargetId = instance->instanceId;
        }
    }

    g_InteractionTargetId = bestTargetId;

    const bool interactPressed = ImGui::IsKeyPressed(ImGuiKey_E, false) || g_engineInstance->GetInput().IsKeyJustPressed('E');
    if (bestTargetId != 0 && interactPressed)
    {
        for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            if (nodeBinding.instanceId == bestTargetId && !nodeBinding.active)
            {
                nodeBinding.active = true;
                g_InteractionTargetId = 0;
                break;
            }
        }
    }

    UpdateVaultVisualState(deltaTime);
}

void Game::Shutdown() {
    ResetVaultRuntimeState(true);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
}

bool Game::HasInteractionTarget() const
{
    return g_InteractionTargetId != 0;
}

uint32_t Game::GetInteractionTargetId() const
{
    return g_InteractionTargetId;
}

int Game::GetVaultActiveNodeCount() const
{
    int activeCount = 0;
    for (const VaultGameplayState::NodeBinding& node : g_VaultState.nodes)
    {
        if (node.active)
            ++activeCount;
    }
    return activeCount;
}

int Game::GetVaultTotalNodeCount() const
{
    return static_cast<int>(g_VaultState.nodes.size());
}

bool Game::IsVaultCoreUnlocked() const
{
    return g_VaultState.coreUnlocked;
}
