#include "Game.h"

#include "Engine.h"
#include "GameplayAudio.h"
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
        enum class NodeState : uint8_t
        {
            Inactive = 0,
            Active,
            Decaying,
        };

        struct NodeBinding
        {
            uint32_t instanceId = 0;
            NodeState state = NodeState::Inactive;
            float decayTimer = 0.0f;
            float decayDuration = 10.0f;
            bool warningPlayed = false;
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

        struct ExitBinding
        {
            uint32_t instanceId = 0;
            int originalMaterialIndex = 0;
            DirectX::XMFLOAT3 originalPosition{ 0.0f, 0.0f, 0.0f };
            float openOffsetY = 3.5f;
            bool opened = false;
        };

        std::vector<NodeBinding> nodes;
        std::vector<RingBinding> rings;
        CoreBinding core{};
        ExitBinding exit{};
        int nodeInactiveMaterial = 0;
        int nodeActiveMaterial = 0;
        int nodeDecayMaterial = 0;
        int coreInactiveMaterial = 0;
        int coreActiveMaterial = 0;
        int coreCompletedMaterial = 0;
        int exitLockedMaterial = 0;
        int exitUnlockedMaterial = 0;
        int ringInactiveMaterial = 0;
        int ringActiveMaterial = 0;
        int ringCompletedMaterial = 0;
        bool initialized = false;
        bool coreUnlocked = false;
        bool unlockedLogged = false;
    };

    struct VaultMission
    {
        VaultMissionState state = VaultMissionState::Inactive;
        int totalNodes = 0;
        int activatedNodes = 0;
        bool coreUnlocked = false;
    };

    static PlayerController g_PlayerController{};
    static uint32_t g_InteractionTargetId = 0;
    static VaultGameplayState g_VaultState{};
    static VaultMission g_VaultMission{};
    static bool g_RuntimeWasPlaying = false;
    static bool g_PlayerFrozen = false;
    static std::string g_RuntimeLevelStartSnapshot;
    static constexpr float kVaultNodeDecayDurationSeconds = 10.0f;
    static constexpr float kVaultNodeDecayWarningSeconds = 3.0f;
    static constexpr float kVaultExitTriggerRadius = 2.5f;

    static const char* GetMissionObjectiveText(VaultMissionState state)
    {
        switch (state)
        {
        case VaultMissionState::ActivatingNodes: return "Objective: Activate all vault nodes";
        case VaultMissionState::CoreUnlocked: return "Objective: Reach and stabilize the vault core";
        case VaultMissionState::Completed: return "Objective: Reach the opened exit";
        case VaultMissionState::Escaped: return "Vault Escaped!";
        case VaultMissionState::Failed: return "Mission Failed";
        case VaultMissionState::Inactive:
        default: return "Objective: Enter the vault";
        }
    }

    static bool IsInteractionTargetCore(uint32_t targetId)
    {
        return targetId != 0 && targetId == g_VaultState.core.instanceId;
    }

    static bool IsVaultExitName(const std::string& value)
    {
        std::string lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered.find("exit") != std::string::npos ||
            lowered.find("door") != std::string::npos ||
            lowered.find("gate") != std::string::npos ||
            lowered.find("barrier") != std::string::npos;
    }

    static VaultGameplayState::NodeBinding* FindNodeBindingById(uint32_t instanceId)
    {
        for (VaultGameplayState::NodeBinding& node : g_VaultState.nodes)
        {
            if (node.instanceId == instanceId)
                return &node;
        }
        return nullptr;
    }

    static const VaultGameplayState::NodeBinding* FindMostUrgentDecayingNode()
    {
        const VaultGameplayState::NodeBinding* bestNode = nullptr;
        for (const VaultGameplayState::NodeBinding& node : g_VaultState.nodes)
        {
            if (node.state != VaultGameplayState::NodeState::Decaying)
                continue;
            if (!bestNode || node.decayTimer < bestNode->decayTimer)
                bestNode = &node;
        }
        return bestNode;
    }

    static void TriggerVaultCompletion()
    {
        g_VaultMission.state = VaultMissionState::Completed;
        g_InteractionTargetId = 0;
        GA_Play(GameplayAudioEvent::CoreStabilized);
        if (g_VaultState.exit.instanceId != 0)
            GA_Play(GameplayAudioEvent::ExitOpened);
        Logger::Log(LogLevel::Info, "Vault core stabilized. Exit unlocked.", "[Game]");
    }

    static void TriggerVaultEscape()
    {
        if (g_VaultMission.state == VaultMissionState::Escaped)
            return;

        g_VaultMission.state = VaultMissionState::Escaped;
        g_PlayerFrozen = true;
        g_InteractionTargetId = 0;
        GA_Play(GameplayAudioEvent::EscapeTriggered);
        Logger::Log(LogLevel::Info, "Vault escaped.", "[Game]");
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

        auto isPreferredPlayerName = [](const std::string& name)
        {
            std::string lowered = name;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return lowered == "vaultrunner" || lowered == "player";
        };

        for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
        {
            SceneInstance* instance = Scene::GetInstance(instanceIndex);
            if (!instance || instance->camera.enabled)
                continue;
            if (!isPreferredPlayerName(instance->name))
                continue;

            g_PlayerController.instanceId = instance->instanceId;
            g_PlayerController.initializedLook = false;
            return instance;
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
            if (SceneInstance* exit = FindInstanceById(g_VaultState.exit.instanceId))
            {
                exit->materialIndex = g_VaultState.exit.originalMaterialIndex;
                exit->position = g_VaultState.exit.originalPosition;
            }

            Scene::RebuildRenderInstancesFromSceneData();
            Scene::MarkInstancesDirty();
        }

        g_InteractionTargetId = 0;
        g_VaultState = {};
        g_VaultMission = {};
        g_PlayerFrozen = false;
        g_RuntimeLevelStartSnapshot.clear();
        GA_Reset();
    }

    static bool AreVaultBindingsValid()
    {
        if (!g_VaultState.initialized)
            return false;
        if (g_VaultState.core.instanceId != 0 && !FindInstanceById(g_VaultState.core.instanceId))
            return false;
        if (g_VaultState.exit.instanceId != 0 && !FindInstanceById(g_VaultState.exit.instanceId))
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
        g_VaultState.nodeDecayMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Decaying", { 1.00f, 0.18f, 0.12f }, 0.08f, 0.24f);
        g_VaultState.coreInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Inactive", { 0.18f, 0.20f, 0.26f }, 0.0f, 0.95f);
        g_VaultState.coreActiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Active", { 1.00f, 0.82f, 0.32f }, 0.25f, 0.22f);
        g_VaultState.coreCompletedMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Completed", { 0.42f, 1.00f, 0.74f }, 0.35f, 0.14f);
        g_VaultState.exitLockedMaterial = GetOrCreateVaultMaterialIndex("VaultExit_Locked", { 0.42f, 0.18f, 0.18f }, 0.04f, 0.84f);
        g_VaultState.exitUnlockedMaterial = GetOrCreateVaultMaterialIndex("VaultExit_Unlocked", { 0.22f, 0.78f, 0.38f }, 0.12f, 0.26f);
        g_VaultState.ringInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Inactive", { 0.32f, 0.24f, 0.18f }, 0.0f, 0.90f);
        g_VaultState.ringActiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Active", { 1.00f, 0.48f, 0.12f }, 0.15f, 0.32f);
        g_VaultState.ringCompletedMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Completed", { 0.38f, 0.94f, 1.00f }, 0.20f, 0.18f);
    }

    static void DiscoverVaultObjects(uint32_t playerInstanceId)
    {
        EnsureVaultMaterials();

        VaultGameplayState newState{};
        newState.nodeInactiveMaterial = g_VaultState.nodeInactiveMaterial;
        newState.nodeActiveMaterial = g_VaultState.nodeActiveMaterial;
        newState.nodeDecayMaterial = g_VaultState.nodeDecayMaterial;
        newState.coreInactiveMaterial = g_VaultState.coreInactiveMaterial;
        newState.coreActiveMaterial = g_VaultState.coreActiveMaterial;
        newState.coreCompletedMaterial = g_VaultState.coreCompletedMaterial;
        newState.exitLockedMaterial = g_VaultState.exitLockedMaterial;
        newState.exitUnlockedMaterial = g_VaultState.exitUnlockedMaterial;
        newState.ringInactiveMaterial = g_VaultState.ringInactiveMaterial;
        newState.ringActiveMaterial = g_VaultState.ringActiveMaterial;
        newState.ringCompletedMaterial = g_VaultState.ringCompletedMaterial;

        for (const SceneInstance& instance : Scene::GetInstances())
        {
            if (instance.instanceId == playerInstanceId || instance.camera.enabled)
                continue;

            if (newState.exit.instanceId == 0 && IsVaultExitName(instance.name))
            {
                newState.exit.instanceId = instance.instanceId;
                newState.exit.originalMaterialIndex = instance.materialIndex;
                newState.exit.originalPosition = instance.position;
                continue;
            }

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
                    newState.nodes.push_back({ instance.instanceId, VaultGameplayState::NodeState::Inactive, 0.0f, kVaultNodeDecayDurationSeconds, false, instance.materialIndex });
                }
                break;
            case VaultType::Ring:
                newState.rings.push_back({ instance.instanceId, instance.materialIndex, instance.rotation });
                break;
            case VaultType::Node:
                newState.nodes.push_back({ instance.instanceId, VaultGameplayState::NodeState::Inactive, 0.0f, kVaultNodeDecayDurationSeconds, false, instance.materialIndex });
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
        g_VaultState.exit.opened = false;
        g_VaultMission.state = VaultMissionState::ActivatingNodes;
        g_VaultMission.totalNodes = static_cast<int>(g_VaultState.nodes.size());
        g_VaultMission.activatedNodes = 0;
        g_VaultMission.coreUnlocked = false;
    }

    static void UpdateVaultVisualState(float deltaTime)
    {
        int activeNodeCount = 0;
        for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            SceneInstance* node = FindInstanceById(nodeBinding.instanceId);
            if (!node)
                continue;

            if (g_VaultMission.state != VaultMissionState::Completed &&
                (nodeBinding.state == VaultGameplayState::NodeState::Active || nodeBinding.state == VaultGameplayState::NodeState::Decaying))
            {
                nodeBinding.decayTimer -= deltaTime;
                if (nodeBinding.decayTimer <= 0.0f)
                {
                    nodeBinding.decayTimer = 0.0f;
                    nodeBinding.state = VaultGameplayState::NodeState::Inactive;
                    nodeBinding.warningPlayed = false;
                    GA_Play(GameplayAudioEvent::NodeDecayed);
                }
                else if (nodeBinding.decayTimer <= kVaultNodeDecayWarningSeconds)
                {
                    nodeBinding.state = VaultGameplayState::NodeState::Decaying;
                    if (!nodeBinding.warningPlayed)
                    {
                        nodeBinding.warningPlayed = true;
                        GA_Play(GameplayAudioEvent::NodeWarning);
                    }
                }
                else
                {
                    nodeBinding.state = VaultGameplayState::NodeState::Active;
                }
            }

            switch (nodeBinding.state)
            {
            case VaultGameplayState::NodeState::Active:
                node->materialIndex = g_VaultState.nodeActiveMaterial;
                ++activeNodeCount;
                break;
            case VaultGameplayState::NodeState::Decaying:
                node->materialIndex = g_VaultState.nodeDecayMaterial;
                ++activeNodeCount;
                break;
            case VaultGameplayState::NodeState::Inactive:
            default:
                node->materialIndex = g_VaultState.nodeInactiveMaterial;
                break;
            }
        }

        const int totalNodes = static_cast<int>(g_VaultState.nodes.size());
        g_VaultMission.totalNodes = totalNodes;
        g_VaultMission.activatedNodes = activeNodeCount;
        const float tension = (totalNodes > 0)
            ? (1.0f - static_cast<float>(activeNodeCount) / static_cast<float>(totalNodes))
            : 0.0f;
        GA_SetTension(tension);
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

            const bool missionCompleted = (g_VaultMission.state == VaultMissionState::Completed);
            const bool isActiveRing = missionCompleted || ringIndex < completedRingCount;
            ring->materialIndex = missionCompleted ? g_VaultState.ringCompletedMaterial : (isActiveRing ? g_VaultState.ringActiveMaterial : g_VaultState.ringInactiveMaterial);
            const float spinSpeed = missionCompleted
                ? (1.6f + 0.2f * static_cast<float>(ringIndex))
                : (0.35f + ringProgress * (0.85f + 0.15f * static_cast<float>(ringIndex)));
            ring->rotation.y += deltaTime * spinSpeed;
        }

        const bool coreUnlocked = totalNodes > 0 && activeNodeCount >= totalNodes;
        g_VaultState.coreUnlocked = coreUnlocked;
        g_VaultMission.coreUnlocked = coreUnlocked;
        if (g_VaultMission.state != VaultMissionState::Completed)
            g_VaultMission.state = coreUnlocked ? VaultMissionState::CoreUnlocked : VaultMissionState::ActivatingNodes;
        if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
            core->materialIndex = (g_VaultMission.state == VaultMissionState::Completed)
                ? g_VaultState.coreCompletedMaterial
                : (coreUnlocked ? g_VaultState.coreActiveMaterial : g_VaultState.coreInactiveMaterial);

        if (SceneInstance* exit = FindInstanceById(g_VaultState.exit.instanceId))
        {
            const bool missionCompleted = (g_VaultMission.state == VaultMissionState::Completed);
            exit->materialIndex = missionCompleted ? g_VaultState.exitUnlockedMaterial : g_VaultState.exitLockedMaterial;
            const float targetY = g_VaultState.exit.originalPosition.y + (missionCompleted ? g_VaultState.exit.openOffsetY : 0.0f);
            const float moveSpeed = missionCompleted ? 2.8f : 4.5f;
            const float delta = targetY - exit->position.y;
            if (std::fabs(delta) > 0.001f)
            {
                const float step = (std::min)(std::fabs(delta), moveSpeed * deltaTime);
                exit->position.y += (delta > 0.0f) ? step : -step;
            }
            else
            {
                exit->position.y = targetY;
            }
            g_VaultState.exit.opened = missionCompleted;
        }

        if (coreUnlocked && !g_VaultState.unlockedLogged)
        {
            g_VaultState.unlockedLogged = true;
            GA_Play(GameplayAudioEvent::CoreUnlocked);
            Logger::Log(LogLevel::Info, "Vault Core activated.", "[Game]");
        }
    }
}

