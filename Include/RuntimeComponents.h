#pragma once

#include <cstdint>
#include <string>

#include <DirectXMath.h>

#include "Scene.h"

using RuntimeEntityId = uint32_t;

static constexpr RuntimeEntityId kInvalidRuntimeEntityId = 0;

struct RuntimeTransformComponent
{
    RuntimeEntityId parent = kInvalidRuntimeEntityId;
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
};

struct RuntimeCameraComponent
{
    bool enabled = false;
    bool isMain = false;
    float fovY = DirectX::XMConvertToRadians(60.0f);
    float nearClip = 0.1f;
    float farClip = 100.0f;
};

struct RuntimeMeshRendererComponent
{
    bool visible = true;
    int materialIndex = 0;
    ScenePrimitive primitive = ScenePrimitive::Cube;
};

struct PlayerControllerComponent
{
    float moveSpeed = 5.0f;
    float lookSensitivity = 0.0018f;
    float cameraHeight = 1.6f;
};

struct TriggerVolumeComponent
{
    DirectX::XMFLOAT3 halfExtents{ 0.5f, 0.5f, 0.5f };
    bool enabled = true;
};

struct VaultNodeComponent
{
    enum class Type : uint8_t
    {
        Normal = 0,
        SlowStabilize,
        Fragile,
        Corrupted,
    };

    enum class State : uint8_t
    {
        Inactive = 0,
        Stabilizing,
        Active,
        Decaying,
    };

    Type type = Type::Normal;
    State state = State::Inactive;
    float decayTimer = 0.0f;
    float decayDuration = 9.0f;
    float stabilizeDuration = 0.0f;
    float stabilizeProgress = 0.0f;
    bool warningPlayed = false;
};

struct VaultCoreComponent
{
    bool unlocked = false;
    bool stabilized = false;
};

struct VaultRingComponent
{
    bool active = false;
    bool completed = false;
};

struct VaultExitComponent
{
    bool unlocked = false;
    bool opened = false;
    float openOffsetY = 3.5f;
};

struct VaultLordComponent
{
    bool enabled = true;
    bool discovered = false;
    bool active = false;
    float threatLevel = 0.0f;
    float influenceRadius = 25.0f;
    float distanceToPlayer = 0.0f;
};
