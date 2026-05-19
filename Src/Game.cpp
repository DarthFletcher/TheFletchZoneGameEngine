#include "Game.h"

#include "Engine.h"
#include "GameplayAudio.h"
#include "Graphics.h"
#include "MaterialManager.h"
#include "Scene.h"
#include "UI.h"
#include "VaultDiscovery.h"
#include "VaultNodeSystem.h"
#include "VaultRuntime.h"
#include "logger.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

extern Engine* g_engineInstance;

namespace
{
    struct PlayerController
    {
        uint32_t instanceId = 0;
        RuntimeEntityId playerEntity = kInvalidRuntimeEntityId;
        RuntimeEntityId cameraEntity = kInvalidRuntimeEntityId;
        float moveSpeed = 5.0f;
        float lookSensitivity = 0.0018f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float cameraHeight = 1.6f;
        bool initializedLook = false;
    };

    enum class VaultMoodType : uint8_t
    {
        Calm = 0,
        Tension,
        Critical,
    };

    struct VaultMoodSettings
    {
        float lightIntensity = 1.0f;
        float ambient = 0.12f;
        DirectX::XMFLOAT3 lightColor{ 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT3 colorTint{ 1.0f, 1.0f, 1.0f };
        const char* skyPreset = "Sunset";
        DirectX::XMFLOAT3 skyTint{ 1.0f, 1.0f, 1.0f };
        float skyIntensity = 1.0f;
        float skyExposure = 0.0f;
        float nodePulseSpeed = 1.0f;
        float nodePulseStrength = 0.2f;
        float audioTension = 0.2f;
    };

    struct VaultMoodRuntimeState
    {
        bool applied = false;
        float elapsedTime = 0.0f;
        SceneSkyboxSettings originalSkybox{};
        Graphics::DirectionalLight originalLight{};
    };

	// Note: these static variables are not thread safe, but that's fine since the game is single-threaded for now. If we ever need to support multiple threads, we can refactor these into a proper GameState class and add synchronization as needed.
    static PlayerController g_PlayerController{};
    static uint32_t g_InteractionTargetId = 0;
    static VaultGameplayState g_VaultState{};
    static VaultMission g_VaultMission{};
    static VaultPresentationState g_VaultPresentation{};
    static VaultScannerState g_VaultScanner{};
    static VaultContextHintState g_VaultContextHint{};
    static VaultMoodRuntimeState g_VaultMoodRuntime{};
    static bool g_RuntimeWasPlaying = false;
    static bool g_PlayerFrozen = false;
    static std::string g_RuntimeLevelStartSnapshot;
    static constexpr float kVaultNodeDecayDurationSeconds = 9.0f;
    static constexpr float kVaultNodeDecayWarningSeconds = 3.0f;
    static constexpr float kVaultExitTriggerRadius = 2.5f;
    static constexpr int kVaultMaxDecayedNodes = 3;
    static constexpr float kVaultPlayerSprintMultiplier = 1.5f;
    static constexpr float kVaultInteractionRange = 2.0f;
    static constexpr float kVaultStartBannerSeconds = 1.35f;
    static constexpr float kVaultEscapeBannerSeconds = 1.35f;
    static constexpr float kVaultFailPulseSeconds = 0.55f;
    static constexpr float kVaultAutoAdvanceSeconds = 5.0f;
    static constexpr float kVaultContextHintSeconds = 5.0f;

    static std::string ToLower(std::string value);
    static void EnsureVaultMaterials();

    static void RefreshRuntimeWorldFromCurrentScene(const char* reason)
    {
        if (!g_engineInstance)
            return;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        if (runtimeWorld.CloneFromScene())
        {
            const RuntimeWorldStats stats = runtimeWorld.GetStats();
            Logger::Log(LogLevel::Info,
                std::format("RuntimeWorld refreshed after {}: {} entities, players={}, vaultNodes={}, vaultCores={}, vaultRings={}, vaultExits={}.",
                    reason,
                    stats.entities,
                    stats.playerControllers,
                    stats.vaultNodes,
                    stats.vaultCores,
                    stats.vaultRings,
                    stats.vaultExits),
                "[Game]");
        }
        else
        {
            Logger::Log(LogLevel::Warning, std::format("RuntimeWorld refresh failed after {}.", reason), "[Game]");
        }
    }

    static RuntimeWorld* GetRuntimeWorldForGameplay()
    {
        return g_engineInstance ? &g_engineInstance->GetRuntimeWorld() : nullptr;
    }

    static DirectX::XMFLOAT3 ScaleColor(const DirectX::XMFLOAT3& color, float scale)
    {
        return {
            std::clamp(color.x * scale, 0.0f, 1.0f),
            std::clamp(color.y * scale, 0.0f, 1.0f),
            std::clamp(color.z * scale, 0.0f, 1.0f)
        };
    }

    static DirectX::XMFLOAT3 MultiplyColor(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        return {
            std::clamp(a.x * b.x, 0.0f, 1.0f),
            std::clamp(a.y * b.y, 0.0f, 1.0f),
            std::clamp(a.z * b.z, 0.0f, 1.0f)
        };
    }

    static DirectX::XMFLOAT3 LerpColor(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
    {
        const float clampedT = std::clamp(t, 0.0f, 1.0f);
        return {
            a.x + (b.x - a.x) * clampedT,
            a.y + (b.y - a.y) * clampedT,
            a.z + (b.z - a.z) * clampedT
        };
    }

    static VaultMoodType GetCurrentVaultMoodType()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        if (currentFilename == "vault_traversal.scene")
            return VaultMoodType::Tension;
        if (currentFilename == "vault_priority.scene")
            return VaultMoodType::Critical;
        return VaultMoodType::Calm;
    }

	// Returns the visual and audio settings for the given vault mood type. This is used to apply the appropriate lighting, skybox, color grading, and audio parameters for each vault scene. The settings are designed to create a distinct atmosphere for each vault: the "Traversal" vault has a tense mood with warm colors and moderate tension, the "Priority" vault has a critical mood with redder colors and high tension, and the tutorial scenes have a calm mood with cooler colors and low tension.
    static VaultMoodSettings GetVaultMoodSettings(VaultMoodType type)
    {
        switch (type)
        {
        case VaultMoodType::Tension:
            return {
                1.0f,
                0.10f,
                { 1.0f, 0.72f, 0.56f },
                { 1.0f, 0.68f, 0.48f },
                "Sunset",
                { 1.0f, 0.74f, 0.58f },
                0.95f,
                -0.08f,
                1.9f,
                0.34f,
                0.58f
            };
        case VaultMoodType::Critical:
            return {
                0.62f,
                0.07f,
                { 1.0f, 0.34f, 0.30f },
                { 1.0f, 0.34f, 0.38f },
                "Night",
                { 1.0f, 0.42f, 0.42f },
                0.72f,
                -0.22f,
                3.2f,
                0.56f,
                0.92f
            };
        case VaultMoodType::Calm:
        default:
            return {
                0.82f,
                0.16f,
                { 0.74f, 0.84f, 1.0f },
                { 0.66f, 0.78f, 1.0f },
                "Day",
                { 0.72f, 0.82f, 1.0f },
                0.88f,
                -0.02f,
                0.9f,
                0.14f,
                0.20f
            };
        }
    }

	// Maps the vault mood type to the corresponding gameplay audio mood. This is used to determine which audio loops and event variations to use based on the current vault scene. For example, the "Traversal" vault has a tense mood with more aggressive audio, while the "Priority" vault has a critical mood with even more intense audio. The tutorial scenes use a calm mood with more subdued audio.
    static GameplayAudioMood GetVaultGameplayAudioMood(VaultMoodType type)
    {
        switch (type)
        {
        case VaultMoodType::Tension: return GameplayAudioMood::Tension;
        case VaultMoodType::Critical: return GameplayAudioMood::Critical;
        case VaultMoodType::Calm:
        default:
            return GameplayAudioMood::Calm;
        }
    }
	
    // Returns the path to the next vault scene to load, or nullptr if there is no next vault. This is used for both the auto-advance and the "Next Vault" button, so it should only return a non-null value if the player can actually advance to the next vault.
    static const char* GetNextVaultScenePath()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        if (currentFilename == "vault_intro.scene" || currentFilename == "main.scene")
            return "Assets/Scenes/Vault_Traversal.scene";
        if (currentFilename == "vault_traversal.scene")
            return "Assets/Scenes/Vault_Priority.scene";
        if (currentFilename == "vault_priority.scene")
            return "Assets/Scenes/Main.scene";
        return nullptr;
    }

