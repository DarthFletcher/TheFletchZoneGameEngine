#include "Frustum.h"

#include <cmath>

using namespace DirectX;

static Plane NormalizePlane(const XMVECTOR& p)
{
    Plane out{};

    const XMVECTOR n = XMVectorSet(XMVectorGetX(p), XMVectorGetY(p), XMVectorGetZ(p), 0.0f);
    const float len = XMVectorGetX(XMVector3Length(n));

    if (len > 0.0f)
    {
        const XMVECTOR pn = XMVectorScale(p, 1.0f / len);
        XMStoreFloat4(&out.Eq, pn);
    }
    else
    {
        XMStoreFloat4(&out.Eq, p);
    }

    return out;
}

Frustum BuildFrustumFromViewProj(const XMFLOAT4X4& viewProj)
{
    const XMMATRIX M = XMLoadFloat4x4(&viewProj);

    // Treat `M` as row-major for extraction purposes.
    const XMVECTOR r1 = M.r[0];
    const XMVECTOR r2 = M.r[1];
    const XMVECTOR r3 = M.r[2];
    const XMVECTOR r4 = M.r[3];

    Frustum fr{};

    fr.Planes[0] = NormalizePlane(XMVectorAdd(r4, r1)); // Left
    fr.Planes[1] = NormalizePlane(XMVectorSubtract(r4, r1)); // Right
    fr.Planes[2] = NormalizePlane(XMVectorAdd(r4, r2)); // Bottom
    fr.Planes[3] = NormalizePlane(XMVectorSubtract(r4, r2)); // Top

    // Near/Far: using common D3D clip-space convention.
    fr.Planes[4] = NormalizePlane(r3); // Near
    fr.Planes[5] = NormalizePlane(XMVectorSubtract(r4, r3)); // Far

    return fr;
}

bool PointInsideFrustum(const Frustum& /*frustum*/, const XMFLOAT3& /*point*/)
{
    // CP11-B
    return true;
}
