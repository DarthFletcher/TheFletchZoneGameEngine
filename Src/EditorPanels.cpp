#include "EditorPanels.h"

#include "imgui.h"
#include "EditorCommon.h"
#include "Graphics.h"
#include "Logger.h"
#include "EditorState.h"
#include "Engine.h"

#include <format>
#include <string>
#include <chrono>

namespace EditorPanels
{
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

    static EditorPanel g_scene{
        "Scene",
        true,
        []()
        {
            auto& panel = Scene();
            if (!panel.open)
                return;

            auto& gfx = Graphics::GetInstance();

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
                    if (ImGui::Selectable("3D", sel3d)) gfx.SetViewMode(ViewMode::Mode3D);
                    if (ImGui::Selectable("2D", sel2d)) gfx.SetViewMode(ViewMode::Mode2D);
                    ImGui::EndCombo();
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
                if (tex)
                {
                    if (ScenePanelDiag_ShouldLog())
                    {
                        // NOTE: In this codebase ImTextureID is treated as a D3D12 CPU descriptor handle ptr.
                        Logger::Log(LogLevel::Debug, std::format(
                            "[SceneDiag] Scene panel ImGui::Image tex=0x{:X} size=({:.1f},{:.1f}) rtSize=({:.0f},{:.0f})",
                            (UINT64)tex, size.x, size.y,
                            gfx.GetSceneRenderTargetSize().x, gfx.GetSceneRenderTargetSize().y));
                    }

                    // Note: UVs flipped vertically for DX12 texture coordinates
                    ImGui::Image(tex, size, ImVec2(0, 1), ImVec2(1, 0));

                    const bool hovered = ImGui::IsItemHovered();
                    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                    // Cache scene image rect for picking
                    const ImVec2 sceneMin = ImGui::GetItemRectMin();

                    // Scene picking (LMB) - preserve camera-nav priority (RMB orbit / MMB pan)
                    if (hovered)
                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool lmbClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

                        if (!io.WantCaptureMouse && lmbClicked && !rmbDown && !mmbDown)
                        {
                            DirectX::XMFLOAT3 hit{};
                            PickRay ray{};

                            const bool hitOk = (gfx.GetViewMode() == ViewMode::Mode2D)
                                ? gfx.TryPickSceneGridZ0(io.MousePos, sceneMin, size, hit, &ray)
                                : gfx.TryPickSceneGridY0(io.MousePos, sceneMin, size, hit, &ray);

                            if (hitOk)
                            {
                                Logger::Log(LogLevel::Info, std::format(
                                    "Pick hit: ({:.3f}, {:.3f}, {:.3f}) | ray o=({:.3f},{:.3f},{:.3f}) d=({:.3f},{:.3f},{:.3f})",
                                    hit.x, hit.y, hit.z,
                                    ray.origin.x, ray.origin.y, ray.origin.z,
                                    ray.dir.x, ray.dir.y, ray.dir.z));
                            }
                            else
                            {
                                Logger::Log(LogLevel::Info, "Pick miss (no plane intersection)");
                            }
                        }
                    }

                    // Camera input gate: only when interacting with the Scene image OR the Scene window is focused.
                    // (Prevents input feeling "stuck" when the image hover detection is flaky.)
                    if (hovered || focused)
                    {
                        ImGuiIO& io = ImGui::GetIO();

                        // Avoid fighting ImGui when it wants to capture mouse.
                        if (!io.WantCaptureMouse)
                        {
                            extern Engine* g_engineInstance;
                            EditorState& editor = g_engineInstance->GetEditorState();

                            const bool precision = io.KeyShift;

                            const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                            const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                            const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

                            const bool alt = io.KeyAlt;
                            const bool ctrl = io.KeyCtrl;

                            float wheelDelta = 0.0f;
                            if (io.MouseWheel != 0.0f)
                                wheelDelta = io.MouseWheel * 120.0f; // match WM_MOUSEWHEEL units

                            bool orbit = false;
                            bool pan = false;
                            bool dolly = false;

                            // Map nav mode -> orbit/pan/dolly inputs
                            switch (editor.cameraNavMode)
                            {
                            case CameraNavMode::Unity_AltMouse:
                                orbit = alt && lmb && (gfx.GetViewMode() == ViewMode::Mode3D);
                                pan = alt && mmb;
                                dolly = alt && rmb;
                                break;

                            case CameraNavMode::Blender_MMB:
                                orbit = mmb && !precision && !ctrl && (gfx.GetViewMode() == ViewMode::Mode3D);
                                pan = mmb && precision;
                                dolly = mmb && ctrl;
                                break;

                            case CameraNavMode::TFZ_RMB:
                            default:
                                orbit = rmb && !ctrl && (gfx.GetViewMode() == ViewMode::Mode3D);
                                pan = mmb;
                                dolly = rmb && ctrl;
                                break;
                            }

                            const bool navActive = orbit || pan || dolly || (wheelDelta != 0.0f);

                            // If camera nav is active, cancel gizmo dragging and prevent starting LMB selection/drag.
                            if (navActive)
                            {
                                if (editor.gizmo.dragging)
                                    editor.gizmo.Reset();
                            }

                            // Never allow LMB picking to be interpreted as camera nav.
                            // Also prevents accidental pan while clicking.
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                                pan = false;

                            // Dolly is implemented by converting mouse vertical delta into wheel-like zoom.
                            // (Keeps the camera logic centralized in SceneCamera::UpdateOrbit())
                            if (dolly)
                            {
                                const float dollyScale = precision ? 0.75f : 1.5f;
                                wheelDelta += (-io.MouseDelta.y) * dollyScale * 120.0f;
                            }

                            gfx.GetSceneCamera().UpdateOrbit(io.MouseDelta.x, io.MouseDelta.y, wheelDelta, orbit, pan, precision);
                        }
                    }
                }
                else
                {
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

                SceneCamera& cam = gfx.GetSceneCamera();
                DirectX::XMFLOAT3 targ{};
                DirectX::XMStoreFloat3(&targ, cam.GetTarget());

                ImGui::Text("Camera Yaw:   %.3f", cam.GetYaw());
                ImGui::Text("Camera Pitch: %.3f", cam.GetPitch());
                ImGui::Text("Camera Dist:  %.3f", cam.GetDistance());
                ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)", targ.x, targ.y, targ.z);
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
                ImGui::Text("\xef\x8c\xb3 Scene Hierarchy");
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
                ImGui::Text("\xef\x94\x8d Inspector");
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
                ImGui::Text("\xef\x93\xa6 Asset Browser");
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

            // Keep these statics in the panel so Scene stays render/data-only.
            static int s_targetInstanceCount = 25;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                ImGui::SliderInt("Instance Count", &s_targetInstanceCount, 1, 10000);

                // Persist the slider value for Scene: use ImGui storage as a simple cross-module bridge.
                ImGuiStorage* store = ImGui::GetStateStorage();
                if (store)
                    store->SetInt(ImGui::GetID("TFZ_Instancing_TargetCount"), s_targetInstanceCount);

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
