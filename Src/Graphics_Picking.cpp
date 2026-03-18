#include "Graphics.h"

#include <cmath>

using namespace DirectX;

PickRay Graphics::ComputeScenePickRay(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize) const
{
    PickRay ray{};

    const float w = (sceneSize.x > 1.0f) ? sceneSize.x : 1.0f;
    const float h = (sceneSize.y > 1.0f) ? sceneSize.y : 1.0f;

    const float localX = mousePos.x - sceneMin.x;
    const float localY = mousePos.y - sceneMin.y;

    const float ndcX = (localX / w) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (localY / h) * 2.0f;

    float aspect = (h > 0.0f) ? (w / h) : 1.0f;
    if (sceneRTWidth > 0 && sceneRTHeight > 0)
        aspect = (float)sceneRTWidth / (float)sceneRTHeight;

    const CameraData camera = sceneCamera.GetCameraData(aspect);
    const XMMATRIX view = XMLoadFloat4x4(&camera.view);
    const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);

    const XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

    const XMVECTOR nearClip = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
    const XMVECTOR farClip  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

    XMVECTOR nearWorld = XMVector4Transform(nearClip, invViewProj);
    XMVECTOR farWorld  = XMVector4Transform(farClip, invViewProj);

    nearWorld = XMVectorScale(nearWorld, 1.0f / XMVectorGetW(nearWorld));
    farWorld  = XMVectorScale(farWorld,  1.0f / XMVectorGetW(farWorld));

    const XMVECTOR dir = XMVector3Normalize(XMVectorSubtract(farWorld, nearWorld));

    XMStoreFloat3(&ray.origin, nearWorld);
    XMStoreFloat3(&ray.dir, dir);

    return ray;
}

static bool IntersectRayPlane(const PickRay& ray, const XMFLOAT3& planePoint, const XMFLOAT3& planeNormal, XMFLOAT3& outHit)
{
    const XMVECTOR ro = XMLoadFloat3(&ray.origin);
    const XMVECTOR rd = XMLoadFloat3(&ray.dir);
    const XMVECTOR p0 = XMLoadFloat3(&planePoint);
    const XMVECTOR n  = XMLoadFloat3(&planeNormal);

    const float denom = XMVectorGetX(XMVector3Dot(rd, n));
    if (fabsf(denom) < 1e-6f)
        return false;

    const float t = XMVectorGetX(XMVector3Dot((p0 - ro), n)) / denom;
    if (t < 0.0f)
        return false;

    const XMVECTOR hit = ro + rd * t;
    XMStoreFloat3(&outHit, hit);
    return true;
}

bool Graphics::TryPickSceneGridY0(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize, XMFLOAT3& outHitPos, PickRay* outRay) const
{
    const PickRay ray = ComputeScenePickRay(mousePos, sceneMin, sceneSize);
    if (outRay) *outRay = ray;

    const XMFLOAT3 planePoint{ 0.0f, 0.0f, 0.0f };
    const XMFLOAT3 planeNormal{ 0.0f, 1.0f, 0.0f };
    return IntersectRayPlane(ray, planePoint, planeNormal, outHitPos);
}

bool Graphics::TryPickSceneGridZ0(ImVec2 mousePos, ImVec2 sceneMin, ImVec2 sceneSize, XMFLOAT3& outHitPos, PickRay* outRay) const
{
    const PickRay ray = ComputeScenePickRay(mousePos, sceneMin, sceneSize);
    if (outRay) *outRay = ray;

    const XMFLOAT3 planePoint{ 0.0f, 0.0f, 0.0f };
    const XMFLOAT3 planeNormal{ 0.0f, 0.0f, 1.0f };
    return IntersectRayPlane(ray, planePoint, planeNormal, outHitPos);
}
