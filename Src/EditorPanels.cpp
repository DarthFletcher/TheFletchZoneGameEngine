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

#include <format>
#include <string>
#include <chrono>
#include <cstring>

namespace EditorPanels
{
    static EditorGizmo g_EditorGizmo;
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
                    // Note: UVs flipped vertically for DX12 texture coordinates
                    ImGui::Image(tex, size, ImVec2(0, 1), ImVec2(1, 0));

                    const bool hovered = ImGui::IsItemHovered();
                    const bool viewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                    // Cache scene image rect for picking
                    const ImVec2 sceneMin = ImGui::GetItemRectMin();
                    const ImVec2 overlayPos = { sceneMin.x + 10.0f, sceneMin.y + 10.0f };

                    const char* modeText = "Translate";
                    switch (g_EditorGizmo.GetMode())
                    {
                    case EditorGizmo::Mode::Rotate: modeText = "Rotate"; break;
                    case EditorGizmo::Mode::Scale: modeText = "Scale"; break;
                    case EditorGizmo::Mode::Translate:
                    default: modeText = "Translate"; break;
                    }

                    const char* spaceText = (g_EditorGizmo.GetSpace() == EditorGizmo::Space::World) ? "World" : "Local";
                    const char* projectionText = (sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective) ? "Perspective" : "Orthographic";
                    const char* viewText = sceneCamera.Is2DMode() ? "2D" : "3D";

                    const bool freelooking = gfx.GetSceneCamera().IsFreelooking();
                    char gizmoHud[320] = {};
                    if (g_EditorGizmo.HasActiveRotationFeedback())
                    {
                        const char axisChar = (g_EditorGizmo.GetActiveAxis() == 0) ? 'X' : (g_EditorGizmo.GetActiveAxis() == 1) ? 'Y' : 'Z';
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s\nRotate %c: %.1f deg",
                            modeText,
                            spaceText,
                            ImGui::GetIO().KeyCtrl ? "ON" : "OFF",
                            freelooking ? "Fly" : (sceneCamera.Is2DMode() ? "Pan" : "Idle"),
                            projectionText,
                            viewText,
                            axisChar,
                            g_EditorGizmo.GetActiveRotationDegrees());
                    }
                    else
                    {
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s",
                            modeText,
                            spaceText,
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
                        DirectX::XMFLOAT3 selectionCenter{};
                        const bool hasSelectionCenter = Scene::TryGetSelectionCenter(selectionCenter);
                        if (hasSelectionCenter)
                        {
                            CameraData pivotCamera{};
                            ImVec2 pivotScreen{};
                            if (Scene::TryGetLastRenderCameraData(pivotCamera) &&
                                ProjectWorldToSceneViewport(selectionCenter, pivotCamera, sceneMin, size, pivotScreen))
                            {
                                overlayDraw->AddCircleFilled(pivotScreen, 4.0f, IM_COL32(255, 230, 80, 220));
                                overlayDraw->AddCircle(pivotScreen, 7.0f, IM_COL32(255, 230, 80, 180), 16, 1.5f);
                            }
                        }
                    }

                    ImGui::SetCursorScreenPos(ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f));
                    if (ImGui::SmallButton(g_EditorGizmo.GetSpace() == EditorGizmo::Space::World ? "World##GizmoSpace" : "Local##GizmoSpace"))
                        g_EditorGizmo.ToggleSpace();

                    if (focused && !Engine::IsKeyboardCapturedByUI())
                    {
                        if (ImGui::IsKeyPressed(ImGuiKey_W))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Translate);
                        if (ImGui::IsKeyPressed(ImGuiKey_E))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Rotate);
                        if (ImGui::IsKeyPressed(ImGuiKey_R))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Scale);

                        if (ImGui::IsKeyPressed(ImGuiKey_F))
                        {
                            DirectX::XMFLOAT3 focusPoint{};
                            if (Scene::TryGetSelectionCenter(focusPoint))
                                gfx.GetSceneCamera().FocusOnPoint(focusPoint, 6.0f);
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                            Scene::DeleteSelectedInstance();

                        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
                            Scene::DuplicateSelectedInstance();
                    }

                    {
                        ImGuiIO& io = ImGui::GetIO();
                        extern Engine* g_engineInstance;
                        EditorState& editor = g_engineInstance->GetEditorState();
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
                                g_EditorGizmo.Update(
                                    selected,
                                    gizmoCamera,
                                    ImGui::GetWindowDrawList(),
                                    sceneMin,
                                    size,
                                    hovered,
                                    allowGizmoInput);

                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                        }
                    }

                    // Scene picking (LMB) - preserve camera-nav priority (RMB freelook)
                    if (hovered)
                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
                        extern Engine* g_engineInstance;
                        EditorState& editor = g_engineInstance->GetEditorState();
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
                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text("\xef\x8c\xb3 Scene Hierarchy");
                if (g_UIFontBold) ImGui::PopFont();

                ImGui::Separator();
                const auto& instances = Scene::GetInstances();
                for (int i = 0; i < static_cast<int>(instances.size()); ++i)
                {
                    const SceneInstance& instance = instances[(size_t)i];
                    const bool isSelected = Scene::IsInstanceSelected(instance.instanceId);
                    if (ImGui::Selectable(instance.name.c_str(), isSelected))
                    {
                        if (ImGui::GetIO().KeyShift)
                            Scene::ToggleSelectedInstanceId(instance.instanceId);
                        else
                            Scene::SetSelectedInstanceId(instance.instanceId);
                    }
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
                ImGui::Text("\xef\x94\x8d Inspector");
                if (g_UIFontBold) ImGui::PopFont();

                if (SceneInstance* selected = Scene::GetSelectedInstance())
                {
                    char nameBuffer[128] = {};
                    strncpy_s(nameBuffer, selected->name.c_str(), sizeof(nameBuffer) - 1);

                    ImGui::Separator();
                    ImGui::Text("Object ID: %u", selected->instanceId);
                    if (ImGui::InputText("Name", nameBuffer, IM_ARRAYSIZE(nameBuffer)))
                        selected->name = nameBuffer;

                    ImGui::InputFloat3("Position", &selected->position.x);
                    ImGui::InputFloat3("Rotation", &selected->rotation.x);
                    ImGui::InputFloat3("Scale", &selected->scale.x);
                    ImGui::Checkbox("Visible", &selected->visible);
                    ImGui::Text("Material Index: %d", selected->materialIndex);

                    if (Material* material = MaterialManager::GetInstance().GetMaterial("TestMaterial"))
                    {
                        ImGui::Separator();
                        ImGui::Text("Material: %s", material->name.c_str());
                        ImGui::Text("Albedo: %s", material->albedo ? "crate.png" : "<none>");
                        ImGui::Text("Metallic: %.2f", material->metallic);
                        ImGui::Text("Roughness: %.2f", material->roughness);
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
                ImGui::Text("\xef\x93\xa6 Asset Browser");
                if (g_UIFontBold) ImGui::PopFont();
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
}
