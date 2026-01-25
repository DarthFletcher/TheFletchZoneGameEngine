#include "EditorPanels.h"

#include "imgui.h"
#include "Graphics.h"
#include "Logger.h"

#include <format>
#include <string>

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
                const UINT w = (UINT)(size.x > 1.0f ? size.x : 1.0f);
                const UINT h = (UINT)(size.y > 1.0f ? size.y : 1.0f);

                gfx.RequestSceneRenderTargetResize(w, h);

                ImTextureID tex = gfx.GetSceneImGuiTextureID();
                if (tex)
                {
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

                    if (hovered || focused)
                    {
                        ImGuiIO& io = ImGui::GetIO();

                        const bool precision = io.KeyShift;
                        const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
                        const float wheel = io.MouseWheel;

                        // Avoid fighting ImGui when it wants to capture mouse (e.g. dragging other widgets).
                        if (!io.WantCaptureMouse)
                        {
                            float wheelDelta = 0.0f;
                            if (wheel != 0.0f)
                                wheelDelta = wheel * 120.0f; // match WM_MOUSEWHEEL units

                            const bool orbit = (gfx.GetViewMode() == ViewMode::Mode3D) ? rmb : false;
                            const bool pan = mmb;

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

    EditorPanel& Scene() { return g_scene; }
    EditorPanel& Hierarchy() { return g_hierarchy; }
    EditorPanel& Inspector() { return g_inspector; }
    EditorPanel& Assets() { return g_assets; }
    EditorPanel& DebugOverlay() { return g_debugOverlay; }
    EditorPanel& Diagnostics() { return g_diagnostics; }
    EditorPanel& LogViewer() { return g_logViewer; }

    void DrawAll()
    {
        if (g_scene.open) g_scene.draw();
        if (g_hierarchy.open) g_hierarchy.draw();
        if (g_inspector.open) g_inspector.draw();
        if (g_assets.open) g_assets.draw();
        if (g_debugOverlay.open) g_debugOverlay.draw();
        if (g_diagnostics.open) g_diagnostics.draw();
        if (g_logViewer.open) g_logViewer.draw();
    }
}
