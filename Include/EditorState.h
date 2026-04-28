#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <string>

#include "EditorCommon.h"
#include "BlackFlameAI.h"
#include "EditorCommands.h"

// Forward declare to avoid heavy includes.
// Your Entity class exists already in your project.
class Entity;

// =====================================================
// Editor interaction enums
// =====================================================
enum class CameraNavMode : uint8_t
{
    Unity_AltMouse = 0,   // Alt + LMB/MMB/RMB
    Unity_WASDMouse,      // RMB look + WASD/QE fly, Alt + mouse orbit
    Blender_MMB,          // MMB orbit/pan
    TFZ_RMB,              // RMB orbit + MMB pan (TFZ style)
    Laptop_Friendly       // Alt + LMB orbit, Shift + LMB pan, Alt + RMB dolly
};

enum class GridMode : uint8_t
{
    Infinite_CameraPivot = 0,
    Fixed_WorldOrigin
};

enum class GizmoPivotMode : uint8_t
{
    Pivot = 0,
    Center
};

enum class SceneDebugViewMode : uint32_t
{
    Lit = 0,
    Albedo,
    Normals,
    Metallic,
    Roughness,
    LightingOnly,
    AmbientOnly,
    SpecularOnly,
    SelectionMask,
    Depth,
    LinearDepth,
    WorldPosition,
    UVs,
    LightDirection
};

// =====================================================
// Selection
// =====================================================
struct EditorSelection
{
    Entity* entity = nullptr;

    // Always keep a "selection position" even if entity is null.
    // This is useful for spawning gizmos / debug markers.
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };

    bool hasSelection() const { return entity != nullptr; }

    void Clear()
    {
        entity = nullptr;
        position = { 0.0f, 0.0f, 0.0f };
    }
};

// =====================================================
// Gizmo interaction (Translate Gizmo foundation)
// =====================================================
struct GizmoInteraction
{
    // Which axis is currently hovered (hot)
    // -1 = none, 0 = X, 1 = Y, 2 = Z
    int hotAxis = -1;

    // Which axis is being dragged (active)
    // -1 = none, 0 = X, 1 = Y, 2 = Z
    int activeAxis = -1;

    bool dragging = false;

    // Used for ray-plane drag logic
    DirectX::XMFLOAT3 dragStartHit = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 dragStartPos = { 0.0f, 0.0f, 0.0f };

    // Some drag systems need a "valid hit start"
    bool hasDragStartHit = false;

    void Reset()
    {
        hotAxis = -1;
        activeAxis = -1;
        dragging = false;
        hasDragStartHit = false;
        dragStartHit = { 0.0f, 0.0f, 0.0f };
        dragStartPos = { 0.0f, 0.0f, 0.0f };
    }
};

// =====================================================
// TFZ Grab Mode (optional for later, safe to include now)
// =====================================================
struct TFZGrabState
{
    bool active = false;      // currently grabbing
    bool canceled = false;

    // Axis lock: -1 none, 0 X, 1 Y, 2 Z
    int lockAxis = -1;

    // Start state for cancel/commit
    DirectX::XMFLOAT3 startHit = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 startPos = { 0.0f, 0.0f, 0.0f };
};

struct SceneHistoryEntry
{
    std::string sceneSnapshot;
    uint32_t activeSelectedInstanceId = 0;
    std::vector<uint32_t> selectedInstanceIds;
};

enum class EditorUndoEntryKind : uint8_t
{
    Snapshot = 0,
    Command,
};

// =====================================================
// Editor interaction timing
// =====================================================
inline constexpr float kEditorControlTickSeconds = 1.0f / 60.0f;

// =====================================================
// Main Editor State
// =====================================================
struct EditorState
{
    // Modes
    CameraNavMode cameraNavMode = CameraNavMode::Unity_AltMouse;
    GridMode gridMode = GridMode::Infinite_CameraPivot;
    ViewMode viewMode = ViewMode::Mode3D;
    GizmoPivotMode gizmoPivotMode = GizmoPivotMode::Center;

    BlackFlameAI blackFlameAI;
    SceneEventDispatcher sceneEvents;
    BlackFlameAccessLevel currentBlackFlameAccess = BlackFlameAccessLevel::Admin;

    // Camera feel
    bool invertLookX = false;
    bool invertLookY = false;
    bool smoothLook = true;
    bool enableGamepadCamera = true;
    bool enableSceneViewCulling = false;
    float lookSmoothing = 0.22f;
    float flyLookSpeed = 0.0018f;
    float orbitLookSpeed = 0.0055f;
    float flyMoveSpeed = 8.0f;
    float gamepadStickDeadzone = 0.18f;
    float gamepadLookSensitivity = 1.0f;
    float gamepadMoveSensitivity = 1.0f;
    float gamepadZoomSensitivity = 1.0f;
    bool gridFadeEnabled = true;
    bool gridMajorLinesEnabled = true;
    float gridFadeDistance = 40.0f;
    float gridVisibility = 1.0f;
    float gridMajorLineBoost = 1.35f;
    float gridAxisEmphasis = 1.25f;
    float gridExtent = 10.0f;
    int gridDivisions = 20;
    SceneDebugViewMode sceneDebugViewMode = SceneDebugViewMode::Lit;

    // Core interaction state
    EditorSelection selection;
    GizmoInteraction gizmo;
    int focusedMaterialIndex = -1;

    // TFZ mode (optional)
    TFZGrabState grab;

    // Debug toggles (optional)
    bool showDiagnostics = true;
    bool showLogViewer = true;
    bool showDebugOverlay = true;

    std::vector<SceneHistoryEntry> undoSceneSnapshots;
    std::vector<SceneHistoryEntry> redoSceneSnapshots;
    std::vector<EditorUndoEntryKind> undoEntryKinds;
    std::vector<EditorUndoEntryKind> redoEntryKinds;
    EditorCommandManager commandManager;
    std::string currentProjectName;
    std::string currentProjectPath;
    std::string currentProjectRoot;
    std::string startupScenePath;
    std::string lastOpenedScenePath;

    void ResetAllInteraction()
    {
        gizmo.Reset();
        grab = TFZGrabState{};
    }

    void ClearHistory()
    {
        undoSceneSnapshots.clear();
        redoSceneSnapshots.clear();
        undoEntryKinds.clear();
        redoEntryKinds.clear();
        commandManager.Clear();
    }
};