    static bool IsFinalVaultSceneCurrentScene()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        return currentFilename == "vault_priority.scene";
    }

    static bool ShouldAutoAdvanceToNextVault()
    {
        return GetNextVaultScenePath() != nullptr && !IsFinalVaultSceneCurrentScene();
    }
	
    // Returns the progression label for the current vault scene, or nullptr if the current scene is not a vault or if the progression should not be shown (e.g. in the tutorial).
    static const char* GetVaultProgressionLabelForCurrentScene()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        if (currentFilename == "vault_intro.scene" || currentFilename == "main.scene")
            return "Vault 1 / 3";
        if (currentFilename == "vault_traversal.scene")
            return "Vault 2 / 3";
        if (currentFilename == "vault_priority.scene")
            return "Vault 3 / 3";
        return nullptr;
    }

	// Returns true if the current scene is a tutorial scene. This is used to determine whether to show the tutorial hints and header, and also to gate certain logic that should only run in the tutorial (e.g. the node decay timers and the auto-advance).
    static bool IsTutorialSceneCurrentScene()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        return currentFilename == "vault_intro.scene" || currentFilename == "main.scene";
    }

	// Returns the header text to show in the tutorial, or nullptr if the header should not be shown. This is used to show the "Tutorial" header in the tutorial scenes, and to hide the header in the vault scenes since they have their own progression label.
    static const char* GetTutorialHeaderText()
    {
        return IsTutorialSceneCurrentScene() ? "Tutorial" : nullptr;
    }

	// Returns the primary hint text to show in the tutorial, or nullptr if the hints should not be shown. This function checks the current vault mission state and returns the appropriate hint for the player based on their progress. For example, if they are in the "ActivatingNodes" state and have not activated any nodes yet, it will prompt them to activate a node. If they have activated some nodes but there are decaying nodes, it will warn them about the red nodes. If all nodes are active, it will tell them to keep them all active to unlock the core. Similar logic applies for the other states like "CoreUnlocked", "Completed", "Failed", and "Escaped". If they are not in a tutorial scene, it returns nullptr to hide the hints.
    static const char* GetTutorialHintPrimaryText()
    {
        if (!IsTutorialSceneCurrentScene())
            return nullptr;

        switch (g_VaultMission.state)
        {
        case VaultMissionState::ActivatingNodes:
            if (g_VaultMission.activatedNodes <= 0)
                return "Step 1: Find a nearby node and press E to activate it.";
            if (VaultNodeSystem::FindMostUrgentDecayingNode(g_VaultState))
                return "If a node turns red, go back and refresh it before it fully decays.";
            return "Keep all nodes active at the same time to unlock the core.";
        case VaultMissionState::CoreUnlocked:
            return "Step 2: The core is unlocked. Move to it and press E to stabilize it.";
        case VaultMissionState::Completed:
            return "Step 3: The exit is open. Walk through the gate area to escape.";
        case VaultMissionState::Failed:
            return "Too many nodes decayed. Retry and keep a tighter loop between nodes.";
        case VaultMissionState::Escaped:
            return "Tutorial complete. Vault 2 adds more traversal pressure.";
        case VaultMissionState::Inactive:
        default:
            return "Learn the loop: nodes -> core -> exit.";
        }
    }

	// Returns the secondary hint text to show in the tutorial, or nullptr if the hints should not be shown. Similar to GetTutorialHintPrimaryText, this function checks the current vault mission state and returns additional hints for the player. For example, in the "ActivatingNodes" state, it will show the basic controls for looking around, moving, sprinting, and interacting. In the "CoreUnlocked" state, it will explain that all nodes need to be active to interact with the core. In the "Completed" state, it will clarify that the gate raises but the trigger stays on the floor. In the "Failed" state, it will explain what red and orange nodes mean and how to restart. In the "Escaped" state, it will give instructions for advancing to the next vault. If they are not in a tutorial scene, it returns nullptr to hide the hints.
    static const char* GetTutorialHintSecondaryText()
    {
        if (!IsTutorialSceneCurrentScene())
            return nullptr;

        switch (g_VaultMission.state)
        {
        case VaultMissionState::ActivatingNodes:
            return "RMB look | WASD move | Shift sprint | E interact | Red means urgent";
        case VaultMissionState::CoreUnlocked:
            return "When every node stays active together, the core becomes interactable.";
        case VaultMissionState::Completed:
            return "The gate raises upward, but the escape trigger stays on the floor path below it.";
        case VaultMissionState::Failed:
            return "Orange means stable. Red means urgent. Press R to restart anytime.";
        case VaultMissionState::Escaped:
            return "Press N, click Next Vault, or wait for the auto-advance.";
        case VaultMissionState::Inactive:
        default:
            return "Nodes decay slowly here so you can learn the rhythm.";
        }
    }

	// Returns the label for the action to advance to the next vault, or nullptr if the next vault cannot be advanced to yet. This function checks the current scene and returns a prompt to press N for the next vault if the player is in a vault scene that has a subsequent vault. If they are in the first vault, it prompts for Vault 2. If they are in the second vault, it prompts for Vault 3. If they are not in a vault scene or if there is no next vault, it returns nullptr to hide the prompt.
    static const char* GetNextVaultActionLabel()
    {
        const std::filesystem::path currentScenePath(UI::GetCurrentSceneAssetPath());
        const std::string currentFilename = ToLower(currentScenePath.filename().string());
        if (currentFilename == "vault_intro.scene" || currentFilename == "main.scene")
            return "Press N / A for Vault 2";
        if (currentFilename == "vault_traversal.scene")
            return "Press N / A for Vault 3";
        if (currentFilename == "vault_priority.scene")
            return "Press N / A to Restart From Vault 1";
        return nullptr;
    }

    static const char* GetNextVaultButtonLabelForCurrentScene()
    {
        return IsFinalVaultSceneCurrentScene() ? "Restart Campaign" : "Next Vault";
    }

	// Returns the objective text to show based on the current vault mission state. This function checks the current state of the vault mission and returns a string that describes the player's current objective. For example, if they are in the "ActivatingNodes" state, it will prompt them to activate all vault nodes. If they are in the "CoreUnlocked" state, it will tell them to reach and stabilize the vault core. If they have completed the mission, it will instruct them to reach the opened exit. If they have escaped, it will simply say "Vault Escaped!". If they have failed, it will say "Mission Failed". If they are inactive or in any other state, it will prompt them to enter the vault. This text is used in the UI to keep the player informed about their current goal.
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

	// Returns true if the given targetId matches the current interaction target, which is typically set to the core when it becomes interactable. This is used to determine whether the player is currently able to interact with the core, and to gate certain logic that should only run when the core is the interaction target (e.g. showing the prompt to stabilize the core, allowing the player to trigger vault completion, etc.). The targetId of 0 is reserved to mean "no target", so this function also checks that the targetId is not 0 before comparing it to the core's instanceId.
    static bool IsInteractionTargetCore(uint32_t targetId)
    {
        return targetId != 0 && targetId == g_VaultState.core.instanceId;
    }

    static VaultRingComponent* FindRingRuntimeComponent(VaultGameplayState::RingBinding& ringBinding)
    {
        if (!g_engineInstance)
            return nullptr;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        if (ringBinding.runtimeEntity == kInvalidRuntimeEntityId)
            ringBinding.runtimeEntity = runtimeWorld.FindBySourceSceneInstanceId(ringBinding.instanceId);

        return ringBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld.GetVaultRing(ringBinding.runtimeEntity)
            : nullptr;
    }

    static RuntimeTransformComponent* FindRingRuntimeTransform(VaultGameplayState::RingBinding& ringBinding)
    {
        if (!g_engineInstance)
            return nullptr;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        if (ringBinding.runtimeEntity == kInvalidRuntimeEntityId)
            ringBinding.runtimeEntity = runtimeWorld.FindBySourceSceneInstanceId(ringBinding.instanceId);

        return ringBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld.GetTransform(ringBinding.runtimeEntity)
            : nullptr;
    }

    static void SyncRingBindingToRuntime(VaultGameplayState::RingBinding& ringBinding, bool active, bool completed)
    {
        VaultRingComponent* runtimeRing = FindRingRuntimeComponent(ringBinding);
        if (!runtimeRing)
            return;

        runtimeRing->active = active;
        runtimeRing->completed = completed;
    }

    static VaultCoreComponent* FindCoreRuntimeComponent(VaultGameplayState::CoreBinding& coreBinding)
    {
        if (!g_engineInstance)
            return nullptr;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        if (coreBinding.runtimeEntity == kInvalidRuntimeEntityId)
            coreBinding.runtimeEntity = runtimeWorld.FindBySourceSceneInstanceId(coreBinding.instanceId);

        return coreBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld.GetVaultCore(coreBinding.runtimeEntity)
            : nullptr;
    }

    static VaultExitComponent* FindExitRuntimeComponent(VaultGameplayState::ExitBinding& exitBinding)
    {
        if (!g_engineInstance)
            return nullptr;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        if (exitBinding.runtimeEntity == kInvalidRuntimeEntityId)
            exitBinding.runtimeEntity = runtimeWorld.FindBySourceSceneInstanceId(exitBinding.instanceId);

        return exitBinding.runtimeEntity != kInvalidRuntimeEntityId
            ? runtimeWorld.GetVaultExit(exitBinding.runtimeEntity)
            : nullptr;
    }

    static void SyncCoreBindingFromRuntime(VaultGameplayState::CoreBinding& coreBinding)
    {
        VaultCoreComponent* runtimeCore = FindCoreRuntimeComponent(coreBinding);
        if (!runtimeCore)
            return;

        g_VaultState.coreUnlocked = runtimeCore->unlocked;
        g_VaultMission.coreUnlocked = runtimeCore->unlocked;
    }

    static void SyncCoreBindingToRuntime(VaultGameplayState::CoreBinding& coreBinding)
    {
        VaultCoreComponent* runtimeCore = FindCoreRuntimeComponent(coreBinding);
        if (!runtimeCore)
            return;

        runtimeCore->unlocked = g_VaultMission.coreUnlocked || g_VaultState.coreUnlocked;
        runtimeCore->stabilized = g_VaultMission.state == VaultMissionState::Completed ||
            g_VaultMission.state == VaultMissionState::Escaped;
    }

    static void SyncExitBindingFromRuntime(VaultGameplayState::ExitBinding& exitBinding)
    {
        VaultExitComponent* runtimeExit = FindExitRuntimeComponent(exitBinding);
        if (!runtimeExit)
            return;

        exitBinding.openOffsetY = runtimeExit->openOffsetY;
        exitBinding.opened = runtimeExit->opened;
    }

    static void SyncExitBindingToRuntime(VaultGameplayState::ExitBinding& exitBinding)
    {
        VaultExitComponent* runtimeExit = FindExitRuntimeComponent(exitBinding);
        if (!runtimeExit)
            return;

        runtimeExit->unlocked = g_VaultMission.state == VaultMissionState::Completed ||
            g_VaultMission.state == VaultMissionState::Escaped;
        runtimeExit->opened = exitBinding.opened;
        runtimeExit->openOffsetY = exitBinding.openOffsetY;
    }

    static void ShowVaultContextHint(VaultContextHintState::HintType hintType)
    {
        switch (hintType)
        {
        case VaultContextHintState::HintType::SlowNode:
            if (g_VaultContextHint.shownSlowNode)
                return;
            g_VaultContextHint.shownSlowNode = true;
            break;
        case VaultContextHintState::HintType::FragileNode:
            if (g_VaultContextHint.shownFragileNode)
                return;
            g_VaultContextHint.shownFragileNode = true;
            break;
        case VaultContextHintState::HintType::None:
        default:
            return;
        }

        g_VaultContextHint.activeHint = hintType;
        g_VaultContextHint.timer = kVaultContextHintSeconds;
    }

    static void ApplyVaultMoodSettings(float deltaTime)
    {
        EnsureVaultMaterials();

        const VaultMoodType moodType = GetCurrentVaultMoodType();
        const VaultMoodSettings mood = GetVaultMoodSettings(moodType);
        Graphics& graphics = Graphics::GetInstance();
        GA_SetMood(GetVaultGameplayAudioMood(moodType));

        if (!g_VaultMoodRuntime.applied)
        {
            g_VaultMoodRuntime.originalSkybox = Scene::GetSkyboxSettings();
            g_VaultMoodRuntime.originalLight = graphics.GetDirectionalLight();

            SceneSkyboxSettings skybox = g_VaultMoodRuntime.originalSkybox;
            skybox.enabled = true;
            skybox.useCubemap = false;
            skybox.builtInPreset = mood.skyPreset;
            skybox.tint = mood.skyTint;
            skybox.intensity = mood.skyIntensity;
            skybox.exposure = mood.skyExposure;
            Scene::SetSkyboxSettings(skybox);

            g_VaultMoodRuntime.applied = true;
            g_VaultMoodRuntime.elapsedTime = 0.0f;
        }

        g_VaultMoodRuntime.elapsedTime += deltaTime;

        Graphics::DirectionalLight light = g_VaultMoodRuntime.originalLight;
        const float moodPulse = 0.5f + 0.5f * std::sin(g_VaultMoodRuntime.elapsedTime * mood.nodePulseSpeed);
        const float lightFlicker = 1.0f + (moodPulse - 0.5f) * 0.08f * mood.nodePulseStrength;
        light.intensity = mood.lightIntensity * lightFlicker;
        light.ambient = mood.ambient;
        light.color = mood.lightColor;
        graphics.GetDirectionalLight() = light;

        MaterialManager& materials = MaterialManager::GetInstance();
        const float pulseBoost = mood.nodePulseStrength * moodPulse;

        auto setMoodMaterial = [&](int materialIndex, const DirectX::XMFLOAT3& baseColor, float tintBlend, float pulseScale)
        {
            if (Material* material = materials.GetMaterialByIndex(materialIndex))
            {
                const DirectX::XMFLOAT3 tinted = LerpColor(baseColor, MultiplyColor(baseColor, mood.colorTint), tintBlend);
                material->SetBaseColor(ScaleColor(tinted, 1.0f + pulseBoost * pulseScale));
            }
        };

        setMoodMaterial(g_VaultState.nodeInactiveMaterial, { 0.16f, 0.18f, 0.24f }, 0.28f, 0.00f);
        setMoodMaterial(g_VaultState.nodeSlowMaterial, { 0.34f, 0.24f, 0.52f }, 0.34f, 0.06f);
        setMoodMaterial(g_VaultState.nodeFragileMaterial, { 0.42f, 0.18f, 0.16f }, 0.36f, 0.08f);
        setMoodMaterial(g_VaultState.nodeActiveMaterial, { 0.76f, 0.44f, 0.14f }, 0.24f, 0.35f);
        setMoodMaterial(g_VaultState.nodeDecayMaterial, { 0.82f, 0.16f, 0.14f }, 0.30f, 0.52f);
        setMoodMaterial(g_VaultState.coreInactiveMaterial, { 0.18f, 0.20f, 0.26f }, 0.22f, 0.02f);
        setMoodMaterial(g_VaultState.coreActiveMaterial, { 1.00f, 0.82f, 0.32f }, 0.24f, 0.48f);
        setMoodMaterial(g_VaultState.coreCompletedMaterial, { 0.42f, 1.00f, 0.74f }, 0.26f, 0.62f);
        setMoodMaterial(g_VaultState.exitLockedMaterial, { 0.42f, 0.18f, 0.18f }, 0.18f, 0.04f);
        setMoodMaterial(g_VaultState.exitUnlockedMaterial, { 0.22f, 0.78f, 0.38f }, 0.24f, 0.30f);
        setMoodMaterial(g_VaultState.ringInactiveMaterial, { 0.32f, 0.24f, 0.18f }, 0.18f, 0.02f);
        setMoodMaterial(g_VaultState.ringActiveMaterial, { 1.00f, 0.48f, 0.12f }, 0.22f, 0.28f);
        setMoodMaterial(g_VaultState.ringCompletedMaterial, { 0.38f, 0.94f, 1.00f }, 0.20f, 0.36f);
    }

	// Resets the vault runtime state to the initial conditions of the current level. If restoreSceneInstances is true, it will also restore the original transform and material state of all scene instances that are part of the vault gameplay (nodes, rings, core, exit) based on the data captured when they were first initialized. This is used when restarting a vault run to ensure that all gameplay elements are reset to their starting state, while allowing for flexibility in whether the scene instances themselves are reloaded from a snapshot or just reset in place.
    static void ResetVaultRuntimeState(bool restoreSceneInstances);

	// Triggers the vault completion sequence. This function sets the vault mission state to completed, resets the interaction target, plays the core stabilized audio event, checks if the exit was defined and plays the exit opened audio event if so, and logs a message indicating that the vault core has been stabilized and the exit is unlocked. This is called when the player successfully interacts with the core after activating all nodes, marking the main objective of the vault as complete and allowing the player to proceed to the exit.
    static void TriggerVaultCompletion()
    {
        g_VaultMission.state = VaultMissionState::Completed;
        g_InteractionTargetId = 0;
        g_VaultState.coreUnlocked = true;
        g_VaultMission.coreUnlocked = true;
        g_VaultState.exit.opened = true;
        SyncCoreBindingToRuntime(g_VaultState.core);
        SyncExitBindingToRuntime(g_VaultState.exit);
        GA_Play(GameplayAudioEvent::CoreStabilized);
        if (g_VaultState.exit.instanceId != 0)
            GA_Play(GameplayAudioEvent::ExitOpened);
        Logger::Log(LogLevel::Info, "Vault core stabilized. Exit unlocked.", "[Game]");
    }

	// Triggers the vault escape sequence. This function checks if the vault mission is already in an escaped state, and if not, it sets the mission state to escaped, freezes player input, resets the interaction target, sets up the vault presentation state to show the escape banner and start the auto-advance timer for progression, plays the escape audio event, and logs a message indicating that the vault has been escaped. This is called when the player successfully reaches the exit after stabilizing the core and completing the vault objectives.
    static void TriggerVaultEscape()
    {
        if (g_VaultMission.state == VaultMissionState::Escaped)
            return;

        g_VaultMission.state = VaultMissionState::Escaped;
        g_PlayerFrozen = true;
        g_InteractionTargetId = 0;
        g_VaultState.exit.opened = true;
        SyncCoreBindingToRuntime(g_VaultState.core);
        SyncExitBindingToRuntime(g_VaultState.exit);
        g_VaultPresentation.bannerType = VaultPresentationState::BannerType::Escape;
        g_VaultPresentation.bannerTimer = kVaultEscapeBannerSeconds;
        g_VaultPresentation.nextVaultAutoAdvanceTimer = ShouldAutoAdvanceToNextVault() ? kVaultAutoAdvanceSeconds : 0.0f;
        if (IsFinalVaultSceneCurrentScene())
        {
            GA_Play(GameplayAudioEvent::CampaignCompleted);
        }
        else
        {
            GA_Play(GameplayAudioEvent::EscapeTriggered);
        }
        Logger::Log(LogLevel::Info, "Vault escaped.", "[Game]");
    }

	// Fails the vault mission. This function checks if the mission is already in a failed or escaped state, and if not, it sets the mission state to failed, freezes player input, resets the interaction target, starts the fail pulse timer for presentation effects, logs a message to the player about retrying, and plays the failure audio event. This is called when too many nodes have decayed or when other failure conditions are met during vault gameplay.
    static void TriggerVaultFailure()
    {
        if (g_VaultMission.state == VaultMissionState::Failed ||
            g_VaultMission.state == VaultMissionState::Escaped)
            return;

        g_VaultMission.state = VaultMissionState::Failed;
        g_PlayerFrozen = true;
        g_InteractionTargetId = 0;
        g_VaultPresentation.failPulseTimer = kVaultFailPulseSeconds;
        Logger::Log(LogLevel::Info, "Vault failed. Press R to retry.", "[Game]");
        GA_Play(GameplayAudioEvent::FailureTriggered);
    }

	// Resets the vault runtime state to the initial conditions of the current level. If restoreSceneInstances is true, it will also restore the original transform and material state of all scene instances that are part of the vault gameplay (nodes, rings, core, exit) based on the data captured when they were first initialized. This is used when restarting a vault run to ensure that all gameplay elements are reset to their starting state, while allowing for flexibility in whether the scene instances themselves are reloaded from a snapshot or just reset in place.
    static bool ResetRun()
    {
        if (g_RuntimeLevelStartSnapshot.empty())
            return false;

        const std::string startSnapshot = g_RuntimeLevelStartSnapshot;
        if (!Scene::LoadFromString(startSnapshot))
            return false;

        RefreshRuntimeWorldFromCurrentScene("vault reset");

        ResetVaultRuntimeState(false);
        g_PlayerController = {};
        g_RuntimeWasPlaying = true;
        g_PlayerFrozen = false;
        g_RuntimeLevelStartSnapshot = startSnapshot;
        g_VaultPresentation.bannerType = VaultPresentationState::BannerType::Start;
        g_VaultPresentation.bannerTimer = kVaultStartBannerSeconds;
        return true;
    }

	// Advances to the next vault scene in the progression. This function checks for the next scene path based on the current scene, and if it exists, it loads that scene. It then resets the vault runtime state without restoring scene instances (since it's a new scene), resets the player controller, sets the runtime was playing flag to true, unfreezes the player, captures a new snapshot of the level start state, and sets up the vault presentation state to show the start banner. It returns true if the next vault scene was successfully loaded and initialized, or false if there is no next scene or if loading failed.
    static bool AdvanceToNextVault()
    {
        const char* nextScenePath = GetNextVaultScenePath();
        if (!nextScenePath)
            return false;
        if (!UI::LoadSceneAssetFromPath(nextScenePath))
            return false;

        RefreshRuntimeWorldFromCurrentScene("vault scene transition");

        ResetVaultRuntimeState(false);
        g_PlayerController = {};
        g_RuntimeWasPlaying = true;
        g_PlayerFrozen = false;
        g_RuntimeLevelStartSnapshot = Scene::SerializeToString();
        g_VaultPresentation.bannerType = VaultPresentationState::BannerType::Start;
        g_VaultPresentation.bannerTimer = kVaultStartBannerSeconds;
        return true;
    }

	// Finds a scene instance by its unique instance ID. This is used to validate and retrieve the player character instance based on the ID stored in the player controller, as well as to look up instances when activating nodes, stabilizing the core, or checking for proximity to the exit. The function iterates through all instances in the scene and returns a pointer to the matching instance if found, or nullptr if no instance with the given ID exists.
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

    static SceneInstance* FindSceneInstanceForRuntimeEntity(const RuntimeWorld& runtimeWorld, RuntimeEntityId entityId)
    {
        const RuntimeEntity* entity = runtimeWorld.GetEntity(entityId);
        if (!entity || entity->sourceSceneInstanceId == 0)
            return nullptr;

        return FindInstanceById(entity->sourceSceneInstanceId);
    }

    static RuntimeEntityId FindRuntimeEntityForSceneInstanceId(uint32_t sceneInstanceId)
    {
        if (!g_engineInstance || sceneInstanceId == 0)
            return kInvalidRuntimeEntityId;

        return g_engineInstance->GetRuntimeWorld().FindBySourceSceneInstanceId(sceneInstanceId);
    }

	// Attempts to acquire a scene instance to be used as the player character for vault gameplay. The function first checks if the currently assigned instance ID in the player controller is valid and corresponds to an existing instance with a disabled camera. If not, it checks the currently selected instance in the scene for the same criteria. If neither of those are suitable, it iterates through all instances in the scene to find one with a disabled camera, prioritizing those with names that suggest they are intended for player use (e.g., containing "vaultrunner" or "player"). If a suitable instance is found, its ID is assigned to the player controller and returned. If no valid instance is found, the player controller's instance ID is reset to 0 and nullptr is returned. This function ensures that the vault gameplay has a valid player instance to work with, while also allowing for flexibility in how scenes are set up.
    static SceneInstance* AcquirePlayerInstance()
    {
        if (SceneInstance* existing = FindInstanceById(g_PlayerController.instanceId))
        {
            if (!existing->camera.enabled)
            {
                if (g_PlayerController.playerEntity == kInvalidRuntimeEntityId)
                    g_PlayerController.playerEntity = FindRuntimeEntityForSceneInstanceId(existing->instanceId);
                return existing;
            }
        }

        if (g_engineInstance)
        {
            const RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
            const RuntimeEntityId runtimePlayerEntity = runtimeWorld.FindFirstPlayerControllerEntity();
            if (SceneInstance* runtimePlayer = FindSceneInstanceForRuntimeEntity(runtimeWorld, runtimePlayerEntity))
            {
                if (!runtimePlayer->camera.enabled)
                {
                    g_PlayerController.instanceId = runtimePlayer->instanceId;
                    g_PlayerController.playerEntity = runtimePlayerEntity;
                    g_PlayerController.initializedLook = false;
                    return runtimePlayer;
                }
            }
        }

        const uint32_t selectedId = Scene::GetSelectedInstanceId();
        if (SceneInstance* selected = FindInstanceById(selectedId))
        {
            if (!selected->camera.enabled)
            {
                g_PlayerController.instanceId = selected->instanceId;
                g_PlayerController.playerEntity = FindRuntimeEntityForSceneInstanceId(selected->instanceId);
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
            g_PlayerController.playerEntity = FindRuntimeEntityForSceneInstanceId(instance->instanceId);
            g_PlayerController.initializedLook = false;
            return instance;
        }

        for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
        {
            SceneInstance* instance = Scene::GetInstance(instanceIndex);
            if (!instance || instance->camera.enabled)
                continue;

            g_PlayerController.instanceId = instance->instanceId;
            g_PlayerController.playerEntity = FindRuntimeEntityForSceneInstanceId(instance->instanceId);
            g_PlayerController.initializedLook = false;
            return instance;
        }

        g_PlayerController.instanceId = 0;
        g_PlayerController.playerEntity = kInvalidRuntimeEntityId;
        g_PlayerController.initializedLook = false;
        return nullptr;
    }

    static SceneInstance* AcquireMainCameraInstance()
    {
        if (g_engineInstance)
        {
            const RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
            const RuntimeEntityId runtimeCameraEntity = runtimeWorld.FindMainCameraEntity();
            if (SceneInstance* runtimeCamera = FindSceneInstanceForRuntimeEntity(runtimeWorld, runtimeCameraEntity))
            {
                if (runtimeCamera->camera.enabled)
                {
                    g_PlayerController.cameraEntity = runtimeCameraEntity;
                    return runtimeCamera;
                }
            }
        }

        SceneInstance* mainCamera = Scene::GetMainCameraInstanceMutable();
        g_PlayerController.cameraEntity = mainCamera ? FindRuntimeEntityForSceneInstanceId(mainCamera->instanceId) : kInvalidRuntimeEntityId;
        return mainCamera;
    }

	// Calculates the squared distance between two 3D points. This is used for proximity checks and distance-based logic in the vault gameplay, such as determining if the player is close enough to a vault node to activate it or if they are within range of the vault exit trigger, without incurring the overhead of a square root calculation that would be required for actual distance.
    static float DistanceSquared(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

	// Calculates the squared distance between two points in the XZ plane, ignoring the Y component. This is used for proximity checks related to vault nodes and exits, where vertical distance is less relevant than horizontal distance for determining if the player is within interaction range or trigger zones.
    static float DistanceSquaredXZ(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return dx * dx + dz * dz;
    }

	// Normalizes an angle in radians to the range [-pi, pi]. This is used for calculating the relative angle between the player's facing direction and the direction to a target (e.g., for the vault scanner), ensuring that the angle is represented in a consistent way that allows for correct directional feedback in the UI.
    static float NormalizeAngleRadians(float angle)
    {
        constexpr float kTwoPi = DirectX::XM_PI * 2.0f;
        while (angle > DirectX::XM_PI)
            angle -= kTwoPi;
        while (angle < -DirectX::XM_PI)
            angle += kTwoPi;
        return angle;
    }

	// Converts a string to lowercase using the C locale. This is used for case-insensitive comparisons of instance names and prefab paths when classifying vault types and identifying vault exits, ensuring consistent behavior regardless of how the names are cased in the scene data.
    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

	// Finds the index of a material by its name. This function iterates through all materials in the MaterialManager and compares their names to the provided name. If a match is found, the index of that material is returned. If no match is found after checking all materials, -1 is returned to indicate that the material does not exist. This is used to look up material indices for vault nodes, rings, cores, and exits based on their configured material names.
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

	// Retrieves the index of a material with the specified name, creating it if it doesn't exist. The material is configured with the provided base color, metallic, and roughness properties. This function ensures that all necessary materials for vault presentation are available and consistently configured, allowing for dynamic visual feedback during vault gameplay.
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

	// Resets all runtime state related to the vault gameplay, including node states, materials, and mission progress. If restoreSceneInstances is true, it also restores the original materials, positions, and rotations of all vault-related scene instances to ensure a clean slate for the next run or scene.
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

        if (g_VaultMoodRuntime.applied)
        {
            Scene::SetSkyboxSettings(g_VaultMoodRuntime.originalSkybox);
            Graphics::GetInstance().GetDirectionalLight() = g_VaultMoodRuntime.originalLight;
        }

        EnsureVaultMaterials();

        g_InteractionTargetId = 0;
        g_VaultState = {};
        g_VaultMission = {};
        g_VaultPresentation = {};
        g_VaultScanner = {};
        g_VaultContextHint = {};
        g_VaultMoodRuntime = {};
        g_PlayerFrozen = false;
        g_RuntimeLevelStartSnapshot.clear();
        GA_Reset();
    }

	// Validates that all instance bindings in the vault gameplay state still reference existing scene instances. This is important to ensure that the vault logic doesn't operate on deleted or missing objects, which could lead to errors or crashes.
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

	// Ensures that all necessary materials for vault presentation exist and stores their indices in the vault state for quick access during gameplay.
    static void EnsureVaultMaterials()
    {
        g_VaultState.nodeInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Inactive", { 0.16f, 0.18f, 0.24f }, 0.0f, 0.98f);
        g_VaultState.nodeSlowMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Slow", { 0.34f, 0.24f, 0.52f }, 0.02f, 0.86f);
        g_VaultState.nodeFragileMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Fragile", { 0.42f, 0.18f, 0.16f }, 0.02f, 0.72f);
        g_VaultState.nodeActiveMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Active", { 0.76f, 0.44f, 0.14f }, 0.05f, 0.52f);
        g_VaultState.nodeDecayMaterial = GetOrCreateVaultMaterialIndex("VaultNode_Decaying", { 0.82f, 0.16f, 0.14f }, 0.05f, 0.34f);
        g_VaultState.coreInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Inactive", { 0.18f, 0.20f, 0.26f }, 0.0f, 0.95f);
        g_VaultState.coreActiveMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Active", { 1.00f, 0.82f, 0.32f }, 0.25f, 0.22f);
        g_VaultState.coreCompletedMaterial = GetOrCreateVaultMaterialIndex("VaultCore_Completed", { 0.42f, 1.00f, 0.74f }, 0.35f, 0.14f);
        g_VaultState.exitLockedMaterial = GetOrCreateVaultMaterialIndex("VaultExit_Locked", { 0.42f, 0.18f, 0.18f }, 0.04f, 0.84f);
        g_VaultState.exitUnlockedMaterial = GetOrCreateVaultMaterialIndex("VaultExit_Unlocked", { 0.22f, 0.78f, 0.38f }, 0.12f, 0.26f);
        g_VaultState.ringInactiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Inactive", { 0.32f, 0.24f, 0.18f }, 0.0f, 0.90f);
        g_VaultState.ringActiveMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Active", { 1.00f, 0.48f, 0.12f }, 0.15f, 0.32f);
        g_VaultState.ringCompletedMaterial = GetOrCreateVaultMaterialIndex("VaultRing_Completed", { 0.38f, 0.94f, 1.00f }, 0.20f, 0.18f);
    }

	// Discovers and initializes all vault-related scene instances, classifying them into nodes, rings, core, and exit based on their properties and names. It captures their original material indices and transforms for later use in resetting the vault state. This function is called at the start of a vault run to set up the gameplay state based on the current scene configuration.
    static void DiscoverVaultObjects(uint32_t playerInstanceId)
    {
        EnsureVaultMaterials();
        const VaultGameplaySettings gameplaySettings = Scene::GetVaultGameplaySettings();

        VaultGameplayState newState{};
        newState.nodeInactiveMaterial = g_VaultState.nodeInactiveMaterial;
        newState.nodeSlowMaterial = g_VaultState.nodeSlowMaterial;
        newState.nodeFragileMaterial = g_VaultState.nodeFragileMaterial;
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

        RuntimeWorld* runtimeWorld = g_engineInstance ? &g_engineInstance->GetRuntimeWorld() : nullptr;
        const VaultDiscoveryResult discoveryResult = BuildVaultGameplayBindings(
            { runtimeWorld, playerInstanceId, gameplaySettings },
            newState);

        for (VaultGameplayState::NodeBinding& nodeBinding : newState.nodes)
            VaultNodeSystem::SyncBindingToRuntime(GetRuntimeWorldForGameplay(), nodeBinding);

        if (discoveryResult.discoveredFromRuntimeWorld)
        {
            Logger::Log(LogLevel::Info,
                std::format("Vault bindings discovered from RuntimeWorld: nodes={} rings={} core={} exit={}",
                    newState.nodes.size(),
                    newState.rings.size(),
                    newState.core.instanceId != 0 ? 1 : 0,
                    newState.exit.instanceId != 0 ? 1 : 0),
                "[Game]");
        }
        
        g_VaultState = std::move(newState);
        g_VaultState.initialized = true;
        g_VaultState.coreUnlocked = false;
        g_VaultState.unlockedLogged = false;
        g_VaultState.exit.opened = false;
        g_VaultMission.state = VaultMissionState::ActivatingNodes;
        g_VaultMission.totalNodes = static_cast<int>(g_VaultState.nodes.size());
        g_VaultMission.activatedNodes = 0;
        g_VaultMission.decayedNodes = 0;
        g_VaultMission.maxDecayedNodes = gameplaySettings.maxDecayedNodes;
        g_VaultMission.nodeDecayDuration = gameplaySettings.nodeDecayDuration;
        g_VaultMission.nodeDecayWarningSeconds = gameplaySettings.nodeDecayWarningSeconds;
        g_VaultMission.coreUnlocked = false;
        SyncCoreBindingToRuntime(g_VaultState.core);
        SyncExitBindingToRuntime(g_VaultState.exit);
    }

	// Updates the state of the vault scanner based on the player's position and the current vault mission state. The scanner targets different objects (nodes, core, exit) depending on the mission progress, and calculates the distance, angle, and signal strength for the targeted object to provide directional feedback to the player. This function is called every frame to ensure that the scanner information is up-to-date as the player moves through the vault environment.
    static void UpdateVaultScannerState(const SceneInstance& playerInstance)
    {
        g_VaultScanner = {};

        DirectX::XMFLOAT3 targetPosition{};
        const char* targetLabel = nullptr;
        bool foundTarget = false;

        if (g_VaultMission.state == VaultMissionState::ActivatingNodes)
        {
            float bestDistSq = std::numeric_limits<float>::max();
            for (const VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
            {
                if (nodeBinding.state != VaultGameplayState::NodeState::Stabilizing)
                    continue;
                if (SceneInstance* node = FindInstanceById(nodeBinding.instanceId))
                {
                    const float distSq = DistanceSquaredXZ(playerInstance.position, node->position);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        targetPosition = node->position;
                        targetLabel = "Scanner: Stabilizing Node";
                        foundTarget = true;
                    }
                }
            }

            if (!foundTarget)
            {
                for (const VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
                {
                    if (nodeBinding.state != VaultGameplayState::NodeState::Decaying)
                        continue;
                    if (SceneInstance* node = FindInstanceById(nodeBinding.instanceId))
                    {
                        const float distSq = DistanceSquaredXZ(playerInstance.position, node->position);
                        if (distSq < bestDistSq)
                        {
                            bestDistSq = distSq;
                            targetPosition = node->position;
                            targetLabel = "Scanner: Unstable Node";
                            foundTarget = true;
                        }
                    }
                }
            }

            if (!foundTarget)
            {
                for (const VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
                {
                    if (nodeBinding.state != VaultGameplayState::NodeState::Inactive)
                        continue;
                    if (SceneInstance* node = FindInstanceById(nodeBinding.instanceId))
                    {
                        const float distSq = DistanceSquaredXZ(playerInstance.position, node->position);
                        if (distSq < bestDistSq)
                        {
                            bestDistSq = distSq;
                            targetPosition = node->position;
                            targetLabel = "Scanner: Vault Node";
                            foundTarget = true;
                        }
                    }
                }
            }
        }
        else if (g_VaultMission.state == VaultMissionState::CoreUnlocked)
        {
            if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
            {
                targetPosition = core->position;
                targetLabel = "Scanner: Core";
                foundTarget = true;
            }
        }
        else if (g_VaultMission.state == VaultMissionState::Completed)
        {
            targetPosition = g_VaultState.exit.originalPosition;
            targetLabel = "Scanner: Exit";
            foundTarget = g_VaultState.exit.instanceId != 0;
        }

        if (!foundTarget)
            return;

        const float dx = targetPosition.x - playerInstance.position.x;
        const float dz = targetPosition.z - playerInstance.position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float worldAngle = std::atan2(dx, dz);

        g_VaultScanner.hasTarget = true;
        g_VaultScanner.label = targetLabel;
        g_VaultScanner.distance = distance;
        g_VaultScanner.relativeAngleRadians = NormalizeAngleRadians(worldAngle - g_PlayerController.yaw);
        g_VaultScanner.strength = std::clamp(1.0f / (1.0f + distance * 0.18f), 0.05f, 1.0f);
    }

	// Updates the visual state of the vault nodes, rings, core, and exit based on the current gameplay state and mission progress. This includes handling node decay timers, changing materials to reflect active/decaying/inactive states, spinning rings based on progress, unlocking the core when all nodes are active, and opening the exit when the mission is completed. This function is called every frame to ensure that the visual feedback in the scene accurately represents the current state of the vault gameplay.
    static void UpdateVaultVisualState(float deltaTime)
    {
        int activeNodeCount = 0;
        for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            VaultNodeSystem::SyncBindingFromRuntime(GetRuntimeWorldForGameplay(), nodeBinding);

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
                    ++g_VaultMission.decayedNodes;
                    GA_Play(GameplayAudioEvent::NodeDecayed);
                }
                else if (nodeBinding.decayTimer <= g_VaultMission.nodeDecayWarningSeconds)
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
            case VaultGameplayState::NodeState::Stabilizing:
                node->materialIndex = g_VaultState.nodeActiveMaterial;
                break;
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
                node->materialIndex = (nodeBinding.type == VaultGameplayState::NodeType::SlowStabilize)
                    ? g_VaultState.nodeSlowMaterial
                    : (nodeBinding.type == VaultGameplayState::NodeType::Fragile ? g_VaultState.nodeFragileMaterial : g_VaultState.nodeInactiveMaterial);
                break;
            }

            VaultNodeSystem::SyncBindingToRuntime(GetRuntimeWorldForGameplay(), nodeBinding);
        }

		// Check for vault failure condition based on decayed nodes
        const int totalNodes = static_cast<int>(g_VaultState.nodes.size());
        g_VaultMission.totalNodes = totalNodes;
        g_VaultMission.activatedNodes = activeNodeCount;
        const float gameplayTension = (totalNodes > 0)
            ? (1.0f - static_cast<float>(activeNodeCount) / static_cast<float>(totalNodes))
            : 0.0f;
        const float moodTension = GetVaultMoodSettings(GetCurrentVaultMoodType()).audioTension;
        GA_SetTension(std::clamp(gameplayTension * 0.7f + moodTension * 0.3f, 0.0f, 1.0f));
        const int ringCount = static_cast<int>(g_VaultState.rings.size());
        const float ringProgress = (totalNodes > 0) ? static_cast<float>(activeNodeCount) / static_cast<float>(totalNodes) : 0.0f;
        const int completedRingCount = (ringCount > 0)
            ? (std::clamp)(static_cast<int>(std::floor(ringProgress * ringCount + 1e-4f)), 0, ringCount)
            : 0;

		// If the number of decayed nodes exceeds the maximum allowed, trigger vault failure
        for (int ringIndex = 0; ringIndex < ringCount; ++ringIndex)
        {
            VaultGameplayState::RingBinding& ringBinding = g_VaultState.rings[(size_t)ringIndex];
            SceneInstance* ring = FindInstanceById(ringBinding.instanceId);
            if (!ring)
                continue;

            const bool missionCompleted = (g_VaultMission.state == VaultMissionState::Completed);
            const bool isActiveRing = missionCompleted || ringIndex < completedRingCount;
            SyncRingBindingToRuntime(ringBinding, isActiveRing, missionCompleted);
            ring->materialIndex = missionCompleted ? g_VaultState.ringCompletedMaterial : (isActiveRing ? g_VaultState.ringActiveMaterial : g_VaultState.ringInactiveMaterial);
            const float spinSpeed = missionCompleted
                ? (1.6f + 0.2f * static_cast<float>(ringIndex))
                : (0.35f + ringProgress * (0.85f + 0.15f * static_cast<float>(ringIndex)));
            if (RuntimeTransformComponent* runtimeTransform = FindRingRuntimeTransform(ringBinding))
            {
                runtimeTransform->rotation.y += deltaTime * spinSpeed;
                ring->rotation = runtimeTransform->rotation;
            }
            else
            {
                ring->rotation.y += deltaTime * spinSpeed;
            }
        }

		// Unlock core if all nodes are active, and update core and exit visuals based on mission state
        const bool coreUnlocked = totalNodes > 0 && activeNodeCount >= totalNodes;
        g_VaultState.coreUnlocked = coreUnlocked;
        g_VaultMission.coreUnlocked = coreUnlocked;
        if (g_VaultMission.state != VaultMissionState::Completed &&
            g_VaultMission.state != VaultMissionState::Escaped &&
            g_VaultMission.state != VaultMissionState::Failed)
            g_VaultMission.state = coreUnlocked ? VaultMissionState::CoreUnlocked : VaultMissionState::ActivatingNodes;
        SyncCoreBindingToRuntime(g_VaultState.core);
        if (SceneInstance* core = FindInstanceById(g_VaultState.core.instanceId))
            core->materialIndex = (g_VaultMission.state == VaultMissionState::Completed)
                ? g_VaultState.coreCompletedMaterial
                : (coreUnlocked ? g_VaultState.coreActiveMaterial : g_VaultState.coreInactiveMaterial);

		// Open the exit if the mission is completed, otherwise keep it closed. This involves changing the material to indicate locked/unlocked state and moving the exit up to create an opening effect when unlocked.
        if (SceneInstance* exit = FindInstanceById(g_VaultState.exit.instanceId))
        {
            SyncExitBindingFromRuntime(g_VaultState.exit);
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
            SyncExitBindingToRuntime(g_VaultState.exit);
        }

		// Log core unlock event only once when it happens
        if (coreUnlocked && !g_VaultState.unlockedLogged)
        {
            g_VaultState.unlockedLogged = true;
            GA_Play(GameplayAudioEvent::CoreUnlocked);
            Logger::Log(LogLevel::Info, "Vault Core activated.", "[Game]");
        }
    }

    static void UpdateVaultNodeVarietyState(const DirectX::XMFLOAT3& playerPosition, float deltaTime)
    {
        if (g_VaultMission.state != VaultMissionState::ActivatingNodes)
            return;

        const float interactRangeSq = kVaultInteractionRange * kVaultInteractionRange;
        for (VaultGameplayState::NodeBinding& nodeBinding : g_VaultState.nodes)
        {
            VaultNodeSystem::SyncBindingFromRuntime(GetRuntimeWorldForGameplay(), nodeBinding);

            if (nodeBinding.type != VaultGameplayState::NodeType::SlowStabilize ||
                nodeBinding.state != VaultGameplayState::NodeState::Stabilizing)
                continue;

            SceneInstance* node = FindInstanceById(nodeBinding.instanceId);
            if (!node)
                continue;

            const float distSq = DistanceSquared(playerPosition, node->position);
            if (distSq > interactRangeSq)
            {
                nodeBinding.state = VaultGameplayState::NodeState::Inactive;
                nodeBinding.stabilizeProgress = 0.0f;
                VaultNodeSystem::SyncBindingToRuntime(GetRuntimeWorldForGameplay(), nodeBinding);
                continue;
            }

            nodeBinding.stabilizeProgress += deltaTime;
            if (nodeBinding.stabilizeProgress >= nodeBinding.stabilizeDuration)
            {
                nodeBinding.state = VaultGameplayState::NodeState::Active;
                nodeBinding.decayTimer = nodeBinding.decayDuration;
                nodeBinding.stabilizeProgress = 0.0f;
                nodeBinding.warningPlayed = false;
                GA_Play(GameplayAudioEvent::SlowNodeCompleted);
            }

            VaultNodeSystem::SyncBindingToRuntime(GetRuntimeWorldForGameplay(), nodeBinding);
        }
    }
}