bool Game::Initialize() {
    ResetVaultRuntimeState(false);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
    g_PlayerFrozen = false;
    g_RuntimeLevelStartSnapshot.clear();
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

    if (!g_RuntimeWasPlaying)
        g_RuntimeLevelStartSnapshot = Scene::SerializeToString();

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

    const bool restartRequested = ImGui::IsKeyPressed(ImGuiKey_R, false) || g_engineInstance->GetInput().IsKeyJustPressed('R');
    const bool exitRequested = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || g_engineInstance->GetInput().IsKeyJustPressed(VK_RETURN);
    if (g_VaultMission.state == VaultMissionState::Escaped)
    {
        if (restartRequested && !g_RuntimeLevelStartSnapshot.empty())
        {
            if (Scene::LoadFromString(g_RuntimeLevelStartSnapshot))
            {
                ResetVaultRuntimeState(false);
                g_PlayerController = {};
                g_RuntimeWasPlaying = true;
                g_RuntimeLevelStartSnapshot = Scene::SerializeToString();
            }
        }
        else if (exitRequested)
        {
            Engine::StopPlayMode();
        }
        return;
    }

    UpdateVaultVisualState(deltaTime);

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
    if (!g_PlayerFrozen && (moveDelta.x != 0.0f || moveDelta.y != 0.0f || moveDelta.z != 0.0f))
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
    if (g_VaultMission.state == VaultMissionState::CoreUnlocked)
    {
        if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
        {
            const float distSq = DistanceSquared(playerInstance->position, core->position);
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestTargetId = core->instanceId;
            }
        }

    if (g_VaultMission.state == VaultMissionState::Completed)
    {
        if (SceneInstance* exit = FindInstanceById(g_VaultState.exit.instanceId))
        {
            const float distSq = DistanceSquared(playerInstance->position, exit->position);
            if (distSq <= kVaultExitTriggerRadius * kVaultExitTriggerRadius)
                TriggerVaultEscape();
        }
    }
    }
    else if (g_VaultMission.state == VaultMissionState::ActivatingNodes)
    {
        int bestPriority = -1;
        for (const VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            SceneInstance* instance = FindInstanceById(nodeBinding.instanceId);
            if (!instance || instance->instanceId == playerInstance->instanceId)
                continue;

            const float distSq = DistanceSquared(playerInstance->position, instance->position);
            if (distSq >= interactRangeSq)
                continue;

            int priority = 0;
            switch (nodeBinding.state)
            {
            case VaultGameplayState::NodeState::Decaying: priority = 2; break;
            case VaultGameplayState::NodeState::Inactive: priority = 1; break;
            case VaultGameplayState::NodeState::Active:
            default: priority = 0; break;
            }

            if (priority > bestPriority || (priority == bestPriority && distSq < bestDistSq))
            {
                bestPriority = priority;
                bestDistSq = distSq;
                bestTargetId = instance->instanceId;
            }
        }
    }

    g_InteractionTargetId = bestTargetId;

    const bool interactPressed = ImGui::IsKeyPressed(ImGuiKey_E, false) || g_engineInstance->GetInput().IsKeyJustPressed('E');
    if (bestTargetId != 0 && interactPressed)
    {
        if (IsInteractionTargetCore(bestTargetId) && g_VaultMission.state == VaultMissionState::CoreUnlocked)
        {
            TriggerVaultCompletion();
        }
        else
        {
            for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
            {
                if (nodeBinding.instanceId == bestTargetId)
                {
                    nodeBinding.state = VaultGameplayState::NodeState::Active;
                    nodeBinding.decayTimer = nodeBinding.decayDuration;
                    nodeBinding.warningPlayed = false;
                    GA_Play(GameplayAudioEvent::NodeActivated);
                    g_InteractionTargetId = 0;
                    break;
                }
            }
        }
    }
}

