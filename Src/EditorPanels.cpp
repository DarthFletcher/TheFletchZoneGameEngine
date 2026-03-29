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
#include "TextureManager.h"
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
        Textures,
        Materials,
    };

    static constexpr size_t kMaxUndoSnapshots = 64;
    static constexpr const char* kSceneCreateCubePayload = "SCENE_CREATE_CUBE";
    static constexpr const char* kSceneCreateSpherePayload = "SCENE_CREATE_SPHERE";
    static constexpr const char* kSceneCreatePlanePayload = "SCENE_CREATE_PLANE";
    static constexpr const char* kSceneCreateCylinderPayload = "SCENE_CREATE_CYLINDER";
    static constexpr const char* kSceneCreatePrefabPayload = "SCENE_CREATE_PREFAB";
    static constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
    static constexpr const char* kAssetMaterialPayload = "ASSET_MATERIAL";
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
        case AssetCategory::Textures: return "Textures";
        case AssetCategory::Materials: return "Materials";
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

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(prefabDir, ec))
        {
            if (ec)
                break;
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

    static bool DrawTickFloat(const char* label, float& value, float minValue, float maxValue, float step, const char* format = "%.2f")
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::SmallButton("-"))
        {
            value = (std::max)(minValue, value - step);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(96.0f);
        changed = ImGui::DragFloat("##value", &value, step, minValue, maxValue, format) || changed;
        value = std::clamp(value, minValue, maxValue);
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
        {
            value = (std::min)(maxValue, value + step);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    static bool DrawTickFloat3(const char* label, DirectX::XMFLOAT3& value, float step, float minValue, float maxValue)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        changed = ImGui::DragFloat3("##value", &value.x, step, minValue, maxValue, "%.2f") || changed;
        const char* axisLabels[3] = { "X", "Y", "Z" };
        float* axisValues[3] = { &value.x, &value.y, &value.z };
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis > 0)
                ImGui::SameLine();
            ImGui::TextUnformatted(axisLabels[axis]);
            ImGui::SameLine();
            const std::string minusId = std::format("-{}", axisLabels[axis]);
            if (ImGui::SmallButton(minusId.c_str()))
            {
                *axisValues[axis] = (std::max)(minValue, *axisValues[axis] - step);
                changed = true;
            }
            ImGui::SameLine();
            const std::string plusId = std::format("+{}", axisLabels[axis]);
            if (ImGui::SmallButton(plusId.c_str()))
            {
                *axisValues[axis] = (std::min)(maxValue, *axisValues[axis] + step);
                changed = true;
            }
        }
        ImGui::PopID();
        return changed;
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
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateSpherePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateSphere(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreatePlanePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            const DirectX::XMFLOAT3 planeSpawnPos{ spawnPos.x, 0.0f, spawnPos.z };
                            Scene::CreatePlane(planeSpawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateCylinderPayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCylinder(spawnPos);
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
                    if (ImGui::SmallButton("Focus"))
                    {
                        DirectX::XMFLOAT3 focusPoint = Scene::GetSelectionCenterOrActivePosition();
                        Graphics::GetInstance().GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Duplicate"))
                    {
                        PushUndoSnapshot(editor);
                        Scene::DuplicateSelectedInstance();
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete"))
                    {
                        PushUndoSnapshot(editor);
                        Scene::DeleteSelectedInstance();
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Save Prefab"))
                    {
                        const std::filesystem::path prefabPath = std::filesystem::path("Assets") / "Prefabs" /
                            (SanitizeAssetName(selected->name) + ".prefab.json");
                        if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
                            Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
                        else
                            EditorPanels::PrefabWorkflow().open = true;
                    }

                    if (!selected->prefabSourcePath.empty())
                    {
                        ImGui::SameLine();
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

                    if (ImGui::SmallButton("Material Preview"))
                        EditorPanels::MaterialPreview().open = true;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Prefab Workflow"))
                        EditorPanels::PrefabWorkflow().open = true;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Black Flame"))
                        EditorPanels::BlackFlame().open = true;

                    if (hasPrefabOverrideState)
                    {
                        ImGui::TextColored(overrideTextColor, "Prefab Instance");
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
                    const bool positionChanged = DrawTickFloat3("Position", position, 0.1f, -10000.0f, 10000.0f);
                    if (positionChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->position = position;
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }

                    DirectX::XMFLOAT3 rotation = selected->rotation;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    const bool rotationChanged = DrawTickFloat3("Rotation", rotation, 1.0f, -360.0f, 360.0f);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.rotation, "Rotation", PrefabProperty::Rotation);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset##Rotation"))
                    {
                        PushUndoSnapshot(editor);
                        selected->rotation = { 0.0f, 0.0f, 0.0f };
                    }
                    else if (rotationChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->rotation = rotation;
                    }

                    DirectX::XMFLOAT3 scale = selected->scale;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    const bool scaleChanged = DrawTickFloat3("Scale", scale, 0.05f, 0.01f, 100.0f);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.scale, "Scale", PrefabProperty::Scale);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset##Scale"))
                    {
                        PushUndoSnapshot(editor);
                        selected->scale = { 1.0f, 1.0f, 1.0f };
                    }
                    else if (scaleChanged)
                    {
                        PushUndoSnapshot(editor);
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
                    Material* material = materials.GetMaterialByIndex(selected->materialIndex);

                    auto emitMaterialChanged = [&](Material* changedMaterial)
                    {
                        if (!changedMaterial)
                            return;
                        SceneEvent evt{};
                        evt.Type = SceneEventType::MaterialChanged;
                        evt.Entity = selected;
                        evt.Material = changedMaterial;
                        evt.ColorBias = changedMaterial->baseColor;
                        evt.EventStrength = 0.8f;
                        editor.sceneEvents.Emit(evt);
                    };

                    ImGui::Separator();
                    ImGui::TextUnformatted("Material");
                    ImGui::Text("Material Slot: %d", selected->materialIndex);

                    const std::string materialLabel = material ? material->name : std::string("<Invalid>");
                    if (ImGui::BeginCombo("Assigned Material", materialLabel.c_str()))
                    {
                        for (int i = 0; i < materials.GetMaterialCount(); ++i)
                        {
                            const std::string* materialName = materials.GetMaterialNameByIndex(i);
                            if (!materialName)
                                continue;
                            const bool isCurrent = (selected->materialIndex == i);
                            if (ImGui::Selectable(materialName->c_str(), isCurrent))
                            {
                                PushUndoSnapshot(editor);
                                selected->materialIndex = i;
                                material = materials.GetMaterialByIndex(i);
                                Scene::RebuildRenderInstancesFromSceneData();
                                Scene::MarkInstancesDirty();
                                emitMaterialChanged(material);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::SmallButton("Create Material"))
                    {
                        Material* created = materials.CreateMaterial(std::format("Material_{}", selected->instanceId));
                        if (created)
                        {
                            PushUndoSnapshot(editor);
                            selected->materialIndex = materials.GetMaterialCount() - 1;
                            material = created;
                            Scene::RebuildRenderInstancesFromSceneData();
                            Scene::MarkInstancesDirty();
                            emitMaterialChanged(material);
                        }
                    }

                    if (material)
                    {
                        auto textureLabel = [](Texture* texture) -> std::string
                        {
                            if (!texture || texture->sourcePath.empty())
                                return "<None>";
                            return std::filesystem::path(texture->sourcePath).filename().string();
                        };

                        auto drawTextureSlot = [&](const char* comboLabel, Texture* currentTexture, const std::function<void(const std::string&)>& assignTexture, const std::function<void()>& clearTexture)
                        {
                            const std::string currentTextureLabel = textureLabel(currentTexture);
                            if (ImGui::BeginCombo(comboLabel, currentTextureLabel.c_str()))
                            {
                                static std::unordered_map<std::string, std::string> textureFilters;
                                std::string& filterText = textureFilters[comboLabel];
                                char filterBuffer[128] = {};
                                strncpy_s(filterBuffer, filterText.c_str(), sizeof(filterBuffer) - 1u);
                                if (ImGui::InputTextWithHint("##TextureFilter", "Filter textures...", filterBuffer, IM_ARRAYSIZE(filterBuffer)))
                                    filterText = filterBuffer;

                                auto matchesTextureFilter = [&](const std::string& textureName)
                                {
                                    if (filterText.empty())
                                        return true;
                                    std::string lowerName = textureName;
                                    std::string lowerFilter = filterText;
                                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                                    return lowerName.find(lowerFilter) != std::string::npos;
                                };

                                const bool noTextureSelected = (currentTexture == nullptr);
                                if (matchesTextureFilter("<None>") && ImGui::Selectable("<None>", noTextureSelected))
                                {
                                    clearTexture();
                                    emitMaterialChanged(material);
                                }

                                const std::filesystem::path textureRoot = std::filesystem::path("Assets") / "Textures";
                                for (const auto& texturePath : EnumerateTextureAssets())
                                {
                                    const std::string texturePathString = texturePath.string();
                                    const std::string textureLabelText = texturePath.lexically_relative(textureRoot).generic_string();
                                    if (!matchesTextureFilter(textureLabelText))
                                        continue;
                                    const bool isCurrent = currentTexture && currentTexture->sourcePath == texturePathString;
                                    if (ImGui::Selectable(textureLabelText.c_str(), isCurrent))
                                    {
                                        assignTexture(texturePathString);
                                        emitMaterialChanged(material);
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        };

                        auto drawTexturePreview = [&](const char* label, Texture* texture)
                        {
                            ImGui::TextUnformatted(label);
                            if (texture && !texture->sourcePath.empty())
                            {
                                const ImTextureID preview = GetAssetThumbnailTexture(texture->sourcePath, texture->sourcePath);
                                if (preview)
                                {
                                    ImGui::Image(preview, ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
                                    ImGui::SameLine();
                                }
                                ImGui::BeginGroup();
                                ImGui::TextUnformatted(textureLabel(texture).c_str());
                                ImGui::TextDisabled("%d x %d", texture->width, texture->height);
                                ImGui::EndGroup();
                            }
                            else
                            {
                                ImGui::TextDisabled("<None>");
                            }
                        };

                        drawTexturePreview("Albedo Preview", material->albedo);
                        drawTextureSlot(
                            "Albedo Texture",
                            material->albedo,
                            [&](const std::string& texturePath) { materials.LoadAlbedoTexture(material, texturePath); },
                            [&]() { materials.SetAlbedoTexture(material, nullptr); });

                        drawTexturePreview("Normal Preview", material->normal);
                        drawTextureSlot(
                            "Normal Texture",
                            material->normal,
                            [&](const std::string& texturePath) { materials.LoadNormalTexture(material, texturePath); },
                            [&]() { materials.SetNormalTexture(material, nullptr); });

                        drawTexturePreview("Metallic Preview", material->metallicMap);
                        drawTextureSlot(
                            "Metallic Texture",
                            material->metallicMap,
                            [&](const std::string& texturePath) { materials.LoadMetallicTexture(material, texturePath); },
                            [&]() { materials.SetMetallicTexture(material, nullptr); });

                        drawTexturePreview("Roughness Preview", material->roughnessMap);
                        drawTextureSlot(
                            "Roughness Texture",
                            material->roughnessMap,
                            [&](const std::string& texturePath) { materials.LoadRoughnessTexture(material, texturePath); },
                            [&]() { materials.SetRoughnessTexture(material, nullptr); });

                        ImGui::Text("Normal: %s", textureLabel(material->normal).c_str());
                        ImGui::Text("Metallic Map: %s", textureLabel(material->metallicMap).c_str());
                        ImGui::Text("Roughness Map: %s", textureLabel(material->roughnessMap).c_str());

                        float baseColor[3] = { material->baseColor.x, material->baseColor.y, material->baseColor.z };
                        if (ImGui::ColorEdit3("Base Color", baseColor))
                        {
                            material->SetBaseColor({ baseColor[0], baseColor[1], baseColor[2] });
                            emitMaterialChanged(material);
                        }

                        float metallic = material->metallic;
                        if (DrawTickFloat("Metallic", metallic, 0.0f, 1.0f, 0.05f))
                        {
                            material->SetFloat("metallic", metallic);
                            emitMaterialChanged(material);
                        }

                        float roughness = material->roughness;
                        if (DrawTickFloat("Roughness", roughness, 0.0f, 1.0f, 0.05f))
                        {
                            material->SetFloat("roughness", roughness);
                            emitMaterialChanged(material);
                        }

                        bool flipNormalY = Graphics::GetInstance().GetFlipNormalGreenChannel();
                        if (ImGui::Checkbox("Normal Flip Y", &flipNormalY))
                        {
                            Graphics::GetInstance().GetFlipNormalGreenChannel() = flipNormalY;
                            emitMaterialChanged(material);
                        }
                    }
                }
                else
                {
                    ImGui::Separator();

                    extern Engine* g_engineInstance;
                    EditorState* editor = g_engineInstance ? &g_engineInstance->GetEditorState() : nullptr;
                    MaterialManager& materials = MaterialManager::GetInstance();
                    Material* focusedMaterial = (editor && editor->focusedMaterialIndex >= 0) ? materials.GetMaterialByIndex(editor->focusedMaterialIndex) : nullptr;

                    if (editor && focusedMaterial)
                    {
                        ImGui::TextUnformatted("Focused Material Asset");
                        ImGui::Text("Material: %s", focusedMaterial->name.c_str());
                        if (ImGui::SmallButton("Clear Material Focus"))
                            editor->focusedMaterialIndex = -1;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Material Preview"))
                            EditorPanels::MaterialPreview().open = true;

                        auto emitMaterialChanged = [&](Material* changedMaterial)
                        {
                            if (!changedMaterial)
                                return;
                            SceneEvent evt{};
                            evt.Type = SceneEventType::MaterialChanged;
                            evt.Material = changedMaterial;
                            evt.ColorBias = changedMaterial->baseColor;
                            evt.EventStrength = 0.8f;
                            editor->sceneEvents.Emit(evt);
                        };

                        float baseColor[3] = { focusedMaterial->baseColor.x, focusedMaterial->baseColor.y, focusedMaterial->baseColor.z };
                        if (ImGui::ColorEdit3("Base Color", baseColor))
                        {
                            focusedMaterial->SetBaseColor({ baseColor[0], baseColor[1], baseColor[2] });
                            emitMaterialChanged(focusedMaterial);
                        }

                        float metallic = focusedMaterial->metallic;
                        if (DrawTickFloat("Metallic", metallic, 0.0f, 1.0f, 0.05f))
                        {
                            focusedMaterial->SetFloat("metallic", metallic);
                            emitMaterialChanged(focusedMaterial);
                        }

                        float roughness = focusedMaterial->roughness;
                        if (DrawTickFloat("Roughness", roughness, 0.0f, 1.0f, 0.05f))
                        {
                            focusedMaterial->SetFloat("roughness", roughness);
                            emitMaterialChanged(focusedMaterial);
                        }

                        ImGui::Text("Texture Bound: %s", (focusedMaterial->albedo && !focusedMaterial->albedo->sourcePath.empty()) ? "Yes" : "No");
                    }
                    else
                    {
                        ImGui::TextUnformatted("No object selected.");
                    }
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
                if (ImGui::Selectable("Textures", g_SelectedAssetCategory == AssetCategory::Textures))
                    g_SelectedAssetCategory = AssetCategory::Textures;
                if (ImGui::Selectable("Materials", g_SelectedAssetCategory == AssetCategory::Materials))
                    g_SelectedAssetCategory = AssetCategory::Materials;
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
                const bool showTextures = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Textures;
                const bool showMaterials = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Materials;

                if (showPrimitives)
                {
                    DrawAssetSectionHeader("Primitives");
                    int primitiveTileIndex = 0;
                    auto drawPrimitive = [&](const char* assetId, const char* label, const char* payloadType)
                    {
                        if (!matchesFilter(label))
                            return;
                        if (primitiveTileIndex > 0 && (primitiveTileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);
                        const ImTextureID thumb = GetAssetThumbnailTexture(assetId, "Assets/Textures/crate.png");
                        if (DrawAssetTile(assetId, label, ICON_FA_CUBE, payloadType, label, std::strlen(label) + 1u, g_SelectedAssetId == assetId, thumb))
                            g_SelectedAssetId = assetId;
                        ++primitiveTileIndex;
                    };

                    drawPrimitive("PrimitiveCube", "Cube", kSceneCreateCubePayload);
                    drawPrimitive("PrimitiveSphere", "Sphere", kSceneCreateSpherePayload);
                    drawPrimitive("PrimitivePlane", "Plane", kSceneCreatePlanePayload);
                    drawPrimitive("PrimitiveCylinder", "Cylinder", kSceneCreateCylinderPayload);

                    if (primitiveTileIndex == 0)
                        ImGui::TextDisabled("No primitive assets match the filter.");
                }

                if (showPrefabs)
                {
                    DrawAssetSectionHeader("Prefabs");
                    const auto prefabAssets = EnumeratePrefabAssets();
                    const std::filesystem::path prefabRoot = std::filesystem::path("Assets") / "Prefabs";
                    int tileIndex = 0;
                    bool anyPrefabShown = false;
                    for (const auto& prefabPath : prefabAssets)
                    {
                        const std::string label = prefabPath.lexically_relative(prefabRoot).generic_string();
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
                            ImGui::TextDisabled("No prefabs were found in Assets/Prefabs.");
                        else
                            ImGui::TextDisabled("No prefab assets match the filter.");
                    }
                }

                if (showTextures)
                {
                    DrawAssetSectionHeader("Textures");
                    const auto textureAssets = EnumerateTextureAssets();
                    const std::filesystem::path assetRoot = std::filesystem::path("Assets");
                    int tileIndex = 0;
                    bool anyTextureShown = false;
                    for (const auto& texturePath : textureAssets)
                    {
                        const std::string label = texturePath.lexically_relative(assetRoot).generic_string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string texturePathString = texturePath.string();
                        const ImTextureID textureThumb = GetAssetThumbnailTexture(texturePathString, texturePathString);
                        if (DrawAssetTile(texturePathString.c_str(), label.c_str(), ICON_FA_FOLDER_OPEN, kAssetTexturePayload, nullptr, 0, g_SelectedAssetId == texturePathString, textureThumb))
                            g_SelectedAssetId = texturePathString;
                        ++tileIndex;
                        anyTextureShown = true;
                    }

                    if (!anyTextureShown)
                    {
                        if (textureAssets.empty())
                            ImGui::TextDisabled("No textures were found under Assets.");
                        else
                            ImGui::TextDisabled("No texture assets match the filter.");
                    }
                }

                if (showMaterials)
                {
                    DrawAssetSectionHeader("Materials");
                    MaterialManager& materialManager = MaterialManager::GetInstance();
                    int tileIndex = 0;
                    bool anyMaterialShown = false;
                    extern Engine* g_engineInstance;
                    for (int i = 0; i < materialManager.GetMaterialCount(); ++i)
                    {
                        const std::string* materialNamePtr = materialManager.GetMaterialNameByIndex(i);
                        Material* material = materialManager.GetMaterialByIndex(i);
                        if (!materialNamePtr || !material)
                            continue;

                        const std::string label = *materialNamePtr;
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string materialId = std::format("Material:{}", label);
                        const std::string thumbPath = (material->albedo && !material->albedo->sourcePath.empty()) ? material->albedo->sourcePath : "Assets/Textures/crate.png";
                        const ImTextureID materialThumb = GetAssetThumbnailTexture(materialId, thumbPath);
                        if (DrawAssetTile(materialId.c_str(), label.c_str(), ICON_FA_GEAR, kAssetMaterialPayload, nullptr, 0, g_SelectedAssetId == materialId, materialThumb))
                        {
                            g_SelectedAssetId = materialId;
                            if (g_engineInstance)
                            {
                                EditorState& editor = g_engineInstance->GetEditorState();
                                editor.focusedMaterialIndex = i;
                                Inspector().open = true;
                                MaterialPreview().open = true;
                            }
                        }
                        ++tileIndex;
                        anyMaterialShown = true;
                    }

                    if (!anyMaterialShown)
                        ImGui::TextDisabled("No material assets match the filter.");
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
                extern Engine* g_engineInstance;
                EditorState* editor = g_engineInstance ? &g_engineInstance->GetEditorState() : nullptr;
                Graphics& gfx = Graphics::GetInstance();
                SceneCamera& sceneCamera = gfx.GetSceneCamera();
                const SceneStats stats = Scene::GetLastStats();
                DirectX::XMFLOAT3 cameraTarget{};
                DirectX::XMStoreFloat3(&cameraTarget, sceneCamera.GetTarget());
                const SceneInstance* selected = Scene::GetSelectedInstance();

                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::TextUnformatted("Debug Overlay");
                if (g_UIFontBold) ImGui::PopFont();
                ImGui::Separator();

                const float fps = ImGui::GetIO().Framerate;
                const float frameMs = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("Frame Time: %.2f ms", frameMs);
                ImGui::Separator();

                ImGui::Text("Scene Objects: %u", stats.totalObjects);
                ImGui::Text("Visible Objects: %u", stats.visibleObjects);
                ImGui::Text("Draw Calls: %u", stats.drawCalls);
                ImGui::Text("Selected Count: %zu", Scene::GetSelectedInstanceIds().size());
                ImGui::Text("Hovered Instance ID: %u", Scene::GetHoveredInstanceId());
                ImGui::Separator();

                ImGui::Text("Selection: %s", selected ? (selected->name.empty() ? "(unnamed)" : selected->name.c_str()) : "None");
                if (selected)
                {
                    const char* primitiveLabel = "Cube";
                    switch (selected->primitive)
                    {
                    case ScenePrimitive::Sphere: primitiveLabel = "Sphere"; break;
                    case ScenePrimitive::Plane: primitiveLabel = "Plane"; break;
                    case ScenePrimitive::Cylinder: primitiveLabel = "Cylinder"; break;
                    case ScenePrimitive::Cube:
                    default: primitiveLabel = "Cube"; break;
                    }
                    ImGui::Text("Primitive: %s", primitiveLabel);
                    ImGui::Text("Position: %.2f %.2f %.2f", selected->position.x, selected->position.y, selected->position.z);
                }
                ImGui::Separator();

                ImGui::Text("Camera Freelook: %s", sceneCamera.IsFreelooking() ? "Yes" : "No");
                ImGui::Text("Camera Yaw/Pitch: %.3f / %.3f", sceneCamera.GetYaw(), sceneCamera.GetPitch());
                ImGui::Text("Camera Distance: %.3f", sceneCamera.GetDistance());
                ImGui::Text("Camera Target: %.2f %.2f %.2f", cameraTarget.x, cameraTarget.y, cameraTarget.z);
                ImGui::Separator();

                if (editor)
                {
                    ImGui::Text("Black Flame Access: %s", editor->currentBlackFlameAccess == BlackFlameAccessLevel::Admin ? "Admin" : "User");
                    ImGui::Text("Black Flame Mode: %s",
                        editor->blackFlameAI.GetMode() == BlackFlameMode::Conversation ? "Conversation" :
                        (editor->blackFlameAI.GetMode() == BlackFlameMode::Hybrid ? "Hybrid" : "Engine"));
                    ImGui::Text("Black Flame State: %d", (int)editor->blackFlameAI.GetState());
                    ImGui::Text("Undo / Redo: %zu / %zu", editor->undoSceneSnapshots.size(), editor->redoSceneSnapshots.size());
                }
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
        if (assetId.empty() && fallbackTexturePath.empty())
            return 0;

        const std::string cacheKey = assetId.empty() ? fallbackTexturePath : assetId;
        if (auto it = g_AssetThumbnailCache.find(cacheKey); it != g_AssetThumbnailCache.end())
            return it->second;

        auto tryLoadTexture = [](const std::string& path) -> ImTextureID
        {
            if (path.empty())
                return 0;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec)
                return 0;
            if (Texture* texture = TextureManager::GetInstance().LoadTexture(path))
            {
                if (texture->srvGPU.ptr != 0)
                    return (ImTextureID)texture->srvGPU.ptr;
            }
            return 0;
        };

        ImTextureID thumbnail = 0;
        thumbnail = tryLoadTexture(assetId);
        if (!thumbnail)
            thumbnail = tryLoadTexture(fallbackTexturePath);
        if (!thumbnail && assetId != fallbackTexturePath)
            thumbnail = tryLoadTexture("Assets/Textures/crate.png");

        g_AssetThumbnailCache[cacheKey] = thumbnail;
        return thumbnail;
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
        const char* typeLabel = "Asset";
        if (payloadType && std::strcmp(payloadType, kSceneCreateCubePayload) == 0)
            typeLabel = "Primitive";
        else if (payloadType && std::strcmp(payloadType, kSceneCreatePrefabPayload) == 0)
            typeLabel = "Prefab";
        else if (payloadType && std::strcmp(payloadType, kAssetTexturePayload) == 0)
            typeLabel = "Texture";
        else if (payloadType && std::strcmp(payloadType, kAssetMaterialPayload) == 0)
            typeLabel = "Material";
        const ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);
        const ImVec2 typePos(min.x + 10.0f, max.y - typeSize.y - 8.0f);

        draw->AddText(titlePos, textColor, title);
        draw->AddText(typePos, subTextColor, typeLabel);

        if (titleSize.x > (kTileWidth - 20.0f))
            draw->AddLine(ImVec2(min.x + 10.0f, min.y + 74.0f), ImVec2(max.x - 10.0f, min.y + 74.0f), IM_COL32(255, 255, 255, 18), 1.0f);

        if (payloadType && payloadData && payloadSize > 0 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
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
        const std::filesystem::path assetRoot = std::filesystem::path("Assets");
        if (!std::filesystem::exists(assetRoot))
            return result;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
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