// Initializes the game state when the game starts. This function resets all vault runtime state, clears the player controller, and sets the runtime was playing flag to false. It also clears any captured level start snapshot since we are just initializing and haven't started a run yet. This ensures that the game starts in a clean state with no residual data from previous runs or scenes.
bool Game::Initialize() {
    ResetVaultRuntimeState(false);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
    g_PlayerFrozen = false;
    g_RuntimeLevelStartSnapshot.clear();
    return true;
}

// Updates the game state every frame. This function handles the main gameplay loop for the vault mechanics, including checking for play mode state, handling player input for restarting or advancing vaults, acquiring the player instance, validating vault bindings, updating visual states of vault objects, and managing mission progress and failure conditions. It ensures that the vault gameplay is responsive and behaves correctly based on the player's actions and the current state of the vault mission.
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
    {
        g_RuntimeLevelStartSnapshot = Scene::SerializeToString();
        g_VaultPresentation.bannerType = VaultPresentationState::BannerType::Start;
        g_VaultPresentation.bannerTimer = kVaultStartBannerSeconds;
    }

    g_RuntimeWasPlaying = true;

    if (g_VaultPresentation.bannerTimer > 0.0f)
        g_VaultPresentation.bannerTimer = (std::max)(0.0f, g_VaultPresentation.bannerTimer - deltaTime);
    if (g_VaultPresentation.failPulseTimer > 0.0f)
        g_VaultPresentation.failPulseTimer = (std::max)(0.0f, g_VaultPresentation.failPulseTimer - deltaTime);
    if (g_VaultContextHint.timer > 0.0f)
    {
        g_VaultContextHint.timer = (std::max)(0.0f, g_VaultContextHint.timer - deltaTime);
        if (g_VaultContextHint.timer <= 0.0f)
            g_VaultContextHint.activeHint = VaultContextHintState::HintType::None;
    }

    ApplyVaultMoodSettings(deltaTime);

    if (engineState == Engine::State::Paused)
        return;

    Input& input = g_engineInstance->GetInput();
    const bool gamepadConnected = input.IsGamepadConnected(0);
    const bool restartRequested = ImGui::IsKeyPressed(ImGuiKey_R, false) || input.IsKeyJustPressed('R') || (gamepadConnected && input.IsGamepadButtonJustPressed(0, XINPUT_GAMEPAD_X));
    const bool nextVaultRequested = ImGui::IsKeyPressed(ImGuiKey_N, false) || input.IsKeyJustPressed('N') || (gamepadConnected && input.IsGamepadButtonJustPressed(0, XINPUT_GAMEPAD_A));
    const bool exitRequested = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || input.IsKeyJustPressed(VK_RETURN) || (gamepadConnected && input.IsGamepadButtonJustPressed(0, XINPUT_GAMEPAD_B));
    if (g_VaultMission.state == VaultMissionState::Escaped || g_VaultMission.state == VaultMissionState::Failed)
    {
        if (g_VaultMission.state == VaultMissionState::Escaped && nextVaultRequested)
        {
            (void)AdvanceToNextVault();
        }
        else if (restartRequested)
        {
            (void)ResetRun();
        }
        else if (exitRequested)
        {
            Engine::StopPlayMode();
        }
        else if (g_VaultMission.state == VaultMissionState::Escaped && g_VaultPresentation.nextVaultAutoAdvanceTimer > 0.0f)
        {
            g_VaultPresentation.nextVaultAutoAdvanceTimer = (std::max)(0.0f, g_VaultPresentation.nextVaultAutoAdvanceTimer - deltaTime);
            if (g_VaultPresentation.nextVaultAutoAdvanceTimer <= 0.0f)
                (void)AdvanceToNextVault();
        }
        return;
    }

    Graphics& graphics = Graphics::GetInstance();
    if (!graphics.IsGameViewportInputAllowed())
        return;

    SceneInstance* playerInstance = AcquirePlayerInstance();
    SceneInstance* mainCamera = AcquireMainCameraInstance();
    if (!playerInstance || !mainCamera || !mainCamera->camera.enabled)
        return;

    RuntimeWorld* runtimeWorld = g_engineInstance ? &g_engineInstance->GetRuntimeWorld() : nullptr;
    RuntimeTransformComponent* playerRuntimeTransform = (runtimeWorld && g_PlayerController.playerEntity != kInvalidRuntimeEntityId)
        ? runtimeWorld->GetTransform(g_PlayerController.playerEntity)
        : nullptr;
    RuntimeTransformComponent* cameraRuntimeTransform = (runtimeWorld && g_PlayerController.cameraEntity != kInvalidRuntimeEntityId)
        ? runtimeWorld->GetTransform(g_PlayerController.cameraEntity)
        : nullptr;

    if (!AreVaultBindingsValid())
        DiscoverVaultObjects(playerInstance->instanceId);

    if (!g_PlayerController.initializedLook)
    {
        g_PlayerController.yaw = playerInstance->rotation.y;
        g_PlayerController.pitch = mainCamera->rotation.x;
        g_PlayerController.initializedLook = true;
    }

    UpdateVaultVisualState(deltaTime);

    if (g_VaultMission.state != VaultMissionState::Failed &&
        g_VaultMission.state != VaultMissionState::Escaped &&
        g_VaultMission.decayedNodes >= g_VaultMission.maxDecayedNodes)
    {
        TriggerVaultFailure();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const EditorState& editor = g_engineInstance->GetEditorState();
    const bool gameplayGamepadEnabled = editor.enableGamepadCamera && gamepadConnected;
    const bool lookActive = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (lookActive)
    {
        const float lookDeltaX = io.MouseDelta.x * g_PlayerController.lookSensitivity * (editor.invertLookX ? -1.0f : 1.0f);
        const float lookDeltaY = io.MouseDelta.y * g_PlayerController.lookSensitivity * (editor.invertLookY ? -1.0f : 1.0f);
        g_PlayerController.yaw += lookDeltaX;
        g_PlayerController.pitch -= lookDeltaY;
    }

    if (gameplayGamepadEnabled)
    {
        const float gamepadLookX = input.GetGamepadRightStickX(0) * editor.gamepadLookSensitivity;
        const float gamepadLookY = input.GetGamepadRightStickY(0) * editor.gamepadLookSensitivity;
        const float lookScale = 2.2f * deltaTime;
        g_PlayerController.yaw += gamepadLookX * lookScale * (editor.invertLookX ? -1.0f : 1.0f);
        g_PlayerController.pitch += gamepadLookY * lookScale * (editor.invertLookY ? -1.0f : 1.0f);
    }
    g_PlayerController.pitch = (std::clamp)(g_PlayerController.pitch, DirectX::XMConvertToRadians(-85.0f), DirectX::XMConvertToRadians(85.0f));

	// Movement input is processed regardless of whether the look is active, allowing for simultaneous movement and looking around. This enables more fluid control, as the player can adjust their view while moving without needing to hold down the look button continuously.
    const bool moveForward = ImGui::IsKeyDown(ImGuiKey_W) || input.IsKeyPressed('W');
    const bool moveBackward = ImGui::IsKeyDown(ImGuiKey_S) || input.IsKeyPressed('S');
    const bool moveLeft = ImGui::IsKeyDown(ImGuiKey_A) || input.IsKeyPressed('A');
    const bool moveRight = ImGui::IsKeyDown(ImGuiKey_D) || input.IsKeyPressed('D');
    const bool speedBoost = io.KeyShift || input.IsKeyPressed(VK_SHIFT) || (gameplayGamepadEnabled && (input.GetGamepadRightTrigger(0) > 0.25f || input.IsGamepadButtonPressed(0, XINPUT_GAMEPAD_LEFT_SHOULDER)));

    float moveSpeed = g_PlayerController.moveSpeed;
    if (speedBoost)
        moveSpeed *= kVaultPlayerSprintMultiplier;

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
    if (gameplayGamepadEnabled)
    {
        const float moveStickX = input.GetGamepadLeftStickX(0) * editor.gamepadMoveSensitivity;
        const float moveStickY = input.GetGamepadLeftStickY(0) * editor.gamepadMoveSensitivity;
        moveDelta.x += forward.x * moveStickY + right.x * moveStickX;
        moveDelta.y += forward.y * moveStickY + right.y * moveStickX;
        moveDelta.z += forward.z * moveStickY + right.z * moveStickX;
    }
    if (!g_PlayerFrozen && (moveDelta.x != 0.0f || moveDelta.y != 0.0f || moveDelta.z != 0.0f))
    {
        DirectX::XMVECTOR move = DirectX::XMLoadFloat3(&moveDelta);
        move = DirectX::XMVector3Normalize(move);
        move = DirectX::XMVectorScale(move, moveSpeed * deltaTime);
        DirectX::XMStoreFloat3(&moveDelta, move);

        DirectX::XMFLOAT3& playerPosition = playerRuntimeTransform ? playerRuntimeTransform->position : playerInstance->position;
        playerPosition.x += moveDelta.x;
        playerPosition.y += moveDelta.y;
        playerPosition.z += moveDelta.z;
    }

    const DirectX::XMFLOAT3 playerGameplayPosition = playerRuntimeTransform ? playerRuntimeTransform->position : playerInstance->position;
    UpdateVaultNodeVarietyState(playerGameplayPosition, deltaTime);
    UpdateVaultVisualState(0.0f);

    if (playerRuntimeTransform)
    {
        playerRuntimeTransform->rotation.y = g_PlayerController.yaw;
    }
    else
    {
        playerInstance->rotation.y = g_PlayerController.yaw;
    }

    const DirectX::XMFLOAT3 updatedPlayerPosition = playerRuntimeTransform ? playerRuntimeTransform->position : playerInstance->position;
    if (cameraRuntimeTransform)
    {
        cameraRuntimeTransform->position = {
            updatedPlayerPosition.x,
            updatedPlayerPosition.y + g_PlayerController.cameraHeight,
            updatedPlayerPosition.z
        };
        cameraRuntimeTransform->rotation.x = g_PlayerController.pitch;
        cameraRuntimeTransform->rotation.y = g_PlayerController.yaw;
        cameraRuntimeTransform->rotation.z = 0.0f;
    }
    else
    {
        mainCamera->position = {
            updatedPlayerPosition.x,
            updatedPlayerPosition.y + g_PlayerController.cameraHeight,
            updatedPlayerPosition.z
        };
        mainCamera->rotation.x = g_PlayerController.pitch;
        mainCamera->rotation.y = g_PlayerController.yaw;
        mainCamera->rotation.z = 0.0f;
    }

    if (runtimeWorld)
    {
        runtimeWorld->SyncTransformToScene(g_PlayerController.playerEntity);
        runtimeWorld->SyncTransformToScene(g_PlayerController.cameraEntity);
    }

    UpdateVaultScannerState(*playerInstance);

    const float interactRangeSq = kVaultInteractionRange * kVaultInteractionRange;
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
    }
    else if (g_VaultMission.state == VaultMissionState::Completed)
    {
        if (SceneInstance* exit = FindInstanceById(g_VaultState.exit.instanceId))
        {
            const DirectX::XMFLOAT3 exitTriggerPosition{
                g_VaultState.exit.originalPosition.x,
                playerInstance->position.y,
                g_VaultState.exit.originalPosition.z
            };
            const float distSq = DistanceSquaredXZ(playerInstance->position, exitTriggerPosition);
            if (distSq <= kVaultExitTriggerRadius * kVaultExitTriggerRadius)
                TriggerVaultEscape();
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
            case VaultGameplayState::NodeState::Stabilizing: priority = 3; break;
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

    if (const VaultGameplayState::NodeBinding* bestNodeBinding = VaultNodeSystem::FindNodeBindingById(g_VaultState, bestTargetId))
    {
        if (bestNodeBinding->type == VaultGameplayState::NodeType::SlowStabilize)
            ShowVaultContextHint(VaultContextHintState::HintType::SlowNode);
        else if (bestNodeBinding->type == VaultGameplayState::NodeType::Fragile)
            ShowVaultContextHint(VaultContextHintState::HintType::FragileNode);
    }

    const bool interactPressed = ImGui::IsKeyPressed(ImGuiKey_E, false) || input.IsKeyJustPressed('E') || (gameplayGamepadEnabled && input.IsGamepadButtonJustPressed(0, XINPUT_GAMEPAD_A));
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
                    VaultNodeSystem::SyncBindingFromRuntime(GetRuntimeWorldForGameplay(), nodeBinding);

                    if (nodeBinding.type == VaultGameplayState::NodeType::SlowStabilize)
                    {
                        nodeBinding.state = VaultGameplayState::NodeState::Stabilizing;
                        nodeBinding.stabilizeProgress = 0.0f;
                        nodeBinding.warningPlayed = false;
                        GA_Play(GameplayAudioEvent::SlowNodeStarted);
                    }
                    else
                    {
                        nodeBinding.state = VaultGameplayState::NodeState::Active;
                        nodeBinding.decayTimer = nodeBinding.decayDuration;
                        nodeBinding.warningPlayed = false;
                        GA_Play(GameplayAudioEvent::NodeActivated);
                    }
                    VaultNodeSystem::SyncBindingToRuntime(GetRuntimeWorldForGameplay(), nodeBinding);
                    g_InteractionTargetId = 0;
                    break;
                }
            }
        }
    }
}