void Game::Shutdown() {
    ResetVaultRuntimeState(true);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
}

VaultMissionState Game::GetVaultMissionState() const
{
    return g_VaultMission.state;
}

const char* Game::GetVaultMissionObjectiveText() const
{
    return GetMissionObjectiveText(g_VaultMission.state);
}

bool Game::HasInteractionTarget() const
{
    return g_InteractionTargetId != 0;
}

uint32_t Game::GetInteractionTargetId() const
{
    return g_InteractionTargetId;
}

const char* Game::GetInteractionPrompt() const
{
    if (!HasInteractionTarget())
        return nullptr;
    if (IsInteractionTargetCore(g_InteractionTargetId))
        return "Press E to stabilize the core";

    const VaultGameplayState::NodeBinding* nodeBinding = FindNodeBindingById(g_InteractionTargetId);
    if (!nodeBinding)
        return "Press E to activate node";

    return nodeBinding->state == VaultGameplayState::NodeState::Inactive
        ? "Press E to activate node"
        : "Press E to refresh node stability";
}

const char* Game::GetInteractionActionLabel() const
{
    if (!HasInteractionTarget())
        return "Interact";
    if (IsInteractionTargetCore(g_InteractionTargetId))
        return "Stabilize";

    const VaultGameplayState::NodeBinding* nodeBinding = FindNodeBindingById(g_InteractionTargetId);
    if (!nodeBinding)
        return "Activate";
    return nodeBinding->state == VaultGameplayState::NodeState::Inactive ? "Activate" : "Refresh";
}

bool Game::HasVaultWarning() const
{
    return FindMostUrgentDecayingNode() != nullptr;
}

const char* Game::GetVaultWarningText() const
{
    const VaultGameplayState::NodeBinding* warningNode = FindMostUrgentDecayingNode();
    if (!warningNode)
        return nullptr;
    if (warningNode->instanceId == g_InteractionTargetId)
        return "Node destabilizing! Refresh it now.";
    return "Node destabilizing!";
}

int Game::GetVaultActiveNodeCount() const
{
    return g_VaultMission.activatedNodes;
}

int Game::GetVaultTotalNodeCount() const
{
    return static_cast<int>(g_VaultState.nodes.size());
}

bool Game::IsVaultCoreUnlocked() const
{
    return g_VaultMission.coreUnlocked;
}

bool Game::IsVaultMissionCompleted() const
{
    return g_VaultMission.state == VaultMissionState::Completed || g_VaultMission.state == VaultMissionState::Escaped;
}

bool Game::IsVaultMissionEscaped() const
{
    return g_VaultMission.state == VaultMissionState::Escaped;
}
