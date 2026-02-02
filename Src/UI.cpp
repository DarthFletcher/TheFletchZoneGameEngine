#include "UI.h"
#include "Logger.h"
#include "Utils.h"
#include <Windows.h>
#include <string>
#include <Engine.h>
#include <Graphics.h>
#include "SplashScreen.h"
#include "EditorPanels.h"
#include "EditorState.h"
#include "EditorIcons.h"

extern Engine* g_engineInstance;

namespace UI {

    // 🔧 Current theme tracking
    static Theme currentTheme = Theme::Dark;

    static bool g_showImGuiDemo = false;
    static bool g_showImGuiMetrics = false;
    static bool g_showFrameDiag = false;

    // Command strip visibility (treated like a panel toggle)
    static bool g_showCommandStrip = true;

    static float g_commandStripHeight = 32.0f;

    // Frame diagnostics for editor shell ordering/visibility
    static int g_editorShellFrameCounter = 0;
    static bool g_dockspaceTouchedThisFrame = false;
    static bool g_commandStripTouchedThisFrame = false;

    bool IsImGuiDemoVisible() { return g_showImGuiDemo; }
    bool IsImGuiMetricsVisible() { return g_showImGuiMetrics; }

    static float GetCommandStripReservedHeight()
    {
        if (!g_showCommandStrip)
            return 0.0f;

        // Ensure the reserved area always fits the strip contents.
        const float minH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
        return (std::max)(g_commandStripHeight, minH);
    }

    namespace
    {
        static void LogImGuiSecondaryWindows()
        {
            ImGuiIO& io = ImGui::GetIO();
            if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
                return;

            static int s_counter = 0;
            if ((++s_counter % 120) != 0) // ~2 sec at 60fps
                return;

            ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
            ImGuiContext* ctx = ImGui::GetCurrentContext();
            const ImGuiViewport* mainVp = ImGui::GetMainViewport();
            const ImGuiID mainId = mainVp ? mainVp->ID : 0;

            Logger::Log(LogLevel::Info, std::format("🪟 ImGui Viewport Summary | Total={} Frame={}", pio.Viewports.Size, ImGui::GetFrameCount()));

            for (int i = 0; i < pio.Viewports.Size; ++i)
            {
                ImGuiViewport* vp = pio.Viewports[i];
                if (!vp)
                    continue;

                const bool isMain = (vp->ID == mainId);
                const uintptr_t hwnd = reinterpret_cast<uintptr_t>(vp->PlatformHandle);

                Logger::Log(LogLevel::Info, std::format(
                    "  - Viewport[{}] ID=0x{:X} Main={} HWND=0x{:X} Pos=({:.0f},{:.0f}) Size=({:.0f},{:.0f}) Dpi={:.2f}",
                    i, vp->ID, isMain, hwnd, vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y, vp->DpiScale));
            }

            if (ctx)
            {
                Logger::Log(LogLevel::Info, std::format("🧩 ImGui Windows | Count={} (showing top-level OS viewport assignment)", ctx->Windows.Size));
                for (int i = 0; i < ctx->Windows.Size; ++i)
                {
                    ImGuiWindow* w = ctx->Windows[i];
                    if (!w || w->Hidden)
                        continue;

                    ImGuiViewport* wvp = w->Viewport;
                    if (!wvp)
                        continue;

                    const bool isSecondary = (mainVp && wvp->ID != mainVp->ID);
                    if (!isSecondary)
                        continue;

                    Logger::Log(LogLevel::Info, std::format(
                        "    * Window '{}' -> ViewportID=0x{:X} HWND=0x{:X}",
                        w->Name, wvp->ID, reinterpret_cast<uintptr_t>(wvp->PlatformHandle)));
                }
            }
        }
    }