// Shuts down the game state when the game ends. This function resets all vault runtime state, clears the player controller, and sets the runtime was playing flag to false. This ensures that when the game is stopped, all gameplay-related data is cleared and the game is left in a clean state for the next time it is started.
void Game::Shutdown() {
    ResetVaultRuntimeState(true);
    g_PlayerController = {};
    g_RuntimeWasPlaying = false;
}

// Returns the current state of the vault mission, which indicates the player's progress through the vault objectives. This state is used to determine what actions are available to the player, what objectives to display, and how to update the visual and gameplay elements of the vault accordingly.
VaultMissionState Game::GetVaultMissionState() const
{
    return g_VaultMission.state;
}

// Returns the appropriate mission objective text based on the current vault mission state. This provides contextual information to the player about their current objective in the vault, such as activating nodes, stabilizing the core, or escaping through the exit. The text is determined by a helper function that maps each mission state to a specific string.
const char* Game::GetVaultMissionObjectiveText() const
{
    return GetMissionObjectiveText(g_VaultMission.state);
}

// Returns whether there is a valid interaction target for the player based on the current vault mission state and the player's proximity to interactable objects. This is used to determine whether to show an interaction prompt and what that prompt should be, providing contextual feedback to the player about potential interactions in their environment.
bool Game::HasInteractionTarget() const
{
    return g_InteractionTargetId != 0;
}

