#include "EditorGizmo.h"

#include "CameraData.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
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
        if (ndcZ < 0.0f)
            return false;

        outScreen.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
        outScreen.y = sceneMin.y + (-ndcY * 0.5f + 0.5f) * sceneSize.y;
        return true;
    }

    static float ComputeGizmoAxisLength(const XMFLOAT3& objectPos, const CameraData& camera)
    {
        const XMVECTOR obj = XMVectorSet(objectPos.x, objectPos.y, objectPos.z, 1.0f);
        const XMVECTOR cam = XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 1.0f);
        const float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(obj, cam)));
        const float scale = std::clamp(distance * 0.15f, 0.5f, 2.0f);
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
}

void EditorGizmo::SetMode(Mode m)
{
    if (mode == m)
        return;

    mode = m;
    dragging = false;
    hoveredAxis = -1;
    activeAxis = -1;
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
    bool allowInput)
{
    if (!instance || !drawList)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (allowInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) SetMode(Mode::Translate);
        if (ImGui::IsKeyPressed(ImGuiKey_E)) SetMode(Mode::Rotate);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) SetMode(Mode::Scale);
    }

    if (mode != Mode::Translate)
        return;

    ImVec2 origin{};
    if (!ProjectWorldToScreen(instance->position, camera, sceneMin, sceneSize, origin))
        return;

    const float axisLength = ComputeGizmoAxisLength(instance->position, camera);
    const ImVec2 xEnd{ origin.x + axisLength, origin.y };
    const ImVec2 yEnd{ origin.x, origin.y - axisLength };
    const ImVec2 zEnd{ origin.x + axisLength * 0.7f, origin.y - axisLength * 0.7f };

    hoveredAxis = -1;
    if (!dragging && viewportHovered)
    {
        const ImVec2 mouse = io.MousePos;
        constexpr float threshold = 8.0f;

        const float dx = DistancePointToSegment(mouse, origin, xEnd);
        const float dy = DistancePointToSegment(mouse, origin, yEnd);
        const float dz = DistancePointToSegment(mouse, origin, zEnd);

        float best = threshold;
        if (dx < best) { best = dx; hoveredAxis = 0; }
        if (dy < best) { best = dy; hoveredAxis = 1; }
        if (dz < best) { best = dz; hoveredAxis = 2; }
    }

    if (allowInput && !dragging && hoveredAxis != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        dragging = true;
        activeAxis = hoveredAxis;
        dragStartPosition = instance->position;
        dragStartMouse = io.MousePos;
    }

    if (dragging)
    {
        if (allowInput && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImVec2 delta{ io.MousePos.x - dragStartMouse.x, io.MousePos.y - dragStartMouse.y };
            constexpr float speed = 0.01f;
            constexpr float snapStep = 1.0f;
            const bool snapEnabled = io.KeyCtrl;

            instance->position = dragStartPosition;
            switch (activeAxis)
            {
            case 0:
                instance->position.x += delta.x * speed;
                if (snapEnabled) instance->position.x = SnapValue(instance->position.x, snapStep);
                break;
            case 1:
                instance->position.y -= delta.y * speed;
                if (snapEnabled) instance->position.y = SnapValue(instance->position.y, snapStep);
                break;
            case 2:
                instance->position.z += delta.x * speed;
                if (snapEnabled) instance->position.z = SnapValue(instance->position.z, snapStep);
                break;
            default:
                break;
            }
        }
        else
        {
            dragging = false;
            activeAxis = -1;
        }
    }

    drawList->AddCircleFilled(origin, 5.0f, IM_COL32(245, 245, 245, 220));
    drawList->AddLine(origin, xEnd, AxisColor(0, activeAxis == 0, hoveredAxis == 0), 3.0f);
    drawList->AddLine(origin, yEnd, AxisColor(1, activeAxis == 1, hoveredAxis == 1), 3.0f);
    drawList->AddLine(origin, zEnd, AxisColor(2, activeAxis == 2, hoveredAxis == 2), 3.0f);
}
