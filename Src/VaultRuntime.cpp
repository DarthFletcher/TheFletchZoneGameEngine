#include "VaultRuntime.h"

VaultGameplayState::NodeType ToVaultNodeType(VaultNodeComponent::Type type)
{
    switch (type)
    {
    case VaultNodeComponent::Type::SlowStabilize:
        return VaultGameplayState::NodeType::SlowStabilize;
    case VaultNodeComponent::Type::Fragile:
        return VaultGameplayState::NodeType::Fragile;
    case VaultNodeComponent::Type::Corrupted:
        return VaultGameplayState::NodeType::Corrupted;
    case VaultNodeComponent::Type::Normal:
    default:
        return VaultGameplayState::NodeType::Normal;
    }
}

VaultNodeComponent::Type ToRuntimeNodeType(VaultGameplayState::NodeType type)
{
    switch (type)
    {
    case VaultGameplayState::NodeType::SlowStabilize:
        return VaultNodeComponent::Type::SlowStabilize;
    case VaultGameplayState::NodeType::Fragile:
        return VaultNodeComponent::Type::Fragile;
    case VaultGameplayState::NodeType::Corrupted:
        return VaultNodeComponent::Type::Corrupted;
    case VaultGameplayState::NodeType::Normal:
    default:
        return VaultNodeComponent::Type::Normal;
    }
}

VaultGameplayState::NodeState ToVaultNodeState(VaultNodeComponent::State state)
{
    switch (state)
    {
    case VaultNodeComponent::State::Stabilizing:
        return VaultGameplayState::NodeState::Stabilizing;
    case VaultNodeComponent::State::Active:
        return VaultGameplayState::NodeState::Active;
    case VaultNodeComponent::State::Decaying:
        return VaultGameplayState::NodeState::Decaying;
    case VaultNodeComponent::State::Inactive:
    default:
        return VaultGameplayState::NodeState::Inactive;
    }
}

VaultNodeComponent::State ToRuntimeNodeState(VaultGameplayState::NodeState state)
{
    switch (state)
    {
    case VaultGameplayState::NodeState::Stabilizing:
        return VaultNodeComponent::State::Stabilizing;
    case VaultGameplayState::NodeState::Active:
        return VaultNodeComponent::State::Active;
    case VaultGameplayState::NodeState::Decaying:
        return VaultNodeComponent::State::Decaying;
    case VaultGameplayState::NodeState::Inactive:
    default:
        return VaultNodeComponent::State::Inactive;
    }
}