// Returns whether the current interaction target is the core based on its instance ID. This is used to determine the appropriate interaction prompt and action label to display to the player, as interacting with the core has different implications than interacting with nodes (activating vs stabilizing).
uint32_t Game::GetInteractionTargetId() const
{
    return g_InteractionTargetId;
}

// Returns the interaction prompt text to display based on the current interaction target. If there is no interaction target, it returns nullptr indicating that no prompt should be shown. If the target is the core, it returns a specific prompt for stabilizing the core. If the target is a node, it checks the node's state and returns either an activation prompt for inactive nodes or a refresh prompt for active/decaying nodes. This function provides contextual feedback to the player about what action they can take with the current interaction target.
const char* Game::GetInteractionPrompt() const
{
    if (!HasInteractionTarget())
        return nullptr;
    if (IsInteractionTargetCore(g_InteractionTargetId))
        return "Press E to stabilize the core";

    const VaultGameplayState::NodeBinding* nodeBinding = VaultNodeSystem::FindNodeBindingById(g_VaultState, g_InteractionTargetId);
    if (!nodeBinding)
        return "Press E to activate node";

    if (nodeBinding->type == VaultGameplayState::NodeType::SlowStabilize)
    {
        if (nodeBinding->state == VaultGameplayState::NodeState::Stabilizing)
            return "Stay near to stabilize the node";
        if (nodeBinding->state == VaultGameplayState::NodeState::Inactive)
            return "Press E to begin stabilizing node";
    }
    if (nodeBinding->type == VaultGameplayState::NodeType::Fragile && nodeBinding->state == VaultGameplayState::NodeState::Inactive)
        return "Press E to activate fragile node";

    return nodeBinding->state == VaultGameplayState::NodeState::Inactive
        ? "Press E to activate node"
        : "Press E to refresh node stability";
}

