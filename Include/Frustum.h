#pragma once

#include <DirectXMath.h>

#include "Bounds.h"

struct Plane
{
    DirectX::XMFLOAT4 Eq{}; // (A,B,C,D)
};

struct Frustum
{
    Plane Planes[6]{};
};

// Planes order:
// 0 Left, 1 Right, 2 Bottom, 3 Top, 4 Near, 5 Far
Frustum BuildFrustumFromViewProj(const DirectX::XMFLOAT4X4& viewProj);

// Stub for CP11-B
bool PointInsideFrustum(const Frustum& frustum, const DirectX::XMFLOAT3& point);

// CP11-C
bool SphereInsideFrustum(const Sphere& s, const Frustum& f);
