#pragma once

#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Game.h"
#include "RuntimeComponents.h"

struct VaultGameplayState
{
    enum class NodeState : uint8_t
    {
        Inactive = 0,
        Stabilizing,
        Active,
        Decaying,
    };

    enum class NodeType : uint8_t
    {
        Normal = 0,
        SlowStabilize,
        Fragile,
        Corrupted,
    };

    struct NodeBinding
    {
        uint32_t instanceId = 0;
        NodeType type = NodeType::Normal;
        NodeState state = NodeState::Inactive;
        float decayTimer = 0.0f;
        float decayDuration = 10.0f;
        float stabilizeDuration = 0.0f;
        float stabilizeProgress = 0.0f;
        bool warningPlayed = false;
        int originalMaterialIndex = 0;
        RuntimeEntityId runtimeEntity = kInvalidRuntimeEntityId;
    };

    struct RingBinding
    {
        uint32_t instanceId = 0;
        int originalMaterialIndex = 0;
        DirectX::XMFLOAT3 originalRotation{ 0.0f, 0.0f, 0.0f };
        RuntimeEntityId runtimeEntity = kInvalidRuntimeEntityId;
    };

    struct CoreBinding
    {
        uint32_t instanceId = 0;
        int originalMaterialIndex = 0;
        RuntimeEntityId runtimeEntity = kInvalidRuntimeEntityId;
    };

    struct ExitBinding
    {
        uint32_t instanceId = 0;
        int originalMaterialIndex = 0;
        DirectX::XMFLOAT3 originalPosition{ 0.0f, 0.0f, 0.0f };
        float openOffsetY = 3.5f;
        bool opened = false;
        RuntimeEntityId runtimeEntity = kInvalidRuntimeEntityId;
    };

    std::vector<NodeBinding> nodes;
    std::vector<RingBinding> rings;
    CoreBinding core{};
    ExitBinding exit{};
    int nodeInactiveMaterial = 0;
    int nodeSlowMaterial = 0;
    int nodeFragileMaterial = 0;
    int nodeCorruptedMaterial = 0;
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
    int decayedNodes = 0;
    int maxDecayedNodes = 3;
    float nodeDecayDuration = 9.0f;
    float nodeDecayWarningSeconds = 3.0f;
    bool coreUnlocked = false;
};

struct VaultPresentationState
{
    float bannerTimer = 0.0f;
    float failPulseTimer = 0.0f;
    float nextVaultAutoAdvanceTimer = 0.0f;

    enum class BannerType : uint8_t
    {
        None = 0,
        Start,
        Escape,
    };

    BannerType bannerType = BannerType::None;
};

struct VaultScannerState
{
    bool hasTarget = false;
    const char* label = nullptr;
    float distance = 0.0f;
    float strength = 0.0f;
    float relativeAngleRadians = 0.0f;
};

struct VaultContextHintState
{
    enum class HintType : uint8_t
    {
        None = 0,
        SlowNode,
        FragileNode,
    };

    HintType activeHint = HintType::None;
    float timer = 0.0f;
    bool shownSlowNode = false;
    bool shownFragileNode = false;
};

VaultGameplayState::NodeType ToVaultNodeType(VaultNodeComponent::Type type);
VaultNodeComponent::Type ToRuntimeNodeType(VaultGameplayState::NodeType type);
VaultGameplayState::NodeState ToVaultNodeState(VaultNodeComponent::State state);
VaultNodeComponent::State ToRuntimeNodeState(VaultGameplayState::NodeState state);