// Returns the label for the current interaction action based on the interaction target. If there is no interaction target, it returns a generic "Interact" label. If the target is the core, it returns "Stabilize" to indicate the specific action of stabilizing the core. If the target is a node, it checks whether the node is currently inactive or active/decaying and returns "Activate" or "Refresh" accordingly to indicate whether the player will be activating a new node or refreshing an already active node.
const char* Game::GetInteractionActionLabel() const
{
    if (!HasInteractionTarget())
        return "Interact";
    if (IsInteractionTargetCore(g_InteractionTargetId))
        return "Stabilize";

    const VaultGameplayState::NodeBinding* nodeBinding = VaultNodeSystem::FindNodeBindingById(g_VaultState, g_InteractionTargetId);
    if (!nodeBinding)
        return "Activate";
    if (nodeBinding->type == VaultGameplayState::NodeType::SlowStabilize)
        return nodeBinding->state == VaultGameplayState::NodeState::Stabilizing ? "Stabilizing" : "Begin Stabilize";
    if (nodeBinding->type == VaultGameplayState::NodeType::Fragile && nodeBinding->state == VaultGameplayState::NodeState::Inactive)
        return "Activate Fragile";
    return nodeBinding->state == VaultGameplayState::NodeState::Inactive ? "Activate" : "Refresh";
}

