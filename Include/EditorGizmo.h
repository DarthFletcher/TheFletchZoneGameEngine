#pragma once

#include <DirectXMath.h>
#include "imgui.h"

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

    void SetMode(Mode m);
    Mode GetMode() const;
    bool IsDragging() const { return dragging; }

    void Update(
        SceneInstance* instance,
        const CameraData& camera,
        ImDrawList* drawList,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        bool viewportHovered,
        bool allowInput);

private:
    Mode mode = Mode::Translate;
    bool dragging = false;
    int hoveredAxis = -1;
    int activeAxis = -1;
    DirectX::XMFLOAT3 dragStartPosition{};
    ImVec2 dragStartMouse{};
};
