#include "EditorGizmo.h"

#include "CameraData.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace DirectX;

namespace
{
    struct WorldRay
    {
        XMFLOAT3 origin{};
        XMFLOAT3 direction{};
    };

    static bool ProjectWorldToScreen(
        const XMFLOAT3& worldPos,
        const CameraData& camera,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        ImVec2& outScreen)
    {
        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        const XMVECTOR clip = XMVector4Transform(pos, XMMatrixMultiply(view, proj));

        const float w = XMVectorGetW(clip);
        if (fabsf(w) < 1e-6f)
            return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        const float ndcZ = XMVectorGetZ(clip) / w;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return false;

        outScreen.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
        outScreen.y = sceneMin.y + (-ndcY * 0.5f + 0.5f) * sceneSize.y;
        return true;
    }

    static bool ScreenPointToWorldRay(
        ImVec2 mousePos,
        const CameraData& camera,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        WorldRay& outRay)
    {
        const float width = (std::max)(sceneSize.x, 1.0f);
        const float height = (std::max)(sceneSize.y, 1.0f);
        const float localX = mousePos.x - sceneMin.x;
        const float localY = mousePos.y - sceneMin.y;

        const float px = (2.0f * localX / width) - 1.0f;
        const float py = 1.0f - (2.0f * localY / height);

        const XMVECTOR nearClip = XMVectorSet(px, py, 0.0f, 1.0f);
        const XMVECTOR farClip = XMVectorSet(px, py, 1.0f, 1.0f);
        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMMATRIX invViewProj = XMMatrixInverse(nullptr, XMMatrixMultiply(view, proj));

        XMVECTOR nearWorld = XMVector4Transform(nearClip, invViewProj);
        XMVECTOR farWorld = XMVector4Transform(farClip, invViewProj);

        const float nearW = XMVectorGetW(nearWorld);
        const float farW = XMVectorGetW(farWorld);
        if (fabsf(nearW) < 1e-6f || fabsf(farW) < 1e-6f)
            return false;

        nearWorld = XMVectorScale(nearWorld, 1.0f / nearW);
        farWorld = XMVectorScale(farWorld, 1.0f / farW);

        XMStoreFloat3(&outRay.origin, nearWorld);
        XMStoreFloat3(&outRay.direction, XMVector3Normalize(XMVectorSubtract(farWorld, nearWorld)));
        return true;
    }