// Returns whether there is a vault warning that should be displayed to the player. A warning is present if there is at least one node that is currently in the decaying state, indicating that the player needs to take action to stabilize it before it fully decays. However, if the mission state is already "Failed" or "Escaped", no warnings are shown since the run has already ended in either failure or success.
bool Game::HasVaultWarning() const
{
    if (g_VaultMission.state == VaultMissionState::Failed || g_VaultMission.state == VaultMissionState::Escaped)
        return false;
    return VaultNodeSystem::FindMostUrgentDecayingNode(g_VaultState) != nullptr;
}

// Returns the warning text to display for the most urgent decaying node in the vault. If there is a node that is currently decaying, it checks if that node is the current interaction target. If it is, it returns a more specific warning prompting the player to refresh that node immediately. If it is not the interaction target, it returns a general warning about nodes destabilizing. If there are no decaying nodes, it returns nullptr, indicating that no warning should be displayed.
const char* Game::GetVaultWarningText() const
{
    const VaultGameplayState::NodeBinding* warningNode = VaultNodeSystem::FindMostUrgentDecayingNode(g_VaultState);
    if (!warningNode)
        return nullptr;
    if (warningNode->instanceId == g_InteractionTargetId)
        return "Node destabilizing! Refresh it now.";
    return "Node destabilizing!";
}

// Returns the number of currently active nodes in the vault, which is used to track the player's progress in activating nodes and to determine when the core should be unlocked based on all nodes being active. This information can also be used for UI elements that display progress or for calculating tension and ring activation based on the ratio of active nodes to total nodes.
int Game::GetVaultActiveNodeCount() const
{
    return g_VaultMission.activatedNodes;
}

// Returns the total number of nodes in the current vault, which is determined by the number of node bindings discovered in the scene. This information is used to track the player's progress in activating nodes and to determine when the core should be unlocked based on all nodes being active. It can also be used for UI elements that display progress or for calculating tension and ring activation based on the ratio of active nodes to total nodes.
int Game::GetVaultTotalNodeCount() const
{
    return static_cast<int>(g_VaultState.nodes.size());
}

// Returns whether the player can restart the current vault run. Restarting is only allowed if there is a valid level start snapshot (indicating that a run has been started) and if the current mission state is either "Escaped" or "Failed". This function is used to determine whether to enable the option for restarting the vault run after the player has either completed it successfully or failed, ensuring that restarts are only possible in appropriate contexts.
bool Game::CanRestartVaultRun() const
{
    return !g_RuntimeLevelStartSnapshot.empty() &&
        (g_VaultMission.state == VaultMissionState::Escaped || g_VaultMission.state == VaultMissionState::Failed);
}

// Restarts the current vault run by reloading the initial scene state captured at the start of the run. This function is only allowed if there is a valid level start snapshot and if the current mission state is either "Escaped" or "Failed". It resets the vault runtime state and player controller to ensure a clean restart of the vault mission. It returns true if the restart was successful, or false if it was not possible to restart (e.g., no snapshot available or current state does not allow restarting).
bool Game::IsVaultCoreUnlocked() const
{
    return g_VaultMission.coreUnlocked;
}

// Returns whether the vault mission has been completed, which occurs when the player successfully stabilizes the core and opens the exit. This function is used to determine if the player has met the primary objectives of the current vault run and can be used to trigger completion-specific UI elements, audio cues, or to allow the player to advance to the next vault or restart.
bool Game::IsVaultMissionCompleted() const
{
    return g_VaultMission.state == VaultMissionState::Completed || g_VaultMission.state == VaultMissionState::Escaped;
}

// Returns whether the vault mission has been escaped, which occurs when the player reaches the exit after completing the core activation. This function is used to determine if the player has successfully completed the current vault run and can be used to trigger escape-specific UI elements, audio cues, or to allow the player to advance to the next vault or restart.
bool Game::IsVaultMissionEscaped() const
{
    return g_VaultMission.state == VaultMissionState::Escaped;
}

// Returns whether the vault mission has failed, which occurs when the number of decayed nodes exceeds the maximum allowed. This function is used to determine if the player has failed the current vault run and can be used to trigger failure-specific UI elements, audio cues, or to allow the player to restart the run.
bool Game::IsVaultMissionFailed() const
{
    return g_VaultMission.state == VaultMissionState::Failed;
}

// Returns whether the game can advance to the next vault based on the current mission state and the availability of the next vault scene. Advancing to the next vault is only possible if the current mission state is "Escaped" and there is a valid next vault scene to load. This function is used to determine whether to allow the player to transition to the next vault or to display an option for advancing after escaping the current vault.
bool Game::CanAdvanceToNextVault() const
{
    return g_VaultMission.state == VaultMissionState::Escaped && GetNextVaultScenePath() != nullptr;
}

// Advances the game to the next vault if possible. This function checks if advancing is allowed based on the current mission state and the availability of the next vault scene. If advancing is possible, it loads the next vault scene and resets the runtime state for the new vault. It returns true if the advance was successful, or false if it was not possible to advance (e.g., no next vault available or current state does not allow advancing).
bool Game::AdvanceToNextVaultNow()
{
    return AdvanceToNextVault();
}

