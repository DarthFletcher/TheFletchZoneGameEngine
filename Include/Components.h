#pragma once

#include <DirectXMath.h>
#include "Entity.h"

// Minimal component set for editor rendering.

struct TransformComponent : public Component
{
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotationEuler{ 0.0f, 0.0f, 0.0f }; // radians
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
};

enum class MeshPrimitive : unsigned char
{
    Triangle = 0,
    Cube = 1,
};

struct MeshRendererComponent : public Component
{
    MeshPrimitive primitive = MeshPrimitive::Cube;
    DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};
