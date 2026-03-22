#include "EditorPanels.h"

#include "imgui.h"
#include "EditorCommon.h"
#include "Graphics.h"
#include "Logger.h"
#include "EditorState.h"
#include "Engine.h"
#include "Scene.h"
#include "CameraData.h"
#include "MaterialManager.h"
#include "EditorGizmo.h"
#include "UI.h"
#include "EditorIcons.h"
#include "ImGuiUtils.h"

#include <format>
#include <string>
#include <chrono>
#include <cstring>
#include <cfloat>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace EditorPanels
{
    enum class AssetCategory
    {
        All,
        Primitives,
        Prefabs,
    };

    static constexpr size_t kMaxUndoSnapshots = 64;
    static constexpr const char* kSceneCreateCubePayload = "SCENE_CREATE_CUBE";
    static constexpr const char* kSceneCreatePrefabPayload = "SCENE_CREATE_PREFAB";
    static EditorGizmo g_EditorGizmo;
    static bool g_WasGizmoDragging = false;
    static AssetCategory g_SelectedAssetCategory = AssetCategory::All;
    static std::string g_SelectedAssetId;
    static char g_AssetSearch[128] = {};
    static std::unordered_map<std::string, ImTextureID> g_AssetThumbnailCache;
    static int GetAssetTileColumnCount(float availableWidth, float tileWidth, float spacing);
    static ImTextureID GetAssetThumbnailTexture(const std::string& assetId, const std::string& fallbackTexturePath);
    static bool DrawAssetTile(const char* id, const char* title, const char* icon, const char* payloadType, const void* payloadData, size_t payloadSize, bool selected, ImTextureID thumbnailTexture);
    static void DrawAssetSectionHeader(const char* label);
    static std::vector<std::filesystem::path> EnumerateTextureAssets();

    static const char* GetAssetCategoryLabel(AssetCategory category)
    {
        switch (category)
        {
        case AssetCategory::Primitives: return "Primitives";
        case AssetCategory::Prefabs: return "Prefabs";
        case AssetCategory::All:
        default: return "All";
        }
    }

    static SceneHistoryEntry MakeHistoryEntry()
    {
        SceneHistoryEntry entry{};
        entry.sceneSnapshot = Scene::SerializeToString();
        entry.activeSelectedInstanceId = Scene::GetSelectedInstanceId();
        entry.selectedInstanceIds = Scene::GetSelectedInstanceIds();
        return entry;
    }

    static std::string SanitizeAssetName(std::string name)
    {
        for (char& c : name)
        {
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
                c = '_';
        }
        if (name.empty())
            name = "Prefab";
        return name;
    }

    static std::vector<std::filesystem::path> EnumeratePrefabAssets()
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path prefabDir = std::filesystem::path("Assets") / "Prefabs";
        if (!std::filesystem::exists(prefabDir))
            return result;

        for (const auto& entry : std::filesystem::directory_iterator(prefabDir))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string filename = entry.path().filename().string();
            if (filename.size() < 12 || filename.find(".prefab.json") == std::string::npos)
                continue;
            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    static bool TryGetCurrentSelectionAnchor(const EditorState& editor, DirectX::XMFLOAT3& outAnchor);

    static void PushUndoSnapshot(EditorState& editor)
    {
        const SceneHistoryEntry snapshot = MakeHistoryEntry();
        if (!editor.undoSceneSnapshots.empty() && editor.undoSceneSnapshots.back().sceneSnapshot == snapshot.sceneSnapshot &&
            editor.undoSceneSnapshots.back().activeSelectedInstanceId == snapshot.activeSelectedInstanceId &&
            editor.undoSceneSnapshots.back().selectedInstanceIds == snapshot.selectedInstanceIds)
            return;

        editor.undoSceneSnapshots.push_back(snapshot);
        if (editor.undoSceneSnapshots.size() > kMaxUndoSnapshots)
            editor.undoSceneSnapshots.erase(editor.undoSceneSnapshots.begin());
        editor.redoSceneSnapshots.clear();
    }

    static bool PerformUndo(EditorState& editor)
    {
        if (editor.undoSceneSnapshots.empty())
            return false;

        editor.redoSceneSnapshots.push_back(MakeHistoryEntry());
        const SceneHistoryEntry snapshot = editor.undoSceneSnapshots.back();
        editor.undoSceneSnapshots.pop_back();
        if (!Scene::LoadFromString(snapshot.sceneSnapshot))
            return false;
        Scene::RestoreSelectionState(snapshot.activeSelectedInstanceId, snapshot.selectedInstanceIds);
        return true;
    }

    static bool PerformRedo(EditorState& editor)
    {
        if (editor.redoSceneSnapshots.empty())
            return false;

        editor.undoSceneSnapshots.push_back(MakeHistoryEntry());
        const SceneHistoryEntry snapshot = editor.redoSceneSnapshots.back();
        editor.redoSceneSnapshots.pop_back();
        if (!Scene::LoadFromString(snapshot.sceneSnapshot))
            return false;
        Scene::RestoreSelectionState(snapshot.activeSelectedInstanceId, snapshot.selectedInstanceIds);
        return true;
    }

    struct EditorWorldRay
    {
        DirectX::XMFLOAT3 origin{};
        DirectX::XMFLOAT3 direction{};
    };

    static bool ScreenPointToSceneWorldRay(ImVec2 mousePos, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, EditorWorldRay& outRay)
    {
        using namespace DirectX;
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

    static bool IntersectRayWithPlaneY(const EditorWorldRay& ray, float planeY, DirectX::XMFLOAT3& outHit)
    {
        constexpr float kEpsilon = 1e-6f;
        if (fabsf(ray.direction.y) < kEpsilon)
            return false;
        const float t = (planeY - ray.origin.y) / ray.direction.y;
        if (t < 0.0f)
            return false;
        outHit = { ray.origin.x + ray.direction.x * t, planeY, ray.origin.z + ray.direction.z * t };
        return true;
    }

    static DirectX::XMFLOAT3 ComputeSceneDropSpawnPosition(const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2 mousePos)
    {
        EditorWorldRay ray{};
        if (ScreenPointToSceneWorldRay(mousePos, camera, sceneMin, sceneSize, ray))
        {
            DirectX::XMFLOAT3 hit{};
            if (IntersectRayWithPlaneY(ray, 0.5f, hit))
                return hit;
            return { ray.origin.x + ray.direction.x * 5.0f, (std::max)(0.5f, ray.origin.y + ray.direction.y * 5.0f), ray.origin.z + ray.direction.z * 5.0f };
        }
        return camera.position;
    }

    static float ComputeWorldMatrixMaxBasisScale(const DirectX::XMFLOAT4X4& world)
    {
        const float sx = std::sqrt(world._11 * world._11 + world._12 * world._12 + world._13 * world._13);
        const float sy = std::sqrt(world._21 * world._21 + world._22 * world._22 + world._23 * world._23);
        const float sz = std::sqrt(world._31 * world._31 + world._32 * world._32 + world._33 * world._33);
        return (std::max)(sx, (std::max)(sy, sz));
    }

    static float ComputeSelectionFocusDistance(const EditorState& editor)
    {
        DirectX::XMFLOAT3 anchor{};
        if (!TryGetCurrentSelectionAnchor(editor, anchor))
            return 6.0f;

        float maxExtent = 0.0f;
        for (uint32_t id : Scene::GetSelectedInstanceIds())
        {
            DirectX::XMFLOAT4X4 world{};
            if (!Scene::TryGetInstanceWorldMatrix(id, world))
                continue;
            const DirectX::XMFLOAT3 center{ world._41, world._42, world._43 };
            const float dx = center.x - anchor.x;
            const float dy = center.y - anchor.y;
            const float dz = center.z - anchor.z;
            const float distanceToCenter = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float radius = 0.8660254f * ComputeWorldMatrixMaxBasisScale(world);
            maxExtent = (std::max)(maxExtent, distanceToCenter + radius);
        }

        if (maxExtent <= 0.0f)
            maxExtent = 1.0f;
        return (std::max)(4.5f, maxExtent * 3.0f);
    }

    static bool ProjectWorldToSceneViewport(
        const DirectX::XMFLOAT3& worldPos,
        const CameraData& camera,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        ImVec2& outScreen)
    {
        using namespace DirectX;

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

    static const char* ViewModeLabel(ViewMode m)
    {
        switch (m)
        {
        case ViewMode::Mode2D: return "2D";
        case ViewMode::Mode3D:
        default: return "3D";
        }
    }

    static std::chrono::steady_clock::time_point g_ScenePanelDiag_Last = std::chrono::steady_clock::now();
    static bool ScenePanelDiag_ShouldLog(std::chrono::milliseconds interval = std::chrono::milliseconds(1000))
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - g_ScenePanelDiag_Last >= interval)
        {
            g_ScenePanelDiag_Last = now;
            return true;
        }
        return false;
    }

    static bool IsSceneCameraNavigating(const EditorState& editor)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool alt = io.KeyAlt;
        const bool ctrl = io.KeyCtrl;
        const bool shift = io.KeyShift;

        switch (editor.cameraNavMode)
        {
        case CameraNavMode::Unity_AltMouse:
            return alt && (lmbDown || mmbDown || rmbDown);
        case CameraNavMode::Blender_MMB:
            return mmbDown && (!shift || shift || ctrl);
        case CameraNavMode::TFZ_RMB:
        default:
            return rmbDown || mmbDown;
        }
    }

    static bool TryGetCurrentSelectionAnchor(const EditorState& editor, DirectX::XMFLOAT3& outAnchor)
    {
        if (editor.gizmoPivotMode == GizmoPivotMode::Center && Scene::TryGetSelectionCenter(outAnchor))
            return true;

        if (SceneInstance* selected = Scene::GetSelectedInstance())
        {
            outAnchor = Scene::GetInstanceWorldPosition(selected->instanceId);
            return true;
        }

        return Scene::TryGetSelectionCenter(outAnchor);
    }

    static bool TryGetSceneInstanceScreenRect(const SceneInstance& instance, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2& outMin, ImVec2& outMax)
    {
        using namespace DirectX;

        const XMFLOAT3 localCorners[8] =
        {
            { -0.5f, -0.5f, -0.5f }, { +0.5f, -0.5f, -0.5f },
            { -0.5f, +0.5f, -0.5f }, { +0.5f, +0.5f, -0.5f },
            { -0.5f, -0.5f, +0.5f }, { +0.5f, -0.5f, +0.5f },
            { -0.5f, +0.5f, +0.5f }, { +0.5f, +0.5f, +0.5f },
        };

        DirectX::XMFLOAT4X4 worldF{};
        if (!Scene::TryGetInstanceWorldMatrix(instance.instanceId, worldF))
            return false;
        const XMMATRIX world = XMLoadFloat4x4(&worldF);

        bool anyProjected = false;
        outMin = ImVec2(FLT_MAX, FLT_MAX);
        outMax = ImVec2(-FLT_MAX, -FLT_MAX);

        for (const XMFLOAT3& local : localCorners)
        {
            const XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&local), world);
            XMFLOAT3 worldPt{};
            XMStoreFloat3(&worldPt, p);

            ImVec2 screenPt{};
            if (!ProjectWorldToSceneViewport(worldPt, camera, sceneMin, sceneSize, screenPt))
                continue;

            anyProjected = true;
            outMin.x = (std::min)(outMin.x, screenPt.x);
            outMin.y = (std::min)(outMin.y, screenPt.y);
            outMax.x = (std::max)(outMax.x, screenPt.x);
            outMax.y = (std::max)(outMax.y, screenPt.y);
        }

        return anyProjected;
    }

    static EditorPanel g_scene{
        "Scene",
        true,
        []()
        {
            auto& panel = Scene();
            if (!panel.open)
            {
                Graphics::GetInstance().SetSceneViewportCameraInputState(false, false, false);
                return;
            }

            auto& gfx = Graphics::GetInstance();
            auto& sceneCamera = gfx.GetSceneCamera();

            if (ImGui::Begin(panel.name, &panel.open))
            {
                // Header view mode dropdown (kept lightweight; uses existing menu routing rules).
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("View");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::BeginCombo("##SceneViewMode", ViewModeLabel(gfx.GetViewMode())))
                {
                    const bool sel3d = (gfx.GetViewMode() == ViewMode::Mode3D);
                    const bool sel2d = (gfx.GetViewMode() == ViewMode::Mode2D);
                    if (ImGui::Selectable("3D", sel3d))
                    {
                        gfx.SetViewMode(ViewMode::Mode3D);
                        sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View3D);
                    }
                    if (ImGui::Selectable("2D", sel2d))
                    {
                        gfx.SetViewMode(ViewMode::Mode2D);
                        sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View2D);
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                const bool projectionToggleEnabled = !sceneCamera.Is2DMode();
                if (!projectionToggleEnabled)
                    ImGui::BeginDisabled();
                if (ImGui::SmallButton(sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective ? "Persp##Proj" : "Ortho##Proj"))
                    sceneCamera.ToggleProjectionMode();
                if (!projectionToggleEnabled)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::SmallButton("Front##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetFrontView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Right##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetRightView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Top##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetTopView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Perspective##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.ResetToDefaultView();
                }

                ImVec2 size = ImGui::GetContentRegionAvail();

                // Quantize to integer pixels and stabilize against 1px docking/padding jitter.
                const int rawW = (int)std::floor(size.x);
                const int rawH = (int)std::floor(size.y);

                static int s_lastRequestedW = 0;
                static int s_lastRequestedH = 0;

                int reqW = (rawW > 1) ? rawW : 1;
                int reqH = (rawH > 1) ? rawH : 1;

                if (s_lastRequestedW != 0 && s_lastRequestedH != 0)
                {
                    if (std::abs(reqW - s_lastRequestedW) <= 1) reqW = s_lastRequestedW;
                    if (std::abs(reqH - s_lastRequestedH) <= 1) reqH = s_lastRequestedH;
                }

                const UINT w = (UINT)reqW;
                const UINT h = (UINT)reqH;

                if ((int)w != s_lastRequestedW || (int)h != s_lastRequestedH)
                {
                    s_lastRequestedW = (int)w;
                    s_lastRequestedH = (int)h;
                    gfx.RequestSceneRenderTargetResize(w, h, ResizeSource::User);
                }

                ImTextureID tex = gfx.GetSceneImGuiTextureID();
                const bool shouldLogSceneDiag = ScenePanelDiag_ShouldLog();
                if (shouldLogSceneDiag)
                {
                    Logger::Log(LogLevel::Debug, std::format(
                        "[ScenePanel] SceneRT TexID=0x{:X} ViewportSize=({:.1f},{:.1f}) RTSize=({:.0f},{:.0f})",
                        (UINT64)tex,
                        size.x, size.y,
                        gfx.GetSceneRenderTargetSize().x, gfx.GetSceneRenderTargetSize().y), "[Editor]");
                }
                if (tex)
                {
                    // Scene render target displayed in native orientation so overlay/gizmo projection matches it.
                    ImGui::Image(tex, size, ImVec2(0, 0), ImVec2(1, 1));

                    const bool hovered = ImGui::IsItemHovered();
                    const bool viewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                    // Cache scene image rect for picking
                    const ImVec2 sceneMin = ImGui::GetItemRectMin();
                    const ImVec2 overlayPos = { sceneMin.x + 10.0f, sceneMin.y + 10.0f };

                    extern Engine* g_engineInstance;
                    EditorState& editor = g_engineInstance->GetEditorState();

                    if (ImGui::BeginDragDropTarget())
                    {
                        CameraData dropCamera{};
                        DirectX::XMFLOAT3 spawnPos{ 0.0f, 0.5f, 0.0f };
                        if (Scene::TryGetLastRenderCameraData(dropCamera))
                            spawnPos = ComputeSceneDropSpawnPosition(dropCamera, sceneMin, size, ImGui::GetMousePos());

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateCubePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCube(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreatePrefabPayload))
                        {
                            const char* prefabPath = static_cast<const char*>(payload->Data);
                            if (prefabPath && *prefabPath)
                            {
                                PushUndoSnapshot(editor);
                                Scene::InstantiatePrefab(prefabPath, spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    const char* modeText = "Translate";
                    switch (g_EditorGizmo.GetMode())
                    {
                    case EditorGizmo::Mode::Rotate: modeText = "Rotate"; break;
                    case EditorGizmo::Mode::Scale: modeText = "Scale"; break;
                    case EditorGizmo::Mode::Translate:
                    default: modeText = "Translate"; break;
                    }

                    const char* spaceText = (g_EditorGizmo.GetSpace() == EditorGizmo::Space::World) ? "World" : "Local";
                    const char* pivotText = (editor.gizmoPivotMode == GizmoPivotMode::Center) ? "Center" : "Pivot";
                    const char* projectionText = (sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective) ? "Perspective" : "Orthographic";
                    const char* viewText = sceneCamera.Is2DMode() ? "2D" : "3D";

                    const bool freelooking = gfx.GetSceneCamera().IsFreelooking();
                    char gizmoHud[320] = {};
                    if (g_EditorGizmo.HasActiveRotationFeedback())
                    {
                        const char axisChar = (g_EditorGizmo.GetActiveAxis() == 0) ? 'X' : (g_EditorGizmo.GetActiveAxis() == 1) ? 'Y' : 'Z';
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nPivot: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s\nRotate %c: %.1f deg",
                            modeText,
                            spaceText,
                            pivotText,
                            ImGui::GetIO().KeyCtrl ? "ON" : "OFF",
                            freelooking ? "Fly" : (sceneCamera.Is2DMode() ? "Pan" : "Idle"),
                            projectionText,
                            viewText,
                            axisChar,
                            g_EditorGizmo.GetActiveRotationDegrees());
                    }
                    else
                    {
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nPivot: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s",
                            modeText,
                            spaceText,
                            pivotText,
                            ImGui::GetIO().KeyCtrl ? "ON" : "OFF",
                            freelooking ? "Fly" : (sceneCamera.Is2DMode() ? "Pan" : "Idle"),
                            projectionText,
                            viewText);
                    }

                    const ImVec2 hudSize = ImGui::CalcTextSize(gizmoHud, nullptr, false);
                    ImDrawList* overlayDraw = ImGui::GetWindowDrawList();
                    overlayDraw->AddRectFilled(
                        ImVec2(overlayPos.x - 6.0f, overlayPos.y - 6.0f),
                        ImVec2(overlayPos.x + hudSize.x + 6.0f, overlayPos.y + hudSize.y + 10.0f),
                        IM_COL32(0, 0, 0, 160),
                        4.0f);
                    overlayDraw->AddText(overlayPos, IM_COL32(255, 255, 255, 255), gizmoHud);

                    {
                        DirectX::XMFLOAT3 anchor{};
                        if (TryGetCurrentSelectionAnchor(editor, anchor))
                        {
                            CameraData pivotCamera{};
                            ImVec2 pivotScreen{};
                            if (Scene::TryGetLastRenderCameraData(pivotCamera) &&
                                ProjectWorldToSceneViewport(anchor, pivotCamera, sceneMin, size, pivotScreen))
                            {
                                overlayDraw->AddCircleFilled(pivotScreen, 4.0f, IM_COL32(255, 230, 80, 220));
                                overlayDraw->AddCircle(pivotScreen, 7.0f, IM_COL32(255, 230, 80, 180), 16, 1.5f);
                            }

                            const auto& instances = Scene::GetInstances();
                            for (const SceneInstance& sceneInstance : instances)
                            {
                                if (!sceneInstance.visible)
                                    continue;

                                const bool isSelected = Scene::IsInstanceSelected(sceneInstance.instanceId);
                                const bool isHovered = (sceneInstance.instanceId == Scene::GetHoveredInstanceId());
                                if (!isSelected && !isHovered)
                                    continue;

                                ImVec2 rectMin{}, rectMax{};
                                if (!TryGetSceneInstanceScreenRect(sceneInstance, pivotCamera, sceneMin, size, rectMin, rectMax))
                                    continue;

                                const ImU32 outlineColor = isSelected ? IM_COL32(255, 140, 60, 220) : IM_COL32(255, 230, 80, 220);
                                const float pad = isSelected ? 3.0f : 2.0f;
                                overlayDraw->AddRect(
                                    ImVec2(rectMin.x - pad, rectMin.y - pad),
                                    ImVec2(rectMax.x + pad, rectMax.y + pad),
                                    outlineColor,
                                    2.0f,
                                    0,
                                    isSelected ? 2.5f : 1.5f);
                            }
                        }
                    }

                    ImGui::SetCursorScreenPos(ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f));
                    if (ImGui::SmallButton(g_EditorGizmo.GetSpace() == EditorGizmo::Space::World ? "World##GizmoSpace" : "Local##GizmoSpace"))
                        g_EditorGizmo.ToggleSpace();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(editor.gizmoPivotMode == GizmoPivotMode::Center ? "Center##GizmoPivot" : "Pivot##GizmoPivot"))
                        editor.gizmoPivotMode = (editor.gizmoPivotMode == GizmoPivotMode::Center) ? GizmoPivotMode::Pivot : GizmoPivotMode::Center;

                    if (focused && !Engine::IsKeyboardCapturedByUI())
                    {
                        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
                        {
                            if (PerformUndo(editor))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
                        {
                            if (PerformRedo(editor))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Home))
                        {
                            gfx.SetViewMode(ViewMode::Mode3D);
                            sceneCamera.ResetToDefaultView();
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_W))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Translate);
                        if (ImGui::IsKeyPressed(ImGuiKey_E))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Rotate);
                        if (ImGui::IsKeyPressed(ImGuiKey_R))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Scale);

                        if (ImGui::IsKeyPressed(ImGuiKey_F))
                        {
                            DirectX::XMFLOAT3 focusPoint{};
                            if (TryGetCurrentSelectionAnchor(editor, focusPoint))
                                gfx.GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                        {
                            PushUndoSnapshot(editor);
                            Scene::DeleteSelectedInstance();
                        }

                        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
                        {
                            PushUndoSnapshot(editor);
                            Scene::DuplicateSelectedInstance();
                        }
                    }

                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool navigatingCamera = IsSceneCameraNavigating(editor);

                        const bool allowCameraInput = hovered && focused && !g_EditorGizmo.IsDragging() && !Engine::IsKeyboardCapturedByUI();
                        gfx.SetSceneViewportCameraInputState(hovered, focused, allowCameraInput);

                        CameraData gizmoCamera{};
                        const ImVec2 hudButtonMin = ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f);
                        const ImVec2 hudButtonMax = ImVec2(hudButtonMin.x + 72.0f, hudButtonMin.y + ImGui::GetFrameHeight());
                        const bool overHudButton = io.MousePos.x >= hudButtonMin.x && io.MousePos.x <= hudButtonMax.x &&
                            io.MousePos.y >= hudButtonMin.y && io.MousePos.y <= hudButtonMax.y;
                        const bool allowGizmoInput = hovered && focused && !navigatingCamera && !overHudButton;
                        if (Scene::TryGetLastRenderCameraData(gizmoCamera))
                        {
                            if (SceneInstance* selected = Scene::GetSelectedInstance())
                            {
                                const bool wasDragging = g_EditorGizmo.IsDragging();
                                g_EditorGizmo.Update(
                                    selected,
                                    gizmoCamera,
                                    ImGui::GetWindowDrawList(),
                                    sceneMin,
                                    size,
                                    hovered,
                                    allowGizmoInput,
                                    editor.gizmoPivotMode == GizmoPivotMode::Center);

                                if (!wasDragging && g_EditorGizmo.IsDragging())
                                    PushUndoSnapshot(editor);
                                g_WasGizmoDragging = g_EditorGizmo.IsDragging();

                                editor.selection.position = TryGetCurrentSelectionAnchor(editor, editor.selection.position)
                                    ? editor.selection.position
                                    : Scene::GetSelectionCenterOrActivePosition();
                            }
                            else
                            {
                                g_WasGizmoDragging = false;
                            }
                        }
                    }

                    // Scene picking (LMB) - preserve camera-nav priority (RMB freelook)
                    if (hovered)
                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
                        const bool navigatingCamera = IsSceneCameraNavigating(editor);
                        const ImVec2 hudButtonMin = ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f);
                        const ImVec2 hudButtonMax = ImVec2(hudButtonMin.x + 72.0f, hudButtonMin.y + ImGui::GetFrameHeight());
                        const bool overHudButton = io.MousePos.x >= hudButtonMin.x && io.MousePos.x <= hudButtonMax.x &&
                            io.MousePos.y >= hudButtonMin.y && io.MousePos.y <= hudButtonMax.y;
                        const bool clickingGizmo = g_EditorGizmo.IsDragging() || g_EditorGizmo.HasHoveredHandle();

                        const float localMouseX = io.MousePos.x - sceneMin.x;
                        const float localMouseY = io.MousePos.y - sceneMin.y;
                        if (!navigatingCamera && !overHudButton && !clickingGizmo)
                            Scene::TryHoverInstanceAtViewportPoint(localMouseX, localMouseY, size.x, size.y);
                        else
                            Scene::ClearHoveredInstance();

                        if (!navigatingCamera && !overHudButton && !clickingGizmo && viewportClicked && !rmbDown && !mmbDown)
                        {
                            const bool shiftSelect = io.KeyShift;
                            if (Scene::TrySelectInstanceAtViewportPoint(localMouseX, localMouseY, size.x, size.y, shiftSelect, shiftSelect))
                            {
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();

                                Logger::Log(LogLevel::Info, std::format("Selected instance {}", Scene::GetSelectedInstanceIndex()), "Editor");
                            }
                            else if (!shiftSelect)
                            {
                                editor.selection.Clear();
                                Logger::Log(LogLevel::Info, "Selection cleared", "Editor");
                            }
                        }
                    }
                    else
                    {
                        Scene::ClearHoveredInstance();
                    }
                }
                else
                {
                    gfx.SetSceneViewportCameraInputState(false, false, false);
                    if (shouldLogSceneDiag)
                    {
                        Logger::Log(LogLevel::Debug, "[ScenePanel] Scene render target not ready for ImGui::Image", "[Editor]");
                    }
                    ImGui::TextUnformatted("Scene render target not ready...");
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_diagnostics{
        "Diagnostics",
        false,
        []()
        {
            auto& panel = Diagnostics();
            if (!panel.open)
                return;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                auto& gfx = Graphics::GetInstance();
                ImGuiIO& io = ImGui::GetIO();

                const float fps = io.Framerate;
                const float dt = (fps > 0.0f) ? (1.0f / fps) : 0.0f;
                const float frameMs = dt * 1000.0f;

                if (g_MonoFont) ImGui::PushFont(g_MonoFont);
                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("DeltaTime: %.4f s", dt);
                ImGui::Text("Frame Time: %.2f ms", frameMs);
                ImGui::Separator();

                ImGui::Text("SwapChain: %u x %u", gfx.GetSwapChainWidth(), gfx.GetSwapChainHeight());
                ImGui::Text("BackBuffer Index: %u", gfx.GetBackBufferIndex());
                ImGui::Separator();

                ImGui::Text("Scene RT: %u x %u", gfx.GetSceneRTWidth(), gfx.GetSceneRTHeight());
                ImGui::Text("Scene RT Pending Resize: %s", gfx.IsSceneRTPendingResize() ? "YES" : "NO");
                if (gfx.IsSceneRTPendingResize())
                    ImGui::Text("Pending Scene RT: %u x %u", gfx.GetPendingSceneRTWidth(), gfx.GetPendingSceneRTHeight());

                ImGui::Separator();

                const SceneStats stats = Scene::GetLastStats();
                ImGui::TextUnformatted("Scene Stats");
                ImGui::Text("Objects: %u", stats.totalObjects);
                ImGui::Text("Visible: %u", stats.visibleObjects);
                ImGui::Text("Draws:   %u", stats.drawCalls);

                ImGui::Separator();

                SceneCamera& cam = gfx.GetSceneCamera();
                DirectX::XMFLOAT3 targ{};
                DirectX::XMStoreFloat3(&targ, cam.GetTarget());

                ImGui::Text("Camera Yaw:   %.3f", cam.GetYaw());
                ImGui::Text("Camera Pitch: %.3f", cam.GetPitch());
                ImGui::Text("Camera Dist:  %.3f", cam.GetDistance());
                ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)", targ.x, targ.y, targ.z);
                ImGui::Separator();
                ImGui::Text("UI Mouse Capture: %d", Engine::IsMouseCapturedByUI() ? 1 : 0);
                ImGui::Text("UI Keyboard Capture: %d", Engine::IsKeyboardCapturedByUI() ? 1 : 0);
                if (g_MonoFont) ImGui::PopFont();
            }
            ImGui::End();
        }
    };

    static EditorPanel g_logViewer{
        "Log Viewer",
        false,
        []()
        {
            auto& panel = LogViewer();
            if (!panel.open)
                return;

            static char filter[256] = "";
            static int levelIdx = 0;
            static bool autoScroll = true;
            static constexpr size_t kMaxLines = 500;

            auto passesLevel = [&](const std::string& line) -> bool
            {
                // Level dropdown: Trace/Info/Warn/Error
                switch (levelIdx)
                {
                case 0: return line.find("[TRACE]") != std::string::npos;
                case 1: return line.find("[INFO]") != std::string::npos || line.find("[SUCCESS]") != std::string::npos;
                case 2: return line.find("[WARNING]") != std::string::npos;
                case 3: return line.find("[ERROR]") != std::string::npos || line.find("[CRITICAL]") != std::string::npos;
                default: return true;
                }
            };

            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (ImGui::Button("Clear"))
                    Logger::ClearAllLogs();
                ImGui::SameLine();
                ImGui::Checkbox("Auto-scroll", &autoScroll);

                ImGui::InputText("Filter", filter, sizeof(filter));

                const char* levels[] = { "Trace", "Info", "Warn", "Error" };
                ImGui::Combo("Level", &levelIdx, levels, IM_ARRAYSIZE(levels));

                ImGui::Separator();

                ImGui::BeginChild("LogViewerChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                const auto lines = Logger::GetRecentLogs(kMaxLines);
                for (const auto& line : lines)
                {
                    if (filter[0] != '\0' && line.find(filter) == std::string::npos)
                        continue;
                    if (!passesLevel(line))
                        continue;

                    ImGui::TextUnformatted(line.c_str());
                }

                if (autoScroll)
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
            }
            ImGui::End();
        }
    };

    static void DrawHierarchyNode(uint32_t instanceId, EditorState& editor)
    {
        SceneInstance* instance = nullptr;
        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].instanceId == instanceId)
            {
                instance = Scene::GetInstance(i);
                break;
            }
        }
        if (!instance)
            return;

        const auto children = Scene::GetChildInstanceIds(instanceId);
        const bool isSelected = Scene::IsInstanceSelected(instance->instanceId);
        const bool isPrefabInstance = !instance->prefabSourcePath.empty();
        PrefabOverrideState prefabOverrides{};
        const bool hasPrefabOverrides = isPrefabInstance && Scene::TryGetPrefabOverrideState(instance->instanceId, prefabOverrides) && prefabOverrides.Any();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        const std::string hierarchyLabel = isPrefabInstance
            ? std::format("{} {}{}", ICON_FA_CUBE, instance->name, hasPrefabOverrides ? " *" : "")
            : instance->name;
        const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)instance->instanceId, flags, "%s", hierarchyLabel.c_str());
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyShift)
                Scene::ToggleSelectedInstanceId(instance->instanceId);
            else
                Scene::SetSelectedInstanceId(instance->instanceId);
        }

        if (ImGui::BeginDragDropSource())
        {
            const uint32_t payloadId = instance->instanceId;
            ImGui::SetDragDropPayload("SCENE_INSTANCE", &payloadId, sizeof(payloadId));
            ImGui::TextUnformatted(hierarchyLabel.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_INSTANCE"))
            {
                const uint32_t draggedId = *(const uint32_t*)payload->Data;
                if (draggedId != instance->instanceId && Scene::CanParentInstance(draggedId, instance->instanceId))
                {
                    PushUndoSnapshot(editor);
                    if (Scene::SetParentInstance(draggedId, instance->instanceId, true))
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (open)
        {
            for (uint32_t childId : children)
                DrawHierarchyNode(childId, editor);
            ImGui::TreePop();
        }
    }

    static EditorPanel g_hierarchy{
        "Hierarchy",
        true,
        []()
        {
            auto& panel = Hierarchy();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                extern Engine* g_engineInstance;
                EditorState& editor = g_engineInstance->GetEditorState();

                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_SITEMAP " Scene Hierarchy");
                if (g_UIFontBold) ImGui::PopFont();

                ImGui::Separator();
                const auto& instances = Scene::GetInstances();
                for (const SceneInstance& instance : instances)
                {
                    if (instance.parentInstanceId == 0)
                        DrawHierarchyNode(instance.instanceId, editor);
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_INSTANCE"))
                    {
                        const uint32_t draggedId = *(const uint32_t*)payload->Data;
                        if (Scene::GetParentInstanceId(draggedId) != 0)
                        {
                            PushUndoSnapshot(editor);
                            if (Scene::SetParentInstance(draggedId, 0, true))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_inspector{
        "Inspector",
        true,
        []()
        {
            auto& panel = Inspector();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_SEARCH " Inspector");
                if (g_UIFontBold) ImGui::PopFont();

                if (SceneInstance* selected = Scene::GetSelectedInstance())
                {
                    extern Engine* g_engineInstance;
                    EditorState& editor = g_engineInstance->GetEditorState();

                    char nameBuffer[128] = {};
                    strncpy_s(nameBuffer, selected->name.c_str(), sizeof(nameBuffer) - 1);

                    PrefabOverrideState prefabOverrides{};
                    const bool hasPrefabOverrideState = !selected->prefabSourcePath.empty() &&
                        Scene::TryGetPrefabOverrideState(selected->instanceId, prefabOverrides);

                    const ImVec4 overrideTextColor = ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
                    const ImVec4 overrideFrameBg = ImVec4(0.42f, 0.26f, 0.08f, 0.45f);
                    const ImVec4 overrideFrameBgHovered = ImVec4(0.50f, 0.31f, 0.10f, 0.60f);
                    const ImVec4 overrideFrameBgActive = ImVec4(0.58f, 0.35f, 0.12f, 0.75f);

                    auto pushOverrideHighlight = [&](bool enabled)
                    {
                        if (!enabled)
                            return;
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, overrideFrameBg);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, overrideFrameBgHovered);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, overrideFrameBgActive);
                    };
                    auto popOverrideHighlight = [&](bool enabled)
                    {
                        if (enabled)
                            ImGui::PopStyleColor(3);
                    };
                    auto revertPrefabProperty = [&](PrefabProperty property)
                    {
                        PushUndoSnapshot(editor);
                        if (!Scene::RevertSelectedPrefabProperty(property))
                            Logger::Log(LogLevel::Error, "Failed to revert prefab property.", "[Editor]");
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    };
                    auto applyPrefabProperty = [&](PrefabProperty property)
                    {
                        if (!Scene::ApplySelectedPrefabProperty(property))
                            Logger::Log(LogLevel::Error, "Failed to apply prefab property.", "[Editor]");
                    };
                    auto drawOverrideControls = [&](bool enabled, const char* id, PrefabProperty property)
                    {
                        if (!enabled)
                            return;
                        ImGui::SameLine();
                        ImGui::TextColored(overrideTextColor, "Override");
                        ImGui::SameLine();
                        const std::string applyButtonId = std::format("Apply##{}", id);
                        if (ImGui::SmallButton(applyButtonId.c_str()))
                            applyPrefabProperty(property);
                        ImGui::SameLine();
                        const std::string revertButtonId = std::format("Revert##{}", id);
                        if (ImGui::SmallButton(revertButtonId.c_str()))
                            revertPrefabProperty(property);
                    };

                    ImGui::Separator();
                    ImGui::Text("Object ID: %u", selected->instanceId);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Move To Origin"))
                    {
                        PushUndoSnapshot(editor);
                        selected->position = { 0.0f, 0.0f, 0.0f };
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Save Prefab"))
                    {
                        const std::filesystem::path prefabPath = std::filesystem::path("Assets") / "Prefabs" /
                            (SanitizeAssetName(selected->name) + ".prefab.json");
                        if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
                            Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
                    }

                    if (!selected->prefabSourcePath.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), ICON_FA_LINK " Prefab Instance");
                        ImGui::TextWrapped("Source: %s", selected->prefabSourcePath.c_str());

                        if (hasPrefabOverrideState)
                        {
                            if (prefabOverrides.Any())
                            {
                                std::string overrideList;
                                auto appendOverride = [&overrideList](const char* label)
                                {
                                    if (!overrideList.empty())
                                        overrideList += ", ";
                                    overrideList += label;
                                };
                                if (prefabOverrides.name) appendOverride("Name");
                                if (prefabOverrides.rotation) appendOverride("Rotation");
                                if (prefabOverrides.scale) appendOverride("Scale");
                                if (prefabOverrides.visible) appendOverride("Visible");
                                if (prefabOverrides.material) appendOverride("Material");
                                ImGui::TextColored(overrideTextColor, "Overrides: %s", overrideList.c_str());
                            }
                            else
                            {
                                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f), "Overrides: None");
                            }
                        }

                        if (ImGui::SmallButton("Apply Prefab"))
                        {
                            if (!Scene::ApplySelectedToPrefab())
                                Logger::Log(LogLevel::Error, "Failed to apply prefab.", "[Editor]");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Revert Prefab"))
                        {
                            PushUndoSnapshot(editor);
                            if (!Scene::RevertSelectedToPrefab())
                                Logger::Log(LogLevel::Error, "Failed to revert prefab.", "[Editor]");
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                    }
                    else
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Scene Object (not linked to a prefab)");
                    }

                    const uint32_t parentId = Scene::GetParentInstanceId(selected->instanceId);
                    std::string parentLabel = "None";
                    if (parentId != 0)
                    {
                        for (const auto& inst : Scene::GetInstances())
                        {
                            if (inst.instanceId == parentId)
                            {
                                parentLabel = inst.name;
                                break;
                            }
                        }
                    }
                    if (ImGui::BeginCombo("Parent", parentLabel.c_str()))
                    {
                        const bool noParentSelected = (parentId == 0);
                        if (ImGui::Selectable("None", noParentSelected))
                        {
                            PushUndoSnapshot(editor);
                            Scene::SetParentInstance(selected->instanceId, 0, true);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }

                        for (const auto& candidate : Scene::GetInstances())
                        {
                            if (candidate.instanceId == selected->instanceId)
                                continue;
                            const bool canParent = Scene::CanParentInstance(selected->instanceId, candidate.instanceId);
                            if (!canParent)
                                ImGui::BeginDisabled();
                            const bool isCurrentParent = (parentId == candidate.instanceId);
                            if (ImGui::Selectable(candidate.name.c_str(), isCurrentParent) && canParent)
                            {
                                PushUndoSnapshot(editor);
                                Scene::SetParentInstance(selected->instanceId, candidate.instanceId, true);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (!canParent)
                                ImGui::EndDisabled();
                        }
                        ImGui::EndCombo();
                    }

                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.name);
                    const bool nameChanged = ImGui::InputText("Name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
                    const bool nameActivated = ImGui::IsItemActivated();
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.name);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.name, "Name", PrefabProperty::Name);
                    if (nameActivated)
                        PushUndoSnapshot(editor);
                    if (nameChanged)
                        selected->name = nameBuffer;

                    DirectX::XMFLOAT3 position = selected->position;
                    const bool positionChanged = ImGui::InputFloat3("Position", &position.x);
                    if (ImGui::IsItemActivated())
                        PushUndoSnapshot(editor);
                    if (positionChanged)
                    {
                        selected->position = position;
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }

                    DirectX::XMFLOAT3 rotation = selected->rotation;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    const bool rotationChanged = ImGui::InputFloat3("Rotation", &rotation.x);
                    const bool rotationActivated = ImGui::IsItemActivated();
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.rotation, "Rotation", PrefabProperty::Rotation);
                    if (rotationActivated)
                        PushUndoSnapshot(editor);
                    if (rotationChanged)
                        selected->rotation = rotation;

                    DirectX::XMFLOAT3 scale = selected->scale;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    const bool scaleChanged = ImGui::InputFloat3("Scale", &scale.x);
                    const bool scaleActivated = ImGui::IsItemActivated();
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.scale, "Scale", PrefabProperty::Scale);
                    if (scaleActivated)
                        PushUndoSnapshot(editor);
                    if (scaleChanged)
                    {
                        selected->scale.x = (std::max)(scale.x, 0.01f);
                        selected->scale.y = (std::max)(scale.y, 0.01f);
                        selected->scale.z = (std::max)(scale.z, 0.01f);
                    }

                    bool visible = selected->visible;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.visible);
                    const bool visibleChanged = ImGui::Checkbox("Visible", &visible);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.visible);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.visible, "Visible", PrefabProperty::Visible);
                    if (visibleChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->visible = visible;
                    }

                    if (hasPrefabOverrideState && prefabOverrides.material)
                    {
                        ImGui::TextColored(overrideTextColor, "Material Index: %d", selected->materialIndex);
                        drawOverrideControls(true, "Material", PrefabProperty::Material);
                    }
                    else
                    {
                        ImGui::Text("Material Index: %d", selected->materialIndex);
                    }

                    MaterialManager& materials = MaterialManager::GetInstance();
                    Material* material = materials.GetMaterial("TestMaterial");
                    if (!material)
                        material = materials.CreateMaterial("TestMaterial");

                    ImGui::Separator();
                    ImGui::TextUnformatted("Material");
                    ImGui::TextDisabled("Shared renderer material (currently affects all scene objects)");
                    ImGui::Text("Material Slot: %d", selected->materialIndex);

                    if (material)
                    {
                        std::string currentTextureLabel = "<None>";
                        if (material->albedo && !material->albedo->sourcePath.empty())
                            currentTextureLabel = std::filesystem::path(material->albedo->sourcePath).filename().string();

                        ImTextureID materialThumb = 0;
                        if (material->albedo && !material->albedo->sourcePath.empty())
                            materialThumb = GetAssetThumbnailTexture(material->albedo->sourcePath, material->albedo->sourcePath);

                        if (materialThumb)
                        {
                            ImGui::TextUnformatted("Albedo Preview");
                            ImGui::Image(materialThumb, ImVec2(72.0f, 72.0f), ImVec2(0, 0), ImVec2(1, 1));
                        }

                        if (ImGui::BeginCombo("Albedo Texture", currentTextureLabel.c_str()))
                        {
                            const bool noTextureSelected = (material->albedo == nullptr);
                            if (ImGui::Selectable("<None>", noTextureSelected))
                                materials.SetAlbedoTexture(material, nullptr);

                            for (const auto& texturePath : EnumerateTextureAssets())
                            {
                                const std::string texturePathString = texturePath.string();
                                const std::string textureName = texturePath.filename().string();
                                const bool isCurrent = material->albedo && material->albedo->sourcePath == texturePathString;
                                if (ImGui::Selectable(textureName.c_str(), isCurrent))
                                    materials.LoadAlbedoTexture(material, texturePathString);
                            }
                            ImGui::EndCombo();
                        }

                        float metallic = material->metallic;
                        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
                            material->SetFloat("metallic", metallic);

                        float roughness = material->roughness;
                        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
                            material->SetFloat("roughness", roughness);
                    }
                }
                else
                {
                    ImGui::Separator();
                    ImGui::TextUnformatted("No object selected.");
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_assets{
        "Assets",
        true,
        []()
        {
            auto& panel = Assets();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_FOLDER_OPEN " Asset Browser");
                if (g_UIFontBold) ImGui::PopFont();

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##AssetSearch", "Search assets...", g_AssetSearch, IM_ARRAYSIZE(g_AssetSearch));
                ImGui::TextDisabled("Assets / %s", GetAssetCategoryLabel(g_SelectedAssetCategory));
                ImGui::Separator();

                const float sidebarWidth = 150.0f;
                const float paneHeight = ImGui::GetContentRegionAvail().y;

                ImGui::BeginChild("##AssetCategories", ImVec2(sidebarWidth, paneHeight), true);
                if (ImGui::Selectable("All", g_SelectedAssetCategory == AssetCategory::All))
                    g_SelectedAssetCategory = AssetCategory::All;
                if (ImGui::Selectable("Primitives", g_SelectedAssetCategory == AssetCategory::Primitives))
                    g_SelectedAssetCategory = AssetCategory::Primitives;
                if (ImGui::Selectable("Prefabs", g_SelectedAssetCategory == AssetCategory::Prefabs))
                    g_SelectedAssetCategory = AssetCategory::Prefabs;
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##AssetContent", ImVec2(0, paneHeight), false);

                const std::string filterText = g_AssetSearch;
                auto matchesFilter = [&filterText](const std::string& label)
                {
                    if (filterText.empty())
                        return true;

                    std::string lowerLabel = label;
                    std::string lowerFilter = filterText;
                    std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    return lowerLabel.find(lowerFilter) != std::string::npos;
                };

                constexpr float kTileWidth = 118.0f;
                constexpr float kTileSpacing = 10.0f;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const int columns = GetAssetTileColumnCount(availableWidth, kTileWidth, kTileSpacing);

                const bool showPrimitives = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Primitives;
                const bool showPrefabs = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Prefabs;

                if (showPrimitives)
                {
                    DrawAssetSectionHeader("Primitives");
                    if (matchesFilter("Cube"))
                    {
                        const ImTextureID cubeThumb = GetAssetThumbnailTexture("PrimitiveCube", "Assets/Textures/crate.png");
                        if (DrawAssetTile("PrimitiveCube", "Cube", ICON_FA_CUBE, kSceneCreateCubePayload, "Cube", std::strlen("Cube") + 1u, g_SelectedAssetId == "PrimitiveCube", cubeThumb))
                            g_SelectedAssetId = "PrimitiveCube";
                    }
                    else
                    {
                        ImGui::TextDisabled("No primitive assets match the filter.");
                    }
                }

                if (showPrefabs)
                {
                    DrawAssetSectionHeader("Prefabs");
                    const auto prefabAssets = EnumeratePrefabAssets();
                    int tileIndex = 0;
                    bool anyPrefabShown = false;
                    for (const auto& prefabPath : prefabAssets)
                    {
                        const std::string label = prefabPath.stem().stem().string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string prefabPathString = prefabPath.string();
                        const ImTextureID prefabThumb = GetAssetThumbnailTexture(prefabPathString, "Assets/Textures/crate.png");
                        if (DrawAssetTile(prefabPathString.c_str(), label.c_str(), ICON_FA_CUBE, kSceneCreatePrefabPayload, prefabPathString.c_str(), prefabPathString.size() + 1u, g_SelectedAssetId == prefabPathString, prefabThumb))
                            g_SelectedAssetId = prefabPathString;
                        ++tileIndex;
                        anyPrefabShown = true;
                    }

                    if (!anyPrefabShown)
                    {
                        if (prefabAssets.empty())
                            ImGui::TextDisabled("No prefabs saved yet.");
                        else
                            ImGui::TextDisabled("No prefabs match the filter.");
                    }
                }

                ImGui::EndChild();
            }
            ImGui::End();
        }
    };

    static EditorPanel g_debugOverlay{
        "Debug Overlay",
        false,
        []()
        {
            auto& panel = DebugOverlay();
            if (!panel.open)
                return;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::Text("ImGui is rendering");
                ImGui::Separator();
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            }
            ImGui::End();
        }
    };

    static EditorPanel g_instancing{
        "Instancing",
        false,
        []()
        {
            auto& panel = Instancing();
            if (!panel.open)
                return;

            uint32_t targetInstanceCount = Scene::GetTargetInstanceCount();
            int instanceCount = (int)targetInstanceCount;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (ImGui::SliderInt("Instance Count", &instanceCount, 1, 10000))
                    Scene::SetTargetInstanceCount((uint32_t)instanceCount);

                ImGui::Separator();
                ImGui::Text("Visible: %u", (unsigned)Scene::GetVisibleInstanceCount());
                ImGui::Text("Total:   %u", (unsigned)Scene::GetInstanceCount());
            }
            ImGui::End();
        }
    };

    EditorPanel& Scene() { return g_scene; }
    EditorPanel& Hierarchy() { return g_hierarchy; }
    EditorPanel& Inspector() { return g_inspector; }
    EditorPanel& Assets() { return g_assets; }
    EditorPanel& DebugOverlay() { return g_debugOverlay; }
    EditorPanel& Diagnostics() { return g_diagnostics; }
    EditorPanel& LogViewer() { return g_logViewer; }
    EditorPanel& Instancing() { return g_instancing; }

    void DrawAll()
    {
        if (g_scene.open) g_scene.draw();
        if (g_hierarchy.open) g_hierarchy.draw();
        if (g_inspector.open) g_inspector.draw();
        if (g_assets.open) g_assets.draw();
        if (g_instancing.open) g_instancing.draw();
        if (g_debugOverlay.open) g_debugOverlay.draw();
        if (g_diagnostics.open) g_diagnostics.draw();
        if (g_logViewer.open) g_logViewer.draw();
    }

    static int GetAssetTileColumnCount(float availableWidth, float tileWidth, float spacing)
    {
        return (std::max)(1, (int)((availableWidth + spacing) / (tileWidth + spacing)));
    }

    static ImTextureID GetAssetThumbnailTexture(const std::string& assetId, const std::string& fallbackTexturePath)
    {
        (void)assetId;
        (void)fallbackTexturePath;
        return 0;
    }

    static bool DrawAssetTile(const char* id, const char* title, const char* icon, const char* payloadType, const void* payloadData, size_t payloadSize, bool selected, ImTextureID thumbnailTexture)
    {
        constexpr float kTileWidth = 118.0f;
        constexpr float kTileHeight = 92.0f;
        constexpr float kCornerRounding = 8.0f;

        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##AssetTile", ImVec2(kTileWidth, kTileHeight));

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const ImU32 bg = selected
            ? IM_COL32(64, 96, 150, 235)
            : (active ? IM_COL32(82, 112, 156, 230) : (hovered ? IM_COL32(54, 58, 66, 235) : IM_COL32(36, 38, 43, 220)));
        const ImU32 border = selected
            ? IM_COL32(140, 190, 255, 255)
            : (hovered ? IM_COL32(110, 160, 235, 255) : IM_COL32(78, 82, 90, 255));
        const ImU32 iconColor = (hovered || selected) ? IM_COL32(255, 255, 255, 255) : IM_COL32(224, 228, 236, 255);
        const ImU32 textColor = IM_COL32(240, 242, 246, 255);
        const ImU32 subTextColor = selected ? IM_COL32(210, 226, 255, 255) : IM_COL32(150, 156, 168, 255);

        draw->AddRectFilled(min, max, bg, kCornerRounding);
        draw->AddRect(min, max, border, kCornerRounding, 0, selected ? 2.5f : (hovered ? 2.0f : 1.0f));

        const ImVec2 previewMin(min.x + 8.0f, min.y + 8.0f);
        const ImVec2 previewMax(max.x - 8.0f, min.y + 48.0f);
        draw->AddRectFilled(previewMin, previewMax, IM_COL32(24, 26, 31, 235), 6.0f);
        draw->AddRect(previewMin, previewMax, IM_COL32(255, 255, 255, 12), 6.0f, 0, 1.0f);

        if (thumbnailTexture)
        {
            draw->AddImage(thumbnailTexture, previewMin, previewMax, ImVec2(0, 0), ImVec2(1, 1));
            draw->AddRect(previewMin, previewMax, IM_COL32(255, 255, 255, 28), 6.0f, 0, 1.0f);
        }
        else
        {
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 iconPos(previewMin.x + ((previewMax.x - previewMin.x) - iconSize.x) * 0.5f, previewMin.y + ((previewMax.y - previewMin.y) - iconSize.y) * 0.5f);
            draw->AddText(iconPos, iconColor, icon);
        }

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        const ImVec2 titlePos(min.x + 10.0f, min.y + 58.0f);
        const char* typeLabel = (std::strcmp(payloadType, kSceneCreateCubePayload) == 0) ? "Primitive" : "Prefab";
        const ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);
        const ImVec2 typePos(min.x + 10.0f, max.y - typeSize.y - 8.0f);

        draw->AddText(titlePos, textColor, title);
        draw->AddText(typePos, subTextColor, typeLabel);

        if (titleSize.x > (kTileWidth - 20.0f))
        {
            draw->AddLine(ImVec2(min.x + 10.0f, min.y + 74.0f), ImVec2(max.x - 10.0f, min.y + 74.0f), IM_COL32(255, 255, 255, 18), 1.0f);
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload(payloadType, payloadData, payloadSize);
            ImGui::TextUnformatted(title);
            ImGui::EndDragDropSource();
        }

        ImGui::PopID();
        return clicked;
    }

    static void DrawAssetSectionHeader(const char* label)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", label);
        ImGui::Separator();
    }

    static std::vector<std::filesystem::path> EnumerateTextureAssets()
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path textureDir = std::filesystem::path("Assets") / "Textures";
        if (!std::filesystem::exists(textureDir))
            return result;

        for (const auto& entry : std::filesystem::directory_iterator(textureDir))
        {
            if (!entry.is_regular_file())
                continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga")
                continue;

            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }
}