// Returns the remaining time in seconds before the game automatically advances to the next vault after escaping the current one. This is used to provide feedback to the player about how much time they have left to manually advance or restart before the game transitions to the next vault on its own. If there is no next vault available or if the current mission state does not allow advancing, it returns 0.0f, indicating that no auto-advance is in progress.
float Game::GetNextVaultAutoAdvanceSecondsRemaining() const
{
    if (!CanAdvanceToNextVault())
        return 0.0f;
    return g_VaultPresentation.nextVaultAutoAdvanceTimer;
}

// Returns the label for the vault progression, which is typically the name of the current vault or a description of the player's progress through the vault missions. This information can be used to display a progression banner or UI element that informs the player about their current location in the vault sequence and what they can expect next. If there is no specific label for the current scene, it returns nullptr.
const char* Game::GetVaultProgressionLabel() const
{
    return GetVaultProgressionLabelForCurrentScene();
}

const char* Game::GetNextVaultButtonLabel() const
{
    if (!CanAdvanceToNextVault())
        return nullptr;
    return GetNextVaultButtonLabelForCurrentScene();
}

void Game::GetVaultMoodAccentColor(float& r, float& g, float& b) const
{
    const VaultMoodSettings mood = GetVaultMoodSettings(GetCurrentVaultMoodType());
    r = mood.colorTint.x;
    g = mood.colorTint.y;
    b = mood.colorTint.z;
}

void Game::GetVaultMoodSecondaryColor(float& r, float& g, float& b) const
{
    const VaultMoodSettings mood = GetVaultMoodSettings(GetCurrentVaultMoodType());
    r = mood.lightColor.x;
    g = mood.lightColor.y;
    b = mood.lightColor.z;
}

// Returns whether the current scene is a tutorial vault, which is determined by checking if the current scene matches any of the predefined tutorial scenes. This information can be used to conditionally display tutorial-specific UI elements, hints, or guidance to help players learn the mechanics of the vault before attempting the main vault missions.
bool Game::IsTutorialVault() const
{
    return IsTutorialSceneCurrentScene();
}

// Returns the header text for the tutorial vault, which is a prominent title displayed in the tutorial vault to introduce the player to the vault mechanics and objectives. This text is shown at the start of the tutorial vault and serves to set the context for the player's experience in learning how to navigate and complete the vault mission. If there is no specific header for the current tutorial scene, it returns nullptr.
const char* Game::GetVaultTutorialHeader() const
{
    return GetTutorialHeaderText();
}

// Returns the primary hint text for the tutorial vault, which provides essential guidance or instructions to the player about the vault mechanics or objectives. This text is shown in the tutorial vault to help players understand the core concepts and actions they need to take to successfully navigate and complete the vault mission. If there is no primary hint for the current tutorial scene, it returns nullptr.
const char* Game::GetVaultTutorialHintPrimary() const
{
    return GetTutorialHintPrimaryText();
}

// Returns the secondary hint text for the tutorial vault, which provides additional guidance or information to the player about the vault mechanics or objectives. This text is shown in the tutorial vault to help players understand how to interact with the vault environment and successfully complete the mission. If there is no secondary hint for the current tutorial scene, it returns nullptr.
const char* Game::GetVaultTutorialHintSecondary() const
{
    return GetTutorialHintSecondaryText();
}

bool Game::HasVaultContextHint() const
{
    return g_VaultContextHint.activeHint != VaultContextHintState::HintType::None && g_VaultContextHint.timer > 0.0f;
}

const char* Game::GetVaultContextHintTitle() const
{
    switch (g_VaultContextHint.activeHint)
    {
    case VaultContextHintState::HintType::SlowNode: return "New Node: Slow Stabilize";
    case VaultContextHintState::HintType::FragileNode: return "New Node: Fragile";
    default: return nullptr;
    }
}

const char* Game::GetVaultContextHintText() const
{
    switch (g_VaultContextHint.activeHint)
    {
    case VaultContextHintState::HintType::SlowNode:
        return "Start it with E or A, then stay nearby until the node fully stabilizes.";
    case VaultContextHintState::HintType::FragileNode:
        return "Fragile nodes decay faster than normal ones, so refresh them first when routing gets tight.";
    default:
        return nullptr;
    }
}

float Game::GetVaultContextHintAlpha() const
{
    if (!HasVaultContextHint())
        return 0.0f;
    return std::clamp(g_VaultContextHint.timer / kVaultContextHintSeconds, 0.0f, 1.0f);
}

// Returns whether the vault scanner currently has a valid target to point to, which indicates that there is a relevant objective in the vault (such as an unstable node, the core, or the exit) that the player can navigate towards. This information can be used to determine whether to display the scanner UI and provide directional feedback to the player about their next objective in the vault.
bool Game::HasVaultScannerTarget() const
{
    return g_VaultScanner.hasTarget;
}

// Returns the label for the vault scanner's current target, which is a string that describes what the scanner is currently pointing at. This could be "Scanner: Vault Node" when targeting an inactive node, "Scanner: Unstable Node" when targeting a decaying node, "Scanner: Core" when targeting the core after it's unlocked, or "Scanner: Exit" when targeting the exit after the mission is completed. If there is no valid target for the scanner, it returns nullptr.
const char* Game::GetVaultScannerTargetLabel() const
{
    return g_VaultScanner.label;
}

// Returns the distance from the player to the vault scanner's current target, which is the next relevant objective in the vault (such as an unstable node, the core, or the exit) based on the player's progress in the mission. The distance is calculated in world units and can be used to provide feedback to the player about how far they are from their next objective in the vault.
float Game::GetVaultScannerDistance() const
{
    return g_VaultScanner.distance;
}

// Returns the signal strength of the vault scanner, which is a value between 0.05 and 1.0 that indicates how strong the scanner signal is for the current target. The strength is calculated based on the distance to the target, with closer targets resulting in a stronger signal. This information can be used to provide feedback to the player about how close they are to the next objective in the vault.
float Game::GetVaultScannerStrength() const
{
    return g_VaultScanner.strength;
}

// Returns the relative angle in radians from the player's forward direction to the vault scanner's target. This angle is normalized to the range [-pi, pi], where 0 means the target is directly ahead, positive values indicate the target is to the right, and negative values indicate the target is to the left. This information can be used to provide directional feedback to the player about where to find the next objective in the vault.
float Game::GetVaultScannerDirectionAngleRadians() const
{
    return g_VaultScanner.relativeAngleRadians;
}

// Returns the text for the next vault action prompt, which is shown when the player has successfully escaped the current vault mission and there is a subsequent vault available to advance to. If there is no next vault available or if the current mission state does not allow advancing, it returns nullptr, indicating that no prompt should be shown.
const char* Game::GetNextVaultActionText() const
{
    if (!CanAdvanceToNextVault())
        return nullptr;
    return GetNextVaultActionLabel();
}

// Returns the title text for the vault end overlay based on the final state of the vault mission. If the mission was successfully escaped, it returns a success message; if it failed, it returns a failure message. If the mission is still in progress or in an undefined state, it returns nullptr, indicating that no overlay should be shown.
const char* Game::GetVaultEndOverlayTitle() const
{
    switch (g_VaultMission.state)
    {
    case VaultMissionState::Escaped: return IsFinalVaultSceneCurrentScene() ? "GAME COMPLETE" : "ESCAPE SUCCESSFUL";
    case VaultMissionState::Failed: return "SYSTEM FAILURE";
    default: return nullptr;
    }
}

// Returns the subtitle text for the vault end overlay based on the final state of the vault mission. If the mission was successfully escaped, it provides a message about the core being stabilized and the vault being cleared. If the mission failed, it prompts the player to retry by pressing R. If the mission is still in progress or in an undefined state, it returns nullptr, indicating that no overlay should be shown.
const char* Game::GetVaultEndOverlaySubtitle() const
{
    switch (g_VaultMission.state)
    {
    case VaultMissionState::Escaped:
        return IsFinalVaultSceneCurrentScene()
            ? "All three vaults are clear. Would you like to restart from the beginning?"
            : "The core is stabilized and the vault has been cleared.";
    case VaultMissionState::Failed: return "Too many vault nodes decayed. Press R to retry.";
    default: return nullptr;
    }
}

// Returns whether the vault presentation banner should be shown, which is a visual element that appears at the start of the vault mission and when the player successfully escapes. This function checks if the banner timer is greater than zero and if the banner type is not None, indicating that there is an active banner that should be displayed to provide feedback about the current state of the vault.
bool Game::HasVaultPresentationBanner() const
{
    return g_VaultPresentation.bannerTimer > 0.0f && g_VaultPresentation.bannerType != VaultPresentationState::BannerType::None;
}

// Returns the text for the vault presentation banner based on the current banner type. The banner is a visual element that appears at the start of the vault mission and when the player successfully escapes, providing feedback about the current state of the vault. If there is no active banner, it returns nullptr, indicating that no banner should be shown.
const char* Game::GetVaultPresentationBannerText() const
{
    switch (g_VaultPresentation.bannerType)
    {
    case VaultPresentationState::BannerType::Start: return "Vault Initialized";
    case VaultPresentationState::BannerType::Escape: return "Escape Successful";
    default: return nullptr;
    }
}

// Returns the alpha value for the vault presentation banner, which is a visual element that appears at the start of the vault mission and when the player successfully escapes. The alpha value is calculated based on the remaining time of the banner timer, normalized by the total duration of the banner display. If there is no active banner, it returns 0.0f, indicating that the banner should be fully transparent.
float Game::GetVaultPresentationBannerAlpha() const
{
    if (!HasVaultPresentationBanner())
        return 0.0f;

    const float duration = (g_VaultPresentation.bannerType == VaultPresentationState::BannerType::Escape)
        ? kVaultEscapeBannerSeconds
        : kVaultStartBannerSeconds;
    const float normalized = std::clamp(g_VaultPresentation.bannerTimer / duration, 0.0f, 1.0f);
    return normalized;
}

// Returns whether the vault fail pulse effect is currently active, which indicates that the player is in a critical state where the vault mission is close to failing due to too many decayed nodes. This function checks if the fail pulse timer is greater than zero, which means that the effect should be active and providing visual feedback to the player about the impending failure condition.
bool Game::HasVaultFailPulse() const
{
    return g_VaultPresentation.failPulseTimer > 0.0f;
}

// Returns the alpha value for the vault fail pulse effect, which is a visual feedback mechanism that indicates an impending failure condition in the vault mission. The alpha value is calculated based on the remaining time of the fail pulse timer, normalized by the total duration of the fail pulse effect. If there is no active fail pulse, it returns 0.0f, indicating that the effect should be fully transparent.
float Game::GetVaultFailPulseAlpha() const
{
    if (!HasVaultFailPulse())
        return 0.0f;
    return std::clamp(g_VaultPresentation.failPulseTimer / kVaultFailPulseSeconds, 0.0f, 1.0f);
}