    static bool IntersectRayPlane(const WorldRay& ray, const XMFLOAT3& planePoint, const XMFLOAT3& planeNormal, XMFLOAT3& outHit)
    {
        const XMVECTOR origin = XMLoadFloat3(&ray.origin);
        const XMVECTOR dir = XMLoadFloat3(&ray.direction);
        const XMVECTOR point = XMLoadFloat3(&planePoint);
        const XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&planeNormal));

        const float denom = XMVectorGetX(XMVector3Dot(dir, normal));
        if (fabsf(denom) < 1e-6f)
            return false;

        const float t = XMVectorGetX(XMVector3Dot(point - origin, normal)) / denom;
        if (t < 0.0f)
            return false;

        XMStoreFloat3(&outHit, origin + dir * t);
        return true;
    }

    static float ComputeGizmoAxisLength(const XMFLOAT3& objectPos, const CameraData& camera)
    {
        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMVECTOR obj = XMVectorSet(objectPos.x, objectPos.y, objectPos.z, 1.0f);
        const XMVECTOR viewPos = XMVector3TransformCoord(obj, view);
        const float depth = fabsf(XMVectorGetZ(viewPos));
        const float scale = (std::clamp)(depth * 0.10f, 0.08f, 2.5f);
        return 80.0f * scale;
    }

    static float SnapValue(float value, float step)
    {
        if (step <= 0.0f)
            return value;
        return std::round(value / step) * step;
    }

    static float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float apx = p.x - a.x;
        const float apy = p.y - a.y;
        const float abLenSq = abx * abx + aby * aby;
        if (abLenSq <= 1e-6f)
            return std::sqrt(apx * apx + apy * apy);

        const float t = (std::max)(0.0f, (std::min)(1.0f, (apx * abx + apy * aby) / abLenSq));
        const float cx = a.x + abx * t;
        const float cy = a.y + aby * t;
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        return std::sqrt(dx * dx + dy * dy);
    }

    static float DistancePointToPolyline(ImVec2 p, const std::vector<ImVec2>& points)
    {
        if (points.size() < 2)
            return FLT_MAX;

        float best = FLT_MAX;
        for (size_t i = 0; i + 1 < points.size(); ++i)
            best = (std::min)(best, DistancePointToSegment(p, points[i], points[i + 1]));

        return best;
    }

    static float Distance(ImVec2 a, ImVec2 b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    static ImVec2 Normalize(ImVec2 v)
    {
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len <= 1e-6f)
            return ImVec2(0.0f, 0.0f);
        return ImVec2(v.x / len, v.y / len);
    }

    static float Sign(ImVec2 p1, ImVec2 p2, ImVec2 p3)
    {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    }

    static bool PointInTriangle(ImVec2 pt, ImVec2 a, ImVec2 b, ImVec2 c)
    {
        const float d1 = Sign(pt, a, b);
        const float d2 = Sign(pt, b, c);
        const float d3 = Sign(pt, c, a);

        const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
        const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
        return !(hasNeg && hasPos);
    }

    static bool PointInQuad(ImVec2 pt, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d)
    {
        return PointInTriangle(pt, a, b, c) || PointInTriangle(pt, a, c, d);
    }

    static ImU32 AxisColor(int axis, bool active, bool hovered)
    {
        if (active)
            return IM_COL32(255, 255, 255, 255);
        if (hovered)
            return IM_COL32(255, 220, 64, 255);

        switch (axis)
        {
        case 0: return IM_COL32(255, 64, 64, 255);
        case 1: return IM_COL32(64, 255, 64, 255);
        case 2: return IM_COL32(64, 128, 255, 255);
        default: return IM_COL32(255, 255, 255, 255);
        }
    }

    static ImU32 PlaneColor(int plane, bool active, bool hovered)
    {
        if (active)
            return IM_COL32(255, 255, 255, 90);
        if (hovered)
            return IM_COL32(255, 220, 64, 90);

        switch (plane)
        {
        case 0: return IM_COL32(255, 200, 0, 80);
        case 1: return IM_COL32(0, 200, 255, 80);
        case 2: return IM_COL32(255, 0, 200, 80);
        default: return IM_COL32(255, 255, 255, 70);
        }
    }

    static ImVec2 Perpendicular(ImVec2 v)
    {
        return ImVec2(-v.y, v.x);
    }

    static float Saturate(float v)
    {
        return (std::max)(0.0f, (std::min)(1.0f, v));
    }

    static ImU32 WithAlpha(ImU32 color, float alphaScale)
    {
        const ImU32 a = (color >> IM_COL32_A_SHIFT) & 0xFFu;
        const ImU32 r = (color >> IM_COL32_R_SHIFT) & 0xFFu;
        const ImU32 g = (color >> IM_COL32_G_SHIFT) & 0xFFu;
        const ImU32 b = (color >> IM_COL32_B_SHIFT) & 0xFFu;
        const ImU32 outA = (ImU32)std::round((float)a * Saturate(alphaScale));
        return IM_COL32(r, g, b, outA);
    }

    static float PlaneVisibility(const CameraData& camera, const XMFLOAT3& planeNormal)
    {
        const XMVECTOR normal = XMVector3Normalize(XMLoadFloat3(&planeNormal));
        const XMMATRIX invView = XMMatrixInverse(nullptr, XMLoadFloat4x4(&camera.view));
        const XMVECTOR cameraForward = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), invView));
        const float dot = fabsf(XMVectorGetX(XMVector3Dot(cameraForward, normal)));
        return (std::clamp)((1.0f - dot) * 1.5f, 0.2f, 1.0f);
    }

    static void BuildArrowhead(ImVec2 origin, ImVec2 tip, float size, ImVec2& outA, ImVec2& outB, ImVec2& outC)
    {
        const ImVec2 axisDir = Normalize(ImVec2(tip.x - origin.x, tip.y - origin.y));
        const ImVec2 perp = Normalize(Perpendicular(axisDir));
        outA = tip;
        outB = ImVec2(tip.x - axisDir.x * size + perp.x * size * 0.5f, tip.y - axisDir.y * size + perp.y * size * 0.5f);
        outC = ImVec2(tip.x - axisDir.x * size - perp.x * size * 0.5f, tip.y - axisDir.y * size - perp.y * size * 0.5f);
    }

    static void ComputeAxisBasis(const SceneInstance& instance, EditorGizmo::Space space, XMFLOAT3& outX, XMFLOAT3& outY, XMFLOAT3& outZ)
    {
        if (space == EditorGizmo::Space::World)
        {
            outX = { 1.0f, 0.0f, 0.0f };
            outY = { 0.0f, 1.0f, 0.0f };
            outZ = { 0.0f, 0.0f, 1.0f };
            return;
        }

        const XMMATRIX rot = XMMatrixRotationRollPitchYaw(
            instance.rotation.x,
            instance.rotation.y,
            instance.rotation.z);

        XMVECTOR x = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rot));
        XMVECTOR y = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rot));
        XMVECTOR z = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot));

        XMStoreFloat3(&outX, x);
        XMStoreFloat3(&outY, y);
        XMStoreFloat3(&outZ, z);
    }

    static XMFLOAT3 AddScaled(const XMFLOAT3& origin, const XMFLOAT3& dir, float scale)
    {
        return XMFLOAT3(
            origin.x + dir.x * scale,
            origin.y + dir.y * scale,
            origin.z + dir.z * scale);
    }

    static float Dot3(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static XMFLOAT3 Cross3(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return XMFLOAT3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
    }

    static XMFLOAT3 Normalize3(const XMFLOAT3& v)
    {
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len <= 1e-6f)
            return XMFLOAT3(0.0f, 0.0f, 0.0f);
        return XMFLOAT3(v.x / len, v.y / len, v.z / len);
    }

    static XMFLOAT3 ProjectOntoBasis(const XMFLOAT3& delta, const XMFLOAT3& a, const XMFLOAT3& b)
    {
        const float da = Dot3(delta, a);
        const float db = Dot3(delta, b);
        return XMFLOAT3(
            a.x * da + b.x * db,
            a.y * da + b.y * db,
            a.z * da + b.z * db);
    }

    static float AngleFromCenter(ImVec2 center, ImVec2 point)
    {
        return std::atan2(point.y - center.y, point.x - center.x);
    }

    static float NormalizeAngleDelta(float delta)
    {
        while (delta > XM_PI)
            delta -= XM_2PI;
        while (delta < -XM_PI)
            delta += XM_2PI;
        return delta;
    }

    static ImU32 RotateRingColor(int axis, bool active, bool hovered)
    {
        return AxisColor(axis, active, hovered);
    }

    static void BuildOrientedRingPoints(
        ImVec2 origin,
        ImVec2 axisA,
        ImVec2 axisB,
        float radiusA,
        float radiusB,
        int segments,
        std::vector<ImVec2>& outPoints)
    {
        outPoints.clear();
        outPoints.reserve((size_t)segments + 1);

        for (int i = 0; i <= segments; ++i)
        {
            const float t = ((float)i / (float)segments) * XM_2PI;
            const float c = cosf(t);
            const float s = sinf(t);
            outPoints.emplace_back(
                origin.x + axisA.x * radiusA * c + axisB.x * radiusB * s,
                origin.y + axisA.y * radiusA * c + axisB.y * radiusB * s);
        }
    }

    static ImVec2 ClosestPointOnSegment(ImVec2 p, ImVec2 a, ImVec2 b)
    {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float abLenSq = abx * abx + aby * aby;
        if (abLenSq <= 1e-6f)
            return a;

        const float apx = p.x - a.x;
        const float apy = p.y - a.y;
        const float t = (std::max)(0.0f, (std::min)(1.0f, (apx * abx + apy * aby) / abLenSq));
        return ImVec2(a.x + abx * t, a.y + aby * t);
    }

    static ImVec2 FindClosestRingTangent(ImVec2 p, const std::vector<ImVec2>& points, ImVec2& outClosestPoint)
    {
        float best = FLT_MAX;
        ImVec2 bestTangent{ 1.0f, 0.0f };
        outClosestPoint = p;

        if (points.size() < 2)
            return bestTangent;

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const ImVec2 a = points[i];
            const ImVec2 b = points[i + 1];
            const ImVec2 closest = ClosestPointOnSegment(p, a, b);
            const float d = Distance(p, closest);
            if (d < best)
            {
                best = d;
                bestTangent = Normalize(ImVec2(b.x - a.x, b.y - a.y));
                outClosestPoint = closest;
            }
        }

        if (bestTangent.x == 0.0f && bestTangent.y == 0.0f)
            bestTangent = ImVec2(1.0f, 0.0f);
        return bestTangent;
    }

    static SceneInstance* FindSceneInstanceById(uint32_t instanceId)
    {
        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            SceneInstance* instance = Scene::GetInstance(i);
            if (instance && instance->instanceId == instanceId)
                return instance;
        }
        return nullptr;
    }

    static XMFLOAT3 RotatePointAroundAxis(const XMFLOAT3& point, const XMFLOAT3& center, const XMFLOAT3& axis, float angle)
    {
        const XMVECTOR p = XMLoadFloat3(&point);
        const XMVECTOR c = XMLoadFloat3(&center);
        const XMVECTOR a = XMVector3Normalize(XMLoadFloat3(&axis));
        const XMMATRIX rot = XMMatrixRotationAxis(a, angle);
        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3TransformCoord(p - c, rot) + c);
        return result;
    }

    static bool TryGetParentWorldMatrix(const SceneInstance& instance, XMMATRIX& outParentWorld)
    {
        const uint32_t parentId = Scene::GetParentInstanceId(instance.instanceId);
        if (parentId == 0)
            return false;

        XMFLOAT4X4 parentWorldF{};
        if (!Scene::TryGetInstanceWorldMatrix(parentId, parentWorldF))
            return false;

        outParentWorld = XMLoadFloat4x4(&parentWorldF);
        return true;
    }

    static XMFLOAT3 QuaternionToEuler(const XMFLOAT4& q)
    {
        const float ysqr = q.y * q.y;
        const float t0 = +2.0f * (q.w * q.x + q.y * q.z);
        const float t1 = +1.0f - 2.0f * (q.x * q.x + ysqr);
        const float t2 = (std::clamp)(+2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
        const float t3 = +2.0f * (q.w * q.z + q.x * q.y);
        const float t4 = +1.0f - 2.0f * (ysqr + q.z * q.z);
        return { std::atan2(t0, t1), std::asin(t2), std::atan2(t3, t4) };
    }

    static XMFLOAT3 GetInstanceWorldPosition(const SceneInstance& instance)
    {
        return Scene::GetInstanceWorldPosition(instance.instanceId);
    }

    static void ComputeAxisBasisFromWorld(const SceneInstance& instance, EditorGizmo::Space space, XMFLOAT3& outX, XMFLOAT3& outY, XMFLOAT3& outZ)
    {
        if (space == EditorGizmo::Space::World)
        {
            outX = { 1.0f, 0.0f, 0.0f };
            outY = { 0.0f, 1.0f, 0.0f };
            outZ = { 0.0f, 0.0f, 1.0f };
            return;
        }

        XMFLOAT4X4 worldF{};
        if (!Scene::TryGetInstanceWorldMatrix(instance.instanceId, worldF))
        {
            ComputeAxisBasis(instance, space, outX, outY, outZ);
            return;
        }

        const XMMATRIX world = XMLoadFloat4x4(&worldF);
        XMStoreFloat3(&outX, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), world)));
        XMStoreFloat3(&outY, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), world)));
        XMStoreFloat3(&outZ, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), world)));
    }

    static void SetInstanceWorldPosition(SceneInstance& instance, const XMFLOAT3& worldPos)
    {
        XMMATRIX parentWorld{};
        if (!TryGetParentWorldMatrix(instance, parentWorld))
        {
            instance.position = worldPos;
            return;
        }

        XMFLOAT3 localPos{};
        XMStoreFloat3(&localPos, XMVector3TransformCoord(XMLoadFloat3(&worldPos), XMMatrixInverse(nullptr, parentWorld)));
        instance.position = localPos;
    }

    static void ApplyWorldAxisRotationToLocal(SceneInstance& instance, const XMFLOAT3& startLocalEuler, const XMFLOAT3& worldAxis, float angle)
    {
        const XMMATRIX localStart = XMMatrixRotationRollPitchYaw(startLocalEuler.x, startLocalEuler.y, startLocalEuler.z);
        const XMMATRIX deltaWorld = XMMatrixRotationAxis(XMVector3Normalize(XMLoadFloat3(&worldAxis)), angle);

        XMMATRIX localResult = XMMatrixMultiply(localStart, deltaWorld);
        XMMATRIX parentWorld{};
        if (TryGetParentWorldMatrix(instance, parentWorld))
        {
            XMVECTOR parentScale{}, parentRotQ{}, parentTrans{};
            if (XMMatrixDecompose(&parentScale, &parentRotQ, &parentTrans, parentWorld))
            {
                const XMMATRIX parentRot = XMMatrixRotationQuaternion(parentRotQ);
                localResult = XMMatrixMultiply(XMMatrixMultiply(localStart, parentRot), XMMatrixMultiply(deltaWorld, XMMatrixInverse(nullptr, parentRot)));
            }
        }

        XMVECTOR outScale{}, outRotQ{}, outTrans{};
        if (!XMMatrixDecompose(&outScale, &outRotQ, &outTrans, localResult))
            return;

        XMFLOAT4 q{};
        XMStoreFloat4(&q, outRotQ);
        instance.rotation = QuaternionToEuler(q);
    }

    static XMFLOAT3 ComputeAxisDragPlaneNormal(const XMFLOAT3& axis, const XMFLOAT3& viewDir)
    {
        const XMFLOAT3 axisN = Normalize3(axis);
        const float axisDotView = Dot3(axisN, viewDir);
        XMFLOAT3 planeNormal{
            viewDir.x - axisN.x * axisDotView,
            viewDir.y - axisN.y * axisDotView,
            viewDir.z - axisN.z * axisDotView
        };
        planeNormal = Normalize3(planeNormal);
        if (fabsf(planeNormal.x) <= 1e-6f && fabsf(planeNormal.y) <= 1e-6f && fabsf(planeNormal.z) <= 1e-6f)
        {
            const XMFLOAT3 fallback = (fabsf(axisN.y) < 0.9f) ? XMFLOAT3{ 0.0f, 1.0f, 0.0f } : XMFLOAT3{ 1.0f, 0.0f, 0.0f };
            planeNormal = Normalize3(Cross3(axisN, Cross3(fallback, axisN)));
        }
        return planeNormal;
    }
}