    void DrawEditorShell()
    {
        // Reset per-frame flags
        ++g_editorShellFrameCounter;
        g_dockspaceTouchedThisFrame = false;
        g_commandStripTouchedThisFrame = false;

        UI::BeginDockSpace();
        UI::DrawToolbar();
        UI::DrawEditorPanels();
        UI::DrawOverlays();
        UI::EndDockSpaceFrame();

        if (UI::IsImGuiDemoVisible())
            ImGui::ShowDemoWindow();
        if (UI::IsImGuiMetricsVisible())
            ImGui::ShowMetricsWindow();

        // Log secondary viewport/windows mapping
        LogImGuiSecondaryWindows();

        // Diagnostic window (dockable) to confirm execution order.
        if (g_showFrameDiag)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const ImVec2 vpPos = vp ? vp->Pos : ImVec2(0, 0);
            const ImVec2 vpSize = vp ? vp->Size : ImVec2(0, 0);
            const ImVec2 workPos = vp ? vp->WorkPos : ImVec2(0, 0);
            const ImVec2 workSize = vp ? vp->WorkSize : ImVec2(0, 0);

            if (ImGui::Begin("Frame Diagnostics", &g_showFrameDiag))
            {
                ImGui::Text("Frame: %d", g_editorShellFrameCounter);
                ImGui::Text("BeginDockSpace(): %s", g_dockspaceTouchedThisFrame ? "YES" : "NO");
                ImGui::Text("DrawCommandStrip(): %s", g_commandStripTouchedThisFrame ? "YES" : "NO");
                ImGui::Separator();
                ImGui::Text("Viewport Pos: (%.0f, %.0f) Size: (%.0f, %.0f)", vpPos.x, vpPos.y, vpSize.x, vpSize.y);
                ImGui::Text("WorkPos:     (%.0f, %.0f) WorkSize: (%.0f, %.0f)", workPos.x, workPos.y, workSize.x, workSize.y);
            }
            ImGui::End();
        }
    }

	// 🗄️ ImGui .ini layout persistence
    void LoadLayoutFromDisk(const char* iniPath)
    {
        if (!iniPath || !*iniPath)
            return;

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = iniPath;

        // Safe even if the file doesn't exist: ImGui will just keep defaults.
        ImGui::LoadIniSettingsFromDisk(iniPath);
        Logger::Log(LogLevel::Info, std::string("📄 Loaded ImGui layout: ") + iniPath);
    }

	// 🗄️ ImGui .ini layout persistence
    void SaveLayoutToDisk(const char* iniPath)
    {
        if (!iniPath || !*iniPath)
            return;

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = iniPath;

        ImGui::SaveIniSettingsToDisk(iniPath);
        Logger::Log(LogLevel::Info, std::string("💾 Saved ImGui layout: ") + iniPath);
    }

    // 🎨 Synthwave theme implementation
    static void ApplySynthwaveStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        style.WindowRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.ScrollbarSize = 16.0f;

        colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.02f, 0.18f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.85f, 0.25f, 1.00f, 0.55f);
        colors[ImGuiCol_Header] = ImVec4(0.8f, 0.1f, 0.9f, 0.45f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.9f, 0.3f, 1.0f, 0.6f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.9f, 0.5f, 1.0f, 0.8f);

        colors[ImGuiCol_Button] = ImVec4(0.7f, 0.2f, 0.9f, 0.5f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.9f, 0.4f, 1.0f, 0.8f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.00f, 0.30f, 0.7f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.00f, 0.45f, 0.9f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.5f, 0.0f, 0.6f, 1.0f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.00f, 0.20f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.3f, 0.0f, 0.5f, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.00f, 0.10f, 0.75f);

        colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.6f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.8f, 0.3f, 1.0f, 0.9f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.4f, 1.0f, 1.0f);

        Logger::Log(LogLevel::Info, "🌈 Synthwave theme applied");
    }

    static void ApplyMagentaStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        style.WindowRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.ScrollbarSize = 14.0f;

        colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.00f, 0.15f, 1.00f); // Dark magenta background
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.00f, 0.22f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(1.00f, 0.25f, 0.90f, 0.45f);
        colors[ImGuiCol_Header] = ImVec4(1.0f, 0.2f, 0.8f, 0.5f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.4f, 0.9f, 0.75f);
        colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_Button] = ImVec4(0.8f, 0.1f, 0.7f, 0.6f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.3f, 0.9f, 0.85f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.30f, 0.00f, 0.40f, 0.7f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.45f, 0.00f, 0.55f, 0.9f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.6f, 0.0f, 0.7f, 1.0f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.00f, 0.25f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.4f, 0.0f, 0.5f, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.00f, 0.15f, 0.75f);

        colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.3f, 1.0f, 0.8f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.4f, 1.0f, 1.0f);

        Logger::Log(LogLevel::Info, "💖 Magenta theme applied");
    }


    // 🎨 Theme switcher
    void ApplyTheme(Theme theme) {
        currentTheme = theme;

        switch (theme) {
        case Theme::Dark:      ImGui::StyleColorsDark();    break;
        case Theme::Light:     ImGui::StyleColorsLight();   break;
        case Theme::Classic:   ImGui::StyleColorsClassic(); break;
        case Theme::Synthwave: ApplySynthwaveStyle();       break;
        case Theme::Magenta:   ApplyMagentaStyle();         break;
        }

        // 🛡️ Clamp alpha to prevent invalid values
        ImGuiStyle& style = ImGui::GetStyle();
        style.Alpha = std::clamp(style.Alpha, 0.85f, 1.0f);

        Logger::Log(LogLevel::Info, "🎨 Theme applied.");
    }

	// 🪟 Set main window size and center on screen
    void SetMainWindowSize(int width, int height)
    {
        HWND hWnd = Graphics::GetInstance().GetHWND(); // Or pass in HWND directly if preferred
        RECT rect = { 0, 0, width, height };

        // Adjust for window borders
        DWORD style = GetWindowLong(hWnd, GWL_STYLE);
        DWORD exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        // Center on screen
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;
        int posX = (screenWidth - windowWidth) / 2;
        int posY = (screenHeight - windowHeight) / 2;

        SetWindowPos(hWnd, nullptr, posX, posY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

	// 🎨 Get clear color based on current theme
    ImVec4 GetClearColor() {
        switch (currentTheme) {
        case Theme::Magenta:   return ImVec4(0.10f, 0.00f, 0.12f, 1.0f);
        case Theme::Synthwave: return ImVec4(0.08f, 0.04f, 0.16f, 1.0f);
        case Theme::Dark:      return ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        case Theme::Light:     return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        case Theme::Classic:   return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
        default:               return ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        }
    }


    // 🖥️ GPU Dropdown Menu
    void ShowGPUSelectionMenu(const std::vector<std::wstring>& gpuList, int& selectedGPUIndex) {
        for (size_t i = 0; i < gpuList.size(); ++i) {
            std::string gpuName = Utils::WideStringToString(gpuList[i]);
            bool isSelected = (selectedGPUIndex == static_cast<int>(i));
            if (ImGui::MenuItem(gpuName.c_str(), nullptr, isSelected)) {
                selectedGPUIndex = static_cast<int>(i);
                GPUSelection::SelectGPU(selectedGPUIndex);
                Logger::Log(LogLevel::Info, "🎮 Switched to GPU index: " + std::to_string(i));
            }
        }
    }

    // 🛠️ Command Strip at Top
    // Refactored: command strip is rendered as a child region inside BeginDockSpace().
    void UI::DrawCommandStrip()
    {
        g_commandStripTouchedThisFrame = true;
    }

    // 🛋️ Dockspace layout management
    namespace
    {
        // Forward declare so BeginDockSpace can call it.
        static void DrawCommandStripContents();

         ImGuiID g_dockspaceID = 0;
         bool g_dockInitialized = false;
         bool g_requestResetLayout = false;
         bool g_buildLayoutNextFrame = false;

         void BuildDefaultDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

            ImGui::DockBuilderRemoveNode(dockspaceID);
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            
            // Match the DockSpace region below the command strip inside the dockspace root.
            const float toolbarH = GetCommandStripReservedHeight();
            const float topPadding = ImGui::GetStyle().ItemSpacing.y;

            // ImGui asserts dock node sizes are non-negative. Clamp aggressively to a safe minimum.
            constexpr float kMinDockDim = 64.0f;
            const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
            const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
            const ImVec2 dockSize = ImVec2(safeW, safeH);
            ImGui::DockBuilderSetNodeSize(dockspaceID, dockSize);

            ImGuiID dock_main = dockspaceID;
            ImGuiID dock_left = 0;
            ImGuiID dock_right = 0;
            ImGuiID dock_bottom = 0;

            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, &dock_right, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, &dock_bottom, &dock_main);

            // Names must match ImGui::Begin("...") titles
            ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
            ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
            ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
            ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);

            ImGui::DockBuilderFinish(dockspaceID);
         }

        // Command strip contents
        static void DrawCommandStripContents()
         {
             // Left
             if (ImGui::Button(ICON_FA_BARS " Project")) { /* popup later */ }
             ImGui::SameLine();
             if (ImGui::Button("Scene")) {}
             ImGui::SameLine();
             if (ImGui::Button("Build")) {}

             // Compute layout metrics
             const ImGuiStyle& style = ImGui::GetStyle();
             const float spacingX = style.ItemSpacing.x;
             const float framePadX = style.FramePadding.x;

             auto calcButtonW = [&](const char* label) -> float
             {
                 return ImGui::CalcTextSize(label).x + framePadX * 2.0f;
             };

             // Engine state
             const Engine::State s = Engine::GetState();
             const bool playing = (s == Engine::State::Playing);
             const bool paused = (s == Engine::State::Paused);

             // Use visible labels, but add hidden IDs to keep them unique/stable.
             const char* playLabel = ICON_FA_PLAY " Play##CmdPlay";
             const char* pauseLabel = ICON_FA_PAUSE " Pause##CmdPause";
             const char* stopLabel = ICON_FA_STOP " Stop##CmdStop";
             const char* settingsLabel = ICON_FA_GEAR " Settings##CmdSettings";

             // NOTE: width calculations must use the visible portion, not the hidden IDs.
             const char* playVisible = ICON_FA_PLAY " Play";
             const char* pauseVisible = ICON_FA_PAUSE " Pause";
             const char* stopVisible = ICON_FA_STOP " Stop";
             const char* settingsVisible = ICON_FA_GEAR " Settings";

             const float playW = calcButtonW(playVisible);
             const float pauseW = calcButtonW(pauseVisible);
             const float stopW = calcButtonW(stopVisible);
             const float centerGroupW = playW + pauseW + stopW + spacingX * 2.0f;

             const float settingsW = calcButtonW(settingsVisible);

             // Content region coordinates
             const float contentMinX = ImGui::GetWindowContentRegionMin().x;
             const float contentMaxX = ImGui::GetWindowContentRegionMax().x;
             const float contentW = (contentMaxX - contentMinX);

             // Determine where left group ends (relative to window)
             const float leftEndX = ImGui::GetCursorPosX();

             // Determine where right group should start so it is right aligned
             const float rightStartX = contentMaxX - settingsW;

             // Compute ideal center start
             float centerStartX = contentMinX + (contentW - centerGroupW) * 0.5f;

             // Clamp center group into the available middle span
             const float minCenterX = leftEndX + spacingX;
             const float maxCenterX = rightStartX - spacingX - centerGroupW;

             const bool hasRoomForCenter = (maxCenterX >= minCenterX);
             if (hasRoomForCenter)
             {
                 centerStartX = (std::max)(minCenterX, (std::min)(centerStartX, maxCenterX));
             }
             else
             {
                 // Not enough horizontal room for a centered group + right-aligned settings.
                 // Fall back: place center group right after left group; it may push towards the right,
                 // but we avoid SameLine() overlap artifacts.
                 centerStartX = minCenterX;
             }

             // Center group
             ImGui::SameLine(centerStartX);

             if (playing)
                 ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.70f, 0.20f, 1.0f));
             if (ImGui::Button(playLabel))
                 Engine::SetState(Engine::State::Playing);
             if (playing)
                 ImGui::PopStyleColor();

             ImGui::SameLine();

             if (paused)
                 ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.70f, 0.10f, 1.0f));
             if (ImGui::Button(pauseLabel))
                 Engine::SetState(Engine::State::Paused);
             if (paused)
                 ImGui::PopStyleColor();

             ImGui::SameLine();

             if (ImGui::Button(stopLabel))
                 Engine::SetState(Engine::State::Editing);

            // Right (only right-align if it won't overlap the center group)
            const float afterCenterX = ImGui::GetCursorPosX();
            if (afterCenterX + spacingX <= rightStartX)
                ImGui::SameLine(rightStartX);
            else
                ImGui::SameLine();

            (void)ImGui::Button(settingsLabel);
         }
     }

	// 🛋️ Dockspace Host with Menu Bar
    ImGuiID BeginDockSpace()
    {
        g_dockspaceTouchedThisFrame = true;

        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
            return 0;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return 0;

        // The dockspace host should follow the full main viewport client area.
        // Using WorkPos/WorkSize here can cause the host to shrink/shift during resize,
        // which looks like the UI is being squished/zoomed.
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        // Ensure the editor root visually owns the background.
        // (Without this, the swapchain clear color will show through.)
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_DockingEmptyBg] = style.Colors[ImGuiCol_WindowBg];

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("TFZ_DockSpaceRoot", nullptr, flags);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar())
        {
            UI::ShowMainMenu();
            ImGui::EndMenuBar();
        }

        if (g_dockspaceID == 0)
            g_dockspaceID = ImGui::GetID("TFZ_DockSpace");

        if (g_requestResetLayout)
        {
            ImGui::LoadIniSettingsFromMemory("");
            g_dockInitialized = false;
            g_buildLayoutNextFrame = true;
            g_requestResetLayout = false;
        }

        // Command strip child (sits under the menu bar).
        const float toolbarH = GetCommandStripReservedHeight();
        const float topPadding = ImGui::GetStyle().ItemSpacing.y;
        if (toolbarH > 0.0f)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            const ImVec4 bg = style.Colors[ImGuiCol_MenuBarBg];
            const ImVec4 border = style.Colors[ImGuiCol_Border];

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
            ImGui::PushStyleColor(ImGuiCol_Border, border);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPadding);
            if (ImGui::BeginChild("TFZ_CommandStripChild", ImVec2(0.0f, toolbarH), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar))
            {
                DrawCommandStripContents();
            }
            ImGui::EndChild();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        // Dockspace in remaining region below the command strip.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPadding);
        const ImVec2 dockPos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 dockSize = ImVec2((std::max)(0.0f, avail.x), (std::max)(0.0f, avail.y));

        // Constrain the dock node rectangle to the remaining region.
        ImGui::DockSpace(g_dockspaceID, dockSize, ImGuiDockNodeFlags_None);

        if (!g_dockInitialized)
        {
            g_dockInitialized = true;
            g_buildLayoutNextFrame = true;
        }

        ImGui::End();

        return g_dockspaceID;
    }

	// 🛋️ Finalize dockspace frame and apply pending layout if needed
    void EndDockSpaceFrame()
    {
        if (!g_buildLayoutNextFrame)
            return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        g_buildLayoutNextFrame = false;
        BuildDefaultDockLayout(g_dockspaceID, viewport);
    }

	// 🔄 Request layout reset on next frame
    void RequestResetLayout()
    {
        g_requestResetLayout = true;
    }

	// 🛠️ Draw all editor panels
    void UI::DrawEditorPanels()
    {
        EditorPanels::DrawAll();
    }

     // 🧭 Main Menu Bar
     void ShowMainMenu() {
         static bool vsyncEnabled = true;
         static int selectedGPUIndex = 0;
         static bool openAbout = false;
         static bool showCustomSizePopup = false;

         Logger::Log(LogLevel::Trace,
             std::format("🧭 MenuBar viewport ID=0x{:X}",
                 ImGui::GetWindowViewport()->ID));

         // 📁 File
         if (ImGui::BeginMenu("File")) {
             if (ImGui::MenuItem("Load", "Ctrl+L")) Logger::Log(LogLevel::Info, "📂 Load clicked");
             if (ImGui::MenuItem("Save", "Ctrl+S")) Logger::Log(LogLevel::Info, "💾 Save clicked");
             ImGui::Separator();
             if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
             ImGui::EndMenu();
         }

         // 🖥️ Graphics
         if (ImGui::BeginMenu("Graphics")) {
             ImGui::Text("Select GPU:");
             if (!GPUSelection::gpuList.empty()) {
                 std::string currentGPU = Utils::WideStringToString(GPUSelection::gpuList[selectedGPUIndex]);
                 ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "Current: %s", currentGPU.c_str());
             }
             ShowGPUSelectionMenu(GPUSelection::gpuList, selectedGPUIndex);
             ImGui::EndMenu();
         }

         // ⚙️ Options
         if (ImGui::BeginMenu("Options")) {
             if (ImGui::MenuItem("VSync", nullptr, vsyncEnabled)) {
                 vsyncEnabled = !vsyncEnabled;
                 Logger::Log(LogLevel::Info, vsyncEnabled ? "✅ VSync Enabled" : "⛔ VSync Disabled");
             }

             // Shared editor state
             extern Engine* g_engineInstance;
             EditorState& editor = ::g_engineInstance->GetEditorState();

             if (ImGui::BeginMenu("Camera Navigation"))
             {
                 const bool isUnity = (editor.cameraNavMode == CameraNavMode::Unity_AltMouse);
                 const bool isBlender = (editor.cameraNavMode == CameraNavMode::Blender_MMB);
                 const bool isTFZ = (editor.cameraNavMode == CameraNavMode::TFZ_RMB);

                 if (ImGui::MenuItem("Unity (Alt + Mouse)", nullptr, isUnity))
                     editor.cameraNavMode = CameraNavMode::Unity_AltMouse;
                 if (ImGui::MenuItem("Blender (MMB)", nullptr, isBlender))
                     editor.cameraNavMode = CameraNavMode::Blender_MMB;
                 if (ImGui::MenuItem("TFZ (RMB)", nullptr, isTFZ))
                     editor.cameraNavMode = CameraNavMode::TFZ_RMB;

                 ImGui::EndMenu();
             }

             if (ImGui::BeginMenu("View Mode"))
             {
                 const bool is3D = (editor.viewMode == ViewMode::Mode3D);
                 const bool is2D = (editor.viewMode == ViewMode::Mode2D);

                 if (ImGui::MenuItem("3D", nullptr, is3D))
                     editor.viewMode = ViewMode::Mode3D;
                 if (ImGui::MenuItem("2D", nullptr, is2D))
                     editor.viewMode = ViewMode::Mode2D;

                 ImGui::EndMenu();
             }

             if (ImGui::BeginMenu("Grid Mode"))
             {
                 const bool isInfinite = (editor.gridMode == GridMode::Infinite_CameraPivot);
                 const bool isFixed = (editor.gridMode == GridMode::Fixed_WorldOrigin);

                 if (ImGui::MenuItem("Infinite", nullptr, isInfinite))
                     editor.gridMode = GridMode::Infinite_CameraPivot;
                 if (ImGui::MenuItem("Fixed (World Origin)", nullptr, isFixed))
                     editor.gridMode = GridMode::Fixed_WorldOrigin;

                 ImGui::EndMenu();
             }

             // Custom Themes
             if (ImGui::BeginMenu("Theme")) {
                 if (ImGui::MenuItem("Dark", nullptr, currentTheme == Theme::Dark))         ApplyTheme(Theme::Dark);
                 if (ImGui::MenuItem("Light", nullptr, currentTheme == Theme::Light))       ApplyTheme(Theme::Light);
                 if (ImGui::MenuItem("Classic", nullptr, currentTheme == Theme::Classic))   ApplyTheme(Theme::Classic);
                 if (ImGui::MenuItem("Synthwave", nullptr, currentTheme == Theme::Synthwave)) ApplyTheme(Theme::Synthwave);
                 if (ImGui::MenuItem("Magenta", nullptr, currentTheme == Theme::Magenta))   ApplyTheme(Theme::Magenta);
                 ImGui::EndMenu();
             }

             if (ImGui::BeginMenu("Panels")) {
                 auto& scene = EditorPanels::Scene();
                 auto& hierarchy = EditorPanels::Hierarchy();
                 auto& inspector = EditorPanels::Inspector();
                 auto& assets = EditorPanels::Assets();
                 auto& debugOverlay = EditorPanels::DebugOverlay();
                 auto& diagnostics = EditorPanels::Diagnostics();
                 auto& logViewer = EditorPanels::LogViewer();

                 ImGui::MenuItem(scene.name, nullptr, &scene.open);
                 ImGui::MenuItem(hierarchy.name, nullptr, &hierarchy.open);
                 ImGui::MenuItem(inspector.name, nullptr, &inspector.open);
                 ImGui::MenuItem(assets.name, nullptr, &assets.open);
                 ImGui::Separator();
                 ImGui::MenuItem(debugOverlay.name, nullptr, &debugOverlay.open);
                 ImGui::MenuItem(diagnostics.name, nullptr, &diagnostics.open);
                 ImGui::MenuItem(logViewer.name, nullptr, &logViewer.open);
                 ImGui::Separator();
                 ImGui::MenuItem("Command Strip", nullptr, &g_showCommandStrip);
                 ImGui::MenuItem("Frame Diagnostics", nullptr, &g_showFrameDiag);
                 ImGui::EndMenu();
             }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                UI::RequestResetLayout();
            }
            if (ImGui::MenuItem("Save Layout")) {
                UI::SaveLayoutToDisk("imgui.ini");
            }

            // Custom Font Size
            static float fontSize = 16.0f;
            if (ImGui::BeginMenu("Font Size")) {
                if (ImGui::MenuItem("Small (14px)", nullptr, fontSize == 14.0f)) {
                    fontSize = 14.0f;
                    RELOAD_IMGUI_FONT(fontSize);
                }
                if (ImGui::MenuItem("Medium (16px)", nullptr, fontSize == 16.0f)) {
                    fontSize = 16.0f;
                    RELOAD_IMGUI_FONT(fontSize);
                }
                if (ImGui::MenuItem("Large (18px)", nullptr, fontSize == 18.0f)) {
                    fontSize = 18.0f;
                    RELOAD_IMGUI_FONT(fontSize);
                }
                if (ImGui::MenuItem("Extra Large (20px)", nullptr, fontSize == 20.0f)) {
                    fontSize = 20.0f;
                    RELOAD_IMGUI_FONT(fontSize);
                }
                ImGui::EndMenu();
            }

            // Custom Window Size
            if (ImGui::BeginMenu("Window Size")) {
                if (ImGui::MenuItem("1280 x 720")) {
                    SetWindowPos(GetActiveWindow(), 0, 100, 100, 1280, 720, SWP_NOZORDER | SWP_SHOWWINDOW);
                    Logger::Log(LogLevel::Info, "📐 Resolution set to 1280x720");
                }
                if (ImGui::MenuItem("1600 x 900")) {
                    SetWindowPos(GetActiveWindow(), 0, 100, 100, 1600, 900, SWP_NOZORDER | SWP_SHOWWINDOW);
                    Logger::Log(LogLevel::Info, "📐 Resolution set to 1600x900");
                }
                if (ImGui::MenuItem("1920 x 1080")) {
                    SetWindowPos(GetActiveWindow(), 0, 100, 100, 1920, 1080, SWP_NOZORDER | SWP_SHOWWINDOW);
                    Logger::Log(LogLevel::Info, "📐 Resolution set to 1920x1080");
                }
                if (ImGui::MenuItem("2560 x 1440")) {
                    SetWindowPos(GetActiveWindow(), 0, 100, 100, 2560, 1440, SWP_NOZORDER | SWP_SHOWWINDOW);
                    Logger::Log(LogLevel::Info, "📐 Resolution set to 2560x1440");
                }
                if (ImGui::MenuItem("Custom...")) {
                    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing, ImVec2(0.0f, 0.0f));
                    showCustomSizePopup = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
         }

         // ❓ Help
         if (ImGui::BeginMenu("Help")) {
             ImGui::MenuItem("ImGui Demo", nullptr, &g_showImGuiDemo);
             ImGui::MenuItem("ImGui Metrics/Debugger", nullptr, &g_showImGuiMetrics);
             ImGui::Separator();
             if (ImGui::MenuItem("About")) {
                 openAbout = true;
             }
             ImGui::EndMenu();
         }

         // ✅ Defer popup open outside the menu
         if (openAbout) {
             ImGui::OpenPopup("AboutPopup");
             openAbout = false;
         }

         if (showCustomSizePopup) {
             ImGui::OpenPopup("Custom Size");
             showCustomSizePopup = false;
         }

         // 📦 About Modal
         if (ImGui::BeginPopupModal("AboutPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
             ImGui::Text("TheFletchZone Game Engine");
             ImGui::Text("Version 1.0");
             ImGui::Spacing();
             ImGui::Text("Built with love and DirectX 12!");

             if (ImGui::Button("Close")) {
                 ImGui::CloseCurrentPopup();
             }
             ImGui::EndPopup();
         }

         // Custom Size Modal
         static int customWidth = 1280;
         static int customHeight = 720;
         if (ImGui::BeginPopupModal("Custom Size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
             ImGui::InputInt("Width", &customWidth);
             ImGui::InputInt("Height", &customHeight);
             ImGui::Spacing();

             if (ImGui::Button("Apply")) {
                 SetMainWindowSize(customWidth, customHeight);
                 Logger::Log(LogLevel::Info, "📐 Custom resolution set to " + std::to_string(customWidth) + "x" + std::to_string(customHeight));
                 ImGui::CloseCurrentPopup();
             }
             ImGui::SameLine();
             if (ImGui::Button("Cancel")) {
                 ImGui::CloseCurrentPopup();
             }

             ImGui::EndPopup();
         }
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

    void DrawMainMenuBar()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("Options"))
        {
            if (ImGui::BeginMenu("View Mode"))
            {
                auto& gfx = Graphics::GetInstance();
                const bool is3D = (gfx.GetViewMode() == ViewMode::Mode3D);
                const bool is2D = (gfx.GetViewMode() == ViewMode::Mode2D);

                if (ImGui::MenuItem("3D", nullptr, is3D))
                    gfx.SetViewMode(ViewMode::Mode3D);
                if (ImGui::MenuItem("2D", nullptr, is2D))
                    gfx.SetViewMode(ViewMode::Mode2D);

                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // Lightweight indicator in the menu bar.
        ImGui::SameLine();
        ImGui::Text("View: %s", ViewModeLabel(Graphics::GetInstance().GetViewMode()));

        ImGui::EndMainMenuBar();
    }

    // 🔍 Optional Debug Overlay
    void DrawOverlays() {
        // Debug overlay is now a dockable panel (see EditorPanels::DebugOverlay()).
    }

    void DrawSplashOverlay()
    {
      SplashScreen::DrawSplash();
    }
}

namespace UI {

    // ...existing code...

    void DrawToolbar()
    {
        g_commandStripTouchedThisFrame = true;

        if (!g_showCommandStrip)
            return;

        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;

        const float stripH = GetCommandStripReservedHeight();
        const ImVec2 pos = vp->WorkPos;
        const ImVec2 size = ImVec2(vp->WorkSize.x, stripH);

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

        if (ImGui::Begin("##Toolbar", nullptr, flags))
        {
            const Engine::State s = Engine::GetState();
            const bool playing = (s == Engine::State::Playing);
            const bool paused = (s == Engine::State::Paused);

            const char* playLabel = ICON_FA_PLAY "##Play";
            const char* pauseLabel = ICON_FA_PAUSE "##Pause";
            const char* stopLabel = ICON_FA_STOP "##Stop";

            if (playing)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.70f, 0.20f, 1.0f));
            if (ImGui::Button(playLabel))
                Engine::SetState(Engine::State::Playing);
            if (playing)
                ImGui::PopStyleColor();

            ImGui::SameLine();

            if (paused)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.70f, 0.10f, 1.0f));
            if (ImGui::Button(pauseLabel))
                Engine::SetState(Engine::State::Paused);
            if (paused)
                ImGui::PopStyleColor();

            ImGui::SameLine();

            if (ImGui::Button(stopLabel))
                Engine::SetState(Engine::State::Editing);

            ImGui::SameLine();
            ImGui::Dummy(ImVec2(12.0f, 0.0f));
            ImGui::SameLine();

            const char* stateText = (s == Engine::State::Editing) ? "Editing" : (s == Engine::State::Playing) ? "Playing" : "Paused";
            ImGui::TextUnformatted(stateText);
        }
        ImGui::End();

        ImGui::PopStyleVar(3);

        // Reserve the strip area from docking.
        vp->WorkPos.y += stripH;
        vp->WorkSize.y -= stripH;
    }

}
