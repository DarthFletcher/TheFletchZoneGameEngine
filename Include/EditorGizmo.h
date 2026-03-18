#pragma once

#include <DirectXMath.h>
#include "imgui.h"
#include <vector>
#include <utility>

struct SceneInstance;
struct CameraData;

class EditorGizmo
{
public:
    enum class Mode
    {
        Translate,
        Rotate,
        Scale
    };

    enum class Space
    {
        World,
        Local
    };

    void SetMode(Mode m);
    Mode GetMode() const;
    void SetSpace(Space s) { space = s; }
    Space GetSpace() const { return space; }
    void ToggleSpace() { space = (space == Space::World) ? Space::Local : Space::World; }
    bool IsDragging() const { return dragging; }
    bool HasHoveredHandle() const { return hoveredAxis != -1 || hoveredPlane != -1 || hoveredCenter; }
    bool HasActiveRotationFeedback() const { return mode == Mode::Rotate && activeAxis != -1; }
    int GetActiveAxis() const { return activeAxis; }
    float GetActiveRotationDegrees() const { return activeRotationDegrees; }

    void Update(
        SceneInstance* instance,
        const CameraData& camera,
        ImDrawList* drawList,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        bool viewportHovered,
        bool allowInput,
        bool useSelectionCenter);

private:
    Mode mode = Mode::Translate;
    Space space = Space::World;
    bool dragging = false;
    int hoveredAxis = -1;
    int activeAxis = -1;
    int hoveredPlane = -1;
    int activePlane = -1;
    bool hoveredCenter = false;
    bool activeCenter = false;
    DirectX::XMFLOAT3 dragStartPosition{};
    DirectX::XMFLOAT3 dragStartRotation{};
    DirectX::XMFLOAT3 dragStartScale{ 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 dragStartHit{};
    DirectX::XMFLOAT3 dragPlaneNormal{};
    bool hasDragStartHit = false;
    float dragStartAngle = 0.0f;
    float activeRotationDegrees = 0.0f;
    ImVec2 dragStartMouse{};
    ImVec2 dragStartTangent{};
    ImVec2 dragStartRingPoint{};
    std::vector<std::pair<uint32_t, DirectX::XMFLOAT3>> dragStartSelectionPositions;
    std::vector<std::pair<uint32_t, DirectX::XMFLOAT3>> dragStartSelectionRotations;
    std::vector<std::pair<uint32_t, DirectX::XMFLOAT3>> dragStartSelectionScales;
};