void EditorGizmo::SetMode(Mode m)
{
    if (mode == m)
        return;

    mode = m;
    dragging = false;
    hoveredAxis = -1;
    activeAxis = -1;
    hoveredPlane = -1;
    activePlane = -1;
    hoveredCenter = false;
    activeCenter = false;
    hasDragStartHit = false;
    activeRotationDegrees = 0.0f;
    dragStartSelectionPositions.clear();
    dragStartSelectionRotations.clear();
    dragStartSelectionScales.clear();
}

EditorGizmo::Mode EditorGizmo::GetMode() const
{
    return mode;
}

void EditorGizmo::Update(
    SceneInstance* instance,
    const CameraData& camera,
    ImDrawList* drawList,
    ImVec2 sceneMin,
    ImVec2 sceneSize,
    bool viewportHovered,
    bool allowInput,
    bool useSelectionCenter)
{
    if (!instance || !drawList)
        return;

    ImGuiIO& io = ImGui::GetIO();
    auto captureDragSelection = [this, instance]() {
        dragStartSelectionPositions.clear();
        dragStartSelectionRotations.clear();
        dragStartSelectionScales.clear();
        const auto& selectedIds = Scene::GetSelectedInstanceIds();
        if (!selectedIds.empty())
        {
            dragStartSelectionPositions.reserve(selectedIds.size());
            dragStartSelectionRotations.reserve(selectedIds.size());
            dragStartSelectionScales.reserve(selectedIds.size());
            for (uint32_t id : selectedIds)
            {
                if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
                {
                    dragStartSelectionPositions.emplace_back(id, GetInstanceWorldPosition(*selectedInstance));
                    dragStartSelectionRotations.emplace_back(id, selectedInstance->rotation);
                    dragStartSelectionScales.emplace_back(id, selectedInstance->scale);
                }
            }
        }

        if (dragStartSelectionPositions.empty() && instance)
        {
            dragStartSelectionPositions.emplace_back(instance->instanceId, GetInstanceWorldPosition(*instance));
            dragStartSelectionRotations.emplace_back(instance->instanceId, instance->rotation);
            dragStartSelectionScales.emplace_back(instance->instanceId, instance->scale);
        }
    };

    auto applyGroupTranslationDelta = [this](const XMFLOAT3& delta) {
        for (const auto& [id, startPos] : dragStartSelectionPositions)
        {
            if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
            {
                const XMFLOAT3 targetWorldPos{
                    startPos.x + delta.x,
                    startPos.y + delta.y,
                    startPos.z + delta.z
                };
                SetInstanceWorldPosition(*selectedInstance, targetWorldPos);
            }
        }
    };

    auto applyGroupRotationDelta = [this](const XMFLOAT3& center, const XMFLOAT3& axis, float angle) {
        for (const auto& [id, startPos] : dragStartSelectionPositions)
        {
            if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
                SetInstanceWorldPosition(*selectedInstance, RotatePointAroundAxis(startPos, center, axis, angle));
        }

        for (const auto& [id, startRot] : dragStartSelectionRotations)
        {
            if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
            {
                ApplyWorldAxisRotationToLocal(*selectedInstance, startRot, axis, angle);
            }
        }
    };

    auto applyGroupScaleDelta = [this](const XMFLOAT3& center, const XMFLOAT3& axis, float factor, bool uniform) {
        const XMVECTOR centerV = XMLoadFloat3(&center);
        const XMVECTOR axisV = XMVector3Normalize(XMLoadFloat3(&axis));

        for (const auto& [id, startPos] : dragStartSelectionPositions)
        {
            if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
            {
                XMVECTOR offset = XMLoadFloat3(&startPos) - centerV;
                if (uniform)
                {
                    offset *= factor;
                }
                else
                {
                    const float amount = XMVectorGetX(XMVector3Dot(offset, axisV));
                    const XMVECTOR parallel = axisV * amount;
                    const XMVECTOR perpendicular = offset - parallel;
                    offset = perpendicular + parallel * factor;
                }

                XMFLOAT3 targetWorldPos{};
                XMStoreFloat3(&targetWorldPos, centerV + offset);
                SetInstanceWorldPosition(*selectedInstance, targetWorldPos);
            }
        }

        for (const auto& [id, startScale] : dragStartSelectionScales)
        {
            if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
            {
                selectedInstance->scale = startScale;
                if (uniform)
                {
                    selectedInstance->scale.x = (std::max)(startScale.x * factor, 0.01f);
                    selectedInstance->scale.y = (std::max)(startScale.y * factor, 0.01f);
                    selectedInstance->scale.z = (std::max)(startScale.z * factor, 0.01f);
                }
                else if (fabsf(axis.x) > 0.5f)
                {
                    selectedInstance->scale.x = (std::max)(startScale.x * factor, 0.01f);
                }
                else if (fabsf(axis.y) > 0.5f)
                {
                    selectedInstance->scale.y = (std::max)(startScale.y * factor, 0.01f);
                }
                else if (fabsf(axis.z) > 0.5f)
                {
                    selectedInstance->scale.z = (std::max)(startScale.z * factor, 0.01f);
                }
            }
        }
    };

    if (allowInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) SetMode(Mode::Translate);
        if (ImGui::IsKeyPressed(ImGuiKey_E)) SetMode(Mode::Rotate);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) SetMode(Mode::Scale);
        if (ImGui::IsKeyPressed(ImGuiKey_X)) ToggleSpace();
    }

    const bool multiSelection = Scene::GetSelectedInstanceIds().size() > 1;
    const bool useCenterAnchor = multiSelection && useSelectionCenter;
    const XMFLOAT3 gizmoOriginWorld = (((mode == Mode::Translate) || (mode == Mode::Rotate) || (mode == Mode::Scale)) && useCenterAnchor)
        ? Scene::GetSelectionCenterOrActivePosition()
        : GetInstanceWorldPosition(*instance);
    const Space effectiveSpace = ((mode == Mode::Translate) || (mode == Mode::Rotate) || (mode == Mode::Scale)) && multiSelection ? Space::World : space;

    ImVec2 origin{};
    if (!ProjectWorldToScreen(gizmoOriginWorld, camera, sceneMin, sceneSize, origin))
        return;

    const float axisLength = ComputeGizmoAxisLength(gizmoOriginWorld, camera);

    XMFLOAT3 axisXWorld{}, axisYWorld{}, axisZWorld{};
    ComputeAxisBasisFromWorld(*instance, effectiveSpace, axisXWorld, axisYWorld, axisZWorld);

    ImVec2 xSample{}, ySample{}, zSample{};
    constexpr float axisWorldSample = 1.0f;
    if (!ProjectWorldToScreen(AddScaled(gizmoOriginWorld, axisXWorld, axisWorldSample), camera, sceneMin, sceneSize, xSample) ||
        !ProjectWorldToScreen(AddScaled(gizmoOriginWorld, axisYWorld, axisWorldSample), camera, sceneMin, sceneSize, ySample) ||
        !ProjectWorldToScreen(AddScaled(gizmoOriginWorld, axisZWorld, axisWorldSample), camera, sceneMin, sceneSize, zSample))
        return;

    const ImVec2 xDir = Normalize(ImVec2(xSample.x - origin.x, xSample.y - origin.y));
    const ImVec2 yDir = Normalize(ImVec2(ySample.x - origin.x, ySample.y - origin.y));
    const ImVec2 zDir = Normalize(ImVec2(zSample.x - origin.x, zSample.y - origin.y));

    if ((xDir.x == 0.0f && xDir.y == 0.0f) ||
        (yDir.x == 0.0f && yDir.y == 0.0f) ||
        (zDir.x == 0.0f && zDir.y == 0.0f))
        return;

    const float xPixelsPerUnit = (std::max)(Distance(origin, xSample), 1.0f);
    const float yPixelsPerUnit = (std::max)(Distance(origin, ySample), 1.0f);
    const float zPixelsPerUnit = (std::max)(Distance(origin, zSample), 1.0f);

    if (mode == Mode::Rotate)
    {
        const float baseRadiusPx = axisLength * 0.95f;
        const float ringThickness = (std::clamp)(axisLength * 0.10f, 10.0f, 18.0f);

        const float ringRadiusWorldX = baseRadiusPx / ((yPixelsPerUnit + zPixelsPerUnit) * 0.5f);
        const float ringRadiusWorldY = baseRadiusPx / ((xPixelsPerUnit + zPixelsPerUnit) * 0.5f);
        const float ringRadiusWorldZ = baseRadiusPx / ((xPixelsPerUnit + yPixelsPerUnit) * 0.5f);

        std::vector<ImVec2> ringX;
        std::vector<ImVec2> ringY;
        std::vector<ImVec2> ringZ;
        BuildOrientedRingPoints(origin, yDir, zDir, ringRadiusWorldX * yPixelsPerUnit, ringRadiusWorldX * zPixelsPerUnit, 64, ringX);
        BuildOrientedRingPoints(origin, xDir, zDir, ringRadiusWorldY * xPixelsPerUnit, ringRadiusWorldY * zPixelsPerUnit, 64, ringY);
        BuildOrientedRingPoints(origin, xDir, yDir, ringRadiusWorldZ * xPixelsPerUnit, ringRadiusWorldZ * yPixelsPerUnit, 64, ringZ);

        hoveredAxis = -1;
        hoveredPlane = -1;
        hoveredCenter = false;

        if (!dragging && viewportHovered && allowInput)
        {
            const ImVec2 mouse = io.MousePos;
            const float dx = DistancePointToPolyline(mouse, ringX);
            const float dy = DistancePointToPolyline(mouse, ringY);
            const float dz = DistancePointToPolyline(mouse, ringZ);
            float best = ringThickness;
            if (dx < best) { best = dx; hoveredAxis = 0; }
            if (dy < best) { best = dy; hoveredAxis = 1; }
            if (dz < best) { best = dz; hoveredAxis = 2; }
        }

        if (allowInput && !dragging && hoveredAxis != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            dragging = true;
            activeAxis = hoveredAxis;
            activePlane = -1;
            activeCenter = false;
            dragStartRotation = instance->rotation;
            dragStartPosition = gizmoOriginWorld;
            dragStartAngle = AngleFromCenter(origin, io.MousePos);
            dragStartMouse = io.MousePos;
            captureDragSelection();
            switch (activeAxis)
            {
            case 0: dragStartTangent = FindClosestRingTangent(io.MousePos, ringX, dragStartRingPoint); break;
            case 1: dragStartTangent = FindClosestRingTangent(io.MousePos, ringY, dragStartRingPoint); break;
            case 2: dragStartTangent = FindClosestRingTangent(io.MousePos, ringZ, dragStartRingPoint); break;
            default: dragStartTangent = ImVec2(1.0f, 0.0f); dragStartRingPoint = io.MousePos; break;
            }
            activeRotationDegrees = 0.0f;
        }

        if (dragging)
        {
            if (allowInput && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 mouseDelta{ io.MousePos.x - dragStartMouse.x, io.MousePos.y - dragStartMouse.y };
                const float tangentDelta = mouseDelta.x * dragStartTangent.x + mouseDelta.y * dragStartTangent.y;
                float appliedDelta = tangentDelta * 0.03f;
                constexpr float snapStep = XM_PIDIV4 / 2.0f;
                const bool snapEnabled = io.KeyCtrl;
                if (snapEnabled)
                    appliedDelta = SnapValue(appliedDelta, snapStep);

                activeRotationDegrees = XMConvertToDegrees(appliedDelta);
                if (multiSelection)
                {
                    const XMFLOAT3 axis = (activeAxis == 0) ? axisXWorld : (activeAxis == 1) ? axisYWorld : axisZWorld;
                    applyGroupRotationDelta(dragStartPosition, axis, appliedDelta);
                }
                else
                {
                    if (effectiveSpace == Space::World)
                    {
                        const XMFLOAT3 axis = (activeAxis == 0) ? axisXWorld : (activeAxis == 1) ? axisYWorld : axisZWorld;
                        ApplyWorldAxisRotationToLocal(*instance, dragStartRotation, axis, appliedDelta);
                    }
                    else
                    {
                        instance->rotation = dragStartRotation;
                        switch (activeAxis)
                        {
                        case 0: instance->rotation.x += appliedDelta; break;
                        case 1: instance->rotation.y += appliedDelta; break;
                        case 2: instance->rotation.z += appliedDelta; break;
                        default: break;
                        }
                    }
                }
            }
            else
            {
                dragging = false;
                activeAxis = -1;
                activeRotationDegrees = 0.0f;
            }
        }

        drawList->AddPolyline(ringX.data(), (int)ringX.size(), RotateRingColor(0, activeAxis == 0, hoveredAxis == 0), ImDrawFlags_None, 3.0f);
        drawList->AddPolyline(ringY.data(), (int)ringY.size(), RotateRingColor(1, activeAxis == 1, hoveredAxis == 1), ImDrawFlags_None, 3.0f);
        drawList->AddPolyline(ringZ.data(), (int)ringZ.size(), RotateRingColor(2, activeAxis == 2, hoveredAxis == 2), ImDrawFlags_None, 3.0f);
        drawList->AddCircleFilled(origin, 3.5f, IM_COL32(245, 245, 245, 180));
        return;
    }

    const float arrowSize = axisLength * 0.10f;
    const float planeOffset = axisLength * 0.18f;
    const float planeSize = axisLength * 0.16f;
    const float centerRadius = axisLength * 0.08f;

    const ImVec2 xEnd{ origin.x + xDir.x * axisLength, origin.y + xDir.y * axisLength };
    const ImVec2 yEnd{ origin.x + yDir.x * axisLength, origin.y + yDir.y * axisLength };
    const ImVec2 zEnd{ origin.x + zDir.x * axisLength * 0.7f, origin.y + zDir.y * axisLength * 0.7f };

    if (mode == Mode::Scale)
    {
        constexpr float kMinScale = 0.01f;
        constexpr float kScaleSnap = 0.1f;
        const float handleHalf = (std::clamp)(axisLength * 0.11f, 7.0f, 16.0f);
        const float centerHalf = handleHalf * 0.85f;

        auto pointInBox = [](ImVec2 p, ImVec2 c, float half)
        {
            return p.x >= (c.x - half) && p.x <= (c.x + half) && p.y >= (c.y - half) && p.y <= (c.y + half);
        };

        hoveredAxis = -1;
        hoveredPlane = -1;
        hoveredCenter = false;
        if (!dragging && viewportHovered && allowInput)
        {
            const ImVec2 mouse = io.MousePos;
            hoveredCenter = pointInBox(mouse, origin, centerHalf + 3.0f);
            if (!hoveredCenter)
            {
                if (pointInBox(mouse, xEnd, handleHalf)) hoveredAxis = 0;
                else if (pointInBox(mouse, yEnd, handleHalf)) hoveredAxis = 1;
                else if (pointInBox(mouse, zEnd, handleHalf)) hoveredAxis = 2;
            }
        }

        if (allowInput && !dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (hoveredCenter)
            {
                dragging = true;
                activeCenter = true;
                activeAxis = -1;
                dragStartScale = instance->scale;
                dragStartPosition = gizmoOriginWorld;
                dragStartMouse = io.MousePos;
                captureDragSelection();
            }
            else if (hoveredAxis != -1)
            {
                dragging = true;
                activeAxis = hoveredAxis;
                activeCenter = false;
                dragStartScale = instance->scale;
                dragStartPosition = gizmoOriginWorld;
                dragStartMouse = io.MousePos;
                captureDragSelection();
            }
        }

        if (dragging)
        {
            if (allowInput && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 mouseDelta{ io.MousePos.x - dragStartMouse.x, io.MousePos.y - dragStartMouse.y };
                const bool snapEnabled = io.KeyCtrl;

                if (activeCenter)
                {
                    float factor = 1.0f + (mouseDelta.x - mouseDelta.y) * 0.006f;
                    if (snapEnabled)
                        factor = SnapValue(factor, 0.1f);
                    factor = (std::max)(factor, kMinScale / (std::max)(dragStartScale.x, (std::max)(dragStartScale.y, dragStartScale.z)));

                    if (multiSelection)
                    {
                        applyGroupScaleDelta(dragStartPosition, XMFLOAT3{ 1.0f, 1.0f, 1.0f }, factor, true);
                    }
                    else
                    {
                        instance->scale = {
                            (std::max)(dragStartScale.x * factor, kMinScale),
                            (std::max)(dragStartScale.y * factor, kMinScale),
                            (std::max)(dragStartScale.z * factor, kMinScale)
                        };
                    }
                }
                else
                {
                    const ImVec2 axisDir2D = (activeAxis == 0) ? xDir : (activeAxis == 1) ? yDir : zDir;
                    const float signedDelta = mouseDelta.x * axisDir2D.x + mouseDelta.y * axisDir2D.y;
                    float factor = 1.0f + signedDelta * 0.006f;
                    factor = (std::max)(factor, 0.01f);

                    if (multiSelection)
                    {
                        const XMFLOAT3 axis = (activeAxis == 0) ? axisXWorld : (activeAxis == 1) ? axisYWorld : axisZWorld;
                        applyGroupScaleDelta(dragStartPosition, axis, factor, false);
                        if (snapEnabled)
                        {
                            for (const auto& [id, startScale] : dragStartSelectionScales)
                            {
                                if (SceneInstance* selectedInstance = FindSceneInstanceById(id))
                                {
                                    float* axisScale = (activeAxis == 0) ? &selectedInstance->scale.x : (activeAxis == 1) ? &selectedInstance->scale.y : &selectedInstance->scale.z;
                                    *axisScale = (std::max)(SnapValue(*axisScale, kScaleSnap), kMinScale);
                                }
                            }
                        }
                    }
                    else
                    {
                        instance->scale = dragStartScale;
                        float* axisScale = (activeAxis == 0) ? &instance->scale.x : (activeAxis == 1) ? &instance->scale.y : &instance->scale.z;
                        *axisScale = (std::max)(*axisScale * factor, kMinScale);
                        if (snapEnabled)
                            *axisScale = (std::max)(SnapValue(*axisScale, kScaleSnap), kMinScale);
                    }
                }
            }
            else
            {
                dragging = false;
                activeAxis = -1;
                activeCenter = false;
            }
        }

        const ImU32 centerColor = activeCenter ? IM_COL32(255, 255, 255, 235) : (hoveredCenter ? IM_COL32(255, 220, 64, 235) : IM_COL32(245, 245, 245, 230));
        drawList->AddLine(origin, xEnd, AxisColor(0, activeAxis == 0, hoveredAxis == 0), 3.0f);
        drawList->AddLine(origin, yEnd, AxisColor(1, activeAxis == 1, hoveredAxis == 1), 3.0f);
        drawList->AddLine(origin, zEnd, AxisColor(2, activeAxis == 2, hoveredAxis == 2), 3.0f);
        drawList->AddRectFilled(ImVec2(xEnd.x - handleHalf, xEnd.y - handleHalf), ImVec2(xEnd.x + handleHalf, yEnd.y + handleHalf), AxisColor(0, activeAxis == 0, hoveredAxis == 0), 2.0f);
        drawList->AddRectFilled(ImVec2(yEnd.x - handleHalf, yEnd.y - handleHalf), ImVec2(yEnd.x + handleHalf, yEnd.y + handleHalf), AxisColor(1, activeAxis == 1, hoveredAxis == 1), 2.0f);
        drawList->AddRectFilled(ImVec2(zEnd.x - handleHalf, zEnd.y - handleHalf), ImVec2(zEnd.x + handleHalf, zEnd.y + handleHalf), AxisColor(2, activeAxis == 2, hoveredAxis == 2), 2.0f);
        drawList->AddRectFilled(ImVec2(origin.x - centerHalf, origin.y - centerHalf), ImVec2(origin.x + centerHalf, origin.y + centerHalf), centerColor, 2.0f);
        return;
    }

    ImVec2 xTriA{}, xTriB{}, xTriC{};
    ImVec2 yTriA{}, yTriB{}, yTriC{};
    ImVec2 zTriA{}, zTriB{}, zTriC{};
    BuildArrowhead(origin, xEnd, arrowSize, xTriA, xTriB, xTriC);
    BuildArrowhead(origin, yEnd, arrowSize, yTriA, yTriB, yTriC);
    BuildArrowhead(origin, zEnd, arrowSize, zTriA, zTriB, zTriC);

    const ImVec2 xzA{ origin.x + xDir.x * planeOffset + zDir.x * planeOffset, origin.y + xDir.y * planeOffset + zDir.y * planeOffset };
    const ImVec2 xzB{ xzA.x + xDir.x * planeSize, xzA.y + xDir.y * planeSize };
    const ImVec2 xzC{ xzB.x + zDir.x * planeSize, xzB.y + zDir.y * planeSize };
    const ImVec2 xzD{ xzA.x + zDir.x * planeSize, xzA.y + zDir.y * planeSize };

    const ImVec2 xyA{ origin.x + xDir.x * planeOffset + yDir.x * planeOffset, origin.y + xDir.y * planeOffset + yDir.y * planeOffset };
    const ImVec2 xyB{ xyA.x + xDir.x * planeSize, xyA.y + xDir.y * planeSize };
    const ImVec2 xyC{ xyB.x + yDir.x * planeSize, xyB.y + yDir.y * planeSize };
    const ImVec2 xyD{ xyA.x + yDir.x * planeSize, xyA.y + yDir.y * planeSize };

    const ImVec2 yzA{ origin.x + yDir.x * planeOffset + zDir.x * planeOffset, origin.y + yDir.y * planeOffset + zDir.y * planeOffset };
    const ImVec2 yzB{ yzA.x + yDir.x * planeSize, yzA.y + yDir.y * planeSize };
    const ImVec2 yzC{ yzB.x + zDir.x * planeSize, yzB.y + zDir.y * planeSize };
    const ImVec2 yzD{ yzA.x + zDir.x * planeSize, yzA.y + zDir.y * planeSize };

    hoveredAxis = -1;
    hoveredPlane = -1;
    hoveredCenter = false;
    if (!dragging && viewportHovered && allowInput)
    {
        const ImVec2 mouse = io.MousePos;
        hoveredCenter = Distance(mouse, origin) <= (centerRadius + 4.0f);

        if (!hoveredCenter)
        {
            const bool overXZ = PointInQuad(mouse, xzA, xzB, xzC, xzD);
            const bool overXY = PointInQuad(mouse, xyA, xyB, xyC, xyD);
            const bool overYZ = PointInQuad(mouse, yzA, yzB, yzC, yzD);

            if (overXZ) hoveredPlane = 0;
            else if (overXY) hoveredPlane = 1;
            else if (overYZ) hoveredPlane = 2;

            if (hoveredPlane == -1)
            {
                const float lineThreshold = (std::max)(8.0f, arrowSize * 0.75f);
                const float tipThreshold = arrowSize + 4.0f;

                const float dx = (std::min)(DistancePointToSegment(mouse, origin, xEnd), Distance(mouse, xEnd));
                const float dy = (std::min)(DistancePointToSegment(mouse, origin, yEnd), Distance(mouse, yEnd));
                const float dz = (std::min)(DistancePointToSegment(mouse, origin, zEnd), Distance(mouse, zEnd));

                float best = (std::min)(lineThreshold, tipThreshold);
                if (dx < best) { best = dx; hoveredAxis = 0; }
                if (dy < best) { best = dy; hoveredAxis = 1; }
                if (dz < best) { best = dz; hoveredAxis = 2; }
            }
        }
    }

    if (allowInput && !dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hoveredCenter)
        {
            WorldRay ray{};
            const XMFLOAT3 planePoint = gizmoOriginWorld;
            XMFLOAT3 planeNormal = {
                planePoint.x - camera.position.x,
                planePoint.y - camera.position.y,
                planePoint.z - camera.position.z
            };
            if (fabsf(planeNormal.x) < 1e-6f && fabsf(planeNormal.y) < 1e-6f && fabsf(planeNormal.z) < 1e-6f)
                planeNormal = { 0.0f, 0.0f, 1.0f };

            if (ScreenPointToWorldRay(io.MousePos, camera, sceneMin, sceneSize, ray) && IntersectRayPlane(ray, planePoint, planeNormal, dragStartHit))
            {
                dragging = true;
                activeCenter = true;
                activeAxis = -1;
                activePlane = -1;
                hoveredAxis = -1;
                hoveredPlane = -1;
                dragStartPosition = gizmoOriginWorld;
                dragPlaneNormal = planeNormal;
                dragStartMouse = io.MousePos;
                hasDragStartHit = true;
                captureDragSelection();
            }
        }
        else if (hoveredPlane != -1)
        {
            WorldRay ray{};
            const XMFLOAT3 planePoint = gizmoOriginWorld;
            XMFLOAT3 planeNormal{};
            switch (hoveredPlane)
            {
            case 0: planeNormal = Normalize3(Cross3(axisXWorld, axisZWorld)); break; // XZ
            case 1: planeNormal = Normalize3(Cross3(axisXWorld, axisYWorld)); break; // XY
            case 2: planeNormal = Normalize3(Cross3(axisYWorld, axisZWorld)); break; // YZ
            default: planeNormal = { 0.0f, 1.0f, 0.0f }; break;
            }

            if (ScreenPointToWorldRay(io.MousePos, camera, sceneMin, sceneSize, ray) && IntersectRayPlane(ray, planePoint, planeNormal, dragStartHit))
            {
                dragging = true;
                activePlane = hoveredPlane;
                activeAxis = -1;
                activeCenter = false;
                dragStartPosition = gizmoOriginWorld;
                dragPlaneNormal = planeNormal;
                dragStartMouse = io.MousePos;
                hasDragStartHit = true;
                captureDragSelection();
            }
        }
        else if (hoveredAxis != -1)
        {
            WorldRay ray{};
            const XMFLOAT3 planePoint = gizmoOriginWorld;
            const XMFLOAT3 axis = (hoveredAxis == 0) ? axisXWorld : (hoveredAxis == 1) ? axisYWorld : axisZWorld;
            XMFLOAT3 viewDir{
                planePoint.x - camera.position.x,
                planePoint.y - camera.position.y,
                planePoint.z - camera.position.z
            };
            if (fabsf(viewDir.x) < 1e-6f && fabsf(viewDir.y) < 1e-6f && fabsf(viewDir.z) < 1e-6f)
                viewDir = { 0.0f, 0.0f, 1.0f };
            const XMFLOAT3 planeNormal = ComputeAxisDragPlaneNormal(axis, Normalize3(viewDir));

            dragging = true;
            activeAxis = hoveredAxis;
            activePlane = -1;
            activeCenter = false;
            dragStartPosition = gizmoOriginWorld;
            dragPlaneNormal = planeNormal;
            dragStartMouse = io.MousePos;
            hasDragStartHit = ScreenPointToWorldRay(io.MousePos, camera, sceneMin, sceneSize, ray) &&
                IntersectRayPlane(ray, planePoint, planeNormal, dragStartHit);
            captureDragSelection();
        }
    }

    if (dragging)
    {
        if (allowInput && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            constexpr float speed = 0.01f;
            constexpr float snapStep = 1.0f;
            const bool snapEnabled = io.KeyCtrl;

            XMFLOAT3 targetPosition = dragStartPosition;
            if (activeCenter || activePlane != -1)
            {
                WorldRay ray{};
                XMFLOAT3 hit{};
                if (hasDragStartHit && ScreenPointToWorldRay(io.MousePos, camera, sceneMin, sceneSize, ray) &&
                    IntersectRayPlane(ray, dragStartPosition, dragPlaneNormal, hit))
                {
                    XMFLOAT3 delta{
                        hit.x - dragStartHit.x,
                        hit.y - dragStartHit.y,
                        hit.z - dragStartHit.z
                    };

                    if (activePlane == 0) delta = ProjectOntoBasis(delta, axisXWorld, axisZWorld); // XZ
                    if (activePlane == 1) delta = ProjectOntoBasis(delta, axisXWorld, axisYWorld); // XY
                    if (activePlane == 2) delta = ProjectOntoBasis(delta, axisYWorld, axisZWorld); // YZ

                    targetPosition = {
                        dragStartPosition.x + delta.x,
                        dragStartPosition.y + delta.y,
                        dragStartPosition.z + delta.z
                    };

                    if (snapEnabled)
                    {
                        if (space == Space::World)
                        {
                            if (activeCenter || activePlane == 0 || activePlane == 1) targetPosition.x = SnapValue(targetPosition.x, snapStep);
                            if (activeCenter || activePlane == 0 || activePlane == 2) targetPosition.z = SnapValue(targetPosition.z, snapStep);
                            if (activeCenter || activePlane == 1 || activePlane == 2) targetPosition.y = SnapValue(targetPosition.y, snapStep);
                        }
                        else
                        {
                            XMFLOAT3 offset{
                                targetPosition.x - dragStartPosition.x,
                                targetPosition.y - dragStartPosition.y,
                                targetPosition.z - dragStartPosition.z
                            };

                            float dx = Dot3(offset, axisXWorld);
                            float dy = Dot3(offset, axisYWorld);
                            float dz = Dot3(offset, axisZWorld);

                            if (activeCenter || activePlane == 0 || activePlane == 1) dx = SnapValue(dx, snapStep);
                            if (activeCenter || activePlane == 1 || activePlane == 2) dy = SnapValue(dy, snapStep);
                            if (activeCenter || activePlane == 0 || activePlane == 2) dz = SnapValue(dz, snapStep);

                            targetPosition = {
                                dragStartPosition.x + axisXWorld.x * dx + axisYWorld.x * dy + axisZWorld.x * dz,
                                dragStartPosition.y + axisXWorld.y * dx + axisYWorld.y * dy + axisZWorld.y * dz,
                                dragStartPosition.z + axisXWorld.z * dx + axisYWorld.z * dy + axisZWorld.z * dz
                            };
                        }
                    }
                }
            }
            else
            {
                WorldRay ray{};
                XMFLOAT3 hit{};
                if (hasDragStartHit && ScreenPointToWorldRay(io.MousePos, camera, sceneMin, sceneSize, ray) &&
                    IntersectRayPlane(ray, dragStartPosition, dragPlaneNormal, hit))
                {
                    const XMFLOAT3 axis = (activeAxis == 0) ? axisXWorld : (activeAxis == 1) ? axisYWorld : axisZWorld;
                    const ImVec2 axisDir2D = (activeAxis == 0) ? xDir : (activeAxis == 1) ? yDir : zDir;
                    const ImVec2 mouseDelta2D{ io.MousePos.x - dragStartMouse.x, io.MousePos.y - dragStartMouse.y };
                    XMFLOAT3 delta{
                        hit.x - dragStartHit.x,
                        hit.y - dragStartHit.y,
                        hit.z - dragStartHit.z
                    };

                    float axisDelta = fabsf(Dot3(delta, axis));
                    if ((mouseDelta2D.x * axisDir2D.x + mouseDelta2D.y * axisDir2D.y) < 0.0f)
                        axisDelta = -axisDelta;

                    if (snapEnabled)
                        axisDelta = SnapValue(axisDelta, snapStep);
                    targetPosition = AddScaled(dragStartPosition, axis, axisDelta);
                }
            }

            const XMFLOAT3 groupDelta{
                targetPosition.x - dragStartPosition.x,
                targetPosition.y - dragStartPosition.y,
                targetPosition.z - dragStartPosition.z
            };
            applyGroupTranslationDelta(groupDelta);
        }
        else
        {
            dragging = false;
            activeAxis = -1;
            activePlane = -1;
            activeCenter = false;
            hasDragStartHit = false;
        }
    }

    const float xzVisibility = PlaneVisibility(camera, XMFLOAT3{ 0.0f, 1.0f, 0.0f });
    const float xyVisibility = PlaneVisibility(camera, XMFLOAT3{ 0.0f, 0.0f, 1.0f });
    const float yzVisibility = PlaneVisibility(camera, XMFLOAT3{ 1.0f, 0.0f, 0.0f });

    const ImU32 xzColor = WithAlpha(PlaneColor(0, activePlane == 0, hoveredPlane == 0), xzVisibility);
    const ImU32 xyColor = WithAlpha(PlaneColor(1, activePlane == 1, hoveredPlane == 1), xyVisibility);
    const ImU32 yzColor = WithAlpha(PlaneColor(2, activePlane == 2, hoveredPlane == 2), yzVisibility);

    drawList->AddQuadFilled(xzA, xzB, xzC, xzD, xzColor);
    drawList->AddQuadFilled(xyA, xyB, xyC, xyD, xyColor);
    drawList->AddQuadFilled(yzA, yzB, yzC, yzD, yzColor);

    drawList->AddLine(origin, xEnd, AxisColor(0, activeAxis == 0, hoveredAxis == 0), 3.0f);
    drawList->AddLine(origin, yEnd, AxisColor(1, activeAxis == 1, hoveredAxis == 1), 3.0f);
    drawList->AddLine(origin, zEnd, AxisColor(2, activeAxis == 2, hoveredAxis == 2), 3.0f);
    drawList->AddTriangleFilled(xTriA, xTriB, xTriC, AxisColor(0, activeAxis == 0, hoveredAxis == 0));
    drawList->AddTriangleFilled(yTriA, yTriB, yTriC, AxisColor(1, activeAxis == 1, hoveredAxis == 1));
    drawList->AddTriangleFilled(zTriA, zTriB, zTriC, AxisColor(2, activeAxis == 2, hoveredAxis == 2));

    const ImU32 centerColor = activeCenter ? IM_COL32(255, 255, 255, 230) : (hoveredCenter ? IM_COL32(255, 220, 64, 220) : IM_COL32(245, 245, 245, 220));
    drawList->AddCircleFilled(origin, centerRadius, centerColor);
}
