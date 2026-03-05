#include "SplashScreen.h"
#include "Logger.h"
#include "imgui.h"
#include "ImGuiUtils.h"
#include <vector>
#include <format>
#include "imgui_impl_dx12_custom.h"
#include "Graphics.h"
#include "BootProgress.h"

#ifndef NDEBUG
#include "DebugIsolation.h"
#endif

// --------------------------------------------------------------
// Internal CPU-side storage
// --------------------------------------------------------------
namespace SplashScreen
{
    static ImTextureID splashTex = 0;         // GPU texture (uploaded later)
    static std::string statusMessage;         // Status text below splash

    static unsigned char* cpuPixels = nullptr;
    static int cpuWidth = 0;
    static int cpuHeight = 0;
    static int cpuChannels = 0;

    static bool gpuUploaded = false;
    static float splashRemaining = 0.0f; // For future fade-out effect

    enum class SplashState
    {
        Boot,
        WaitingForGPU,
        FadingOut,
        Done
    };

    static SplashState s_state = SplashState::Boot;
    static float s_fadeAlpha = 1.0f;

    static float s_minShowTime = 0.0f;
    static float s_elapsedSinceShow = 0.0f;

    static bool s_statusExplicit = false;    // Track explicit status text
    static bool s_showRequested = false;
    static bool s_finishRequested = false;

    static void DrawCenteredSplashImage(ImGuiViewport* vp, float alpha);
    static void DrawRecommendedSplashLayout(ImGuiViewport* vp, float alpha);

    // --------------------------------------------------------------
    // 1. CPU-ONLY load (safe during early engine init)
    // --------------------------------------------------------------
    bool Initialize(const char* imagePath)
    {
        Logger::Log(LogLevel::Info, "🖼 SplashScreen::Initialize (CPU decode only)");

        cpuPixels = ImGuiUtils::LoadImageCPU(imagePath, cpuWidth, cpuHeight, cpuChannels);

        if (!cpuPixels)
        {
            Logger::Log(LogLevel::Error,
                std::format("❌ Failed to load splash image '{}'", imagePath));
            return false;
        }

        Logger::Log(LogLevel::Info,
            std::format("✅ Splash image decoded: {}x{} ({} channels)",
                cpuWidth, cpuHeight, cpuChannels));

        gpuUploaded = false;
        splashTex = 0;

        s_state = SplashState::Boot;
        s_fadeAlpha = 1.0f;

        return true;
    }

    // --------------------------------------------------------------
    // Helper: upload GPU texture only when the render pipeline is ready
    // --------------------------------------------------------------
    void EnsureGPUTexture()
    {
#ifdef _DEBUG
        if (g_r_skipSplashUpload)
        {
            static double s_lastSkipLog = -1000.0;
            const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
            if (now - s_lastSkipLog > 1.0)
            {
                s_lastSkipLog = now;
                Logger::Log(LogLevel::Info, "[Splash] GPU upload skipped via F5", "[Splash]");
            }
            return;
        }
#endif

        auto& gfx = Graphics::GetInstance();

        // Detect the common failure mode: splashTex accidentally points at the ImGui font atlas SRV.
        // In that case, force a re-upload/recreate.
        ImTextureID fontTex = 0;
        if (ImGui::GetCurrentContext())
            fontTex = ImGui::GetIO().Fonts ? ImGui::GetIO().Fonts->TexID : 0;

        if (splashTex && fontTex && splashTex == fontTex)
        {
            Logger::Log(LogLevel::Warning, std::format(
                "⚠️ SplashScreen::EnsureGPUTexture() - splashTex matches font TexID (0x{:016X}). Forcing re-upload.",
                (uint64_t)splashTex));
            splashTex = 0;
            gpuUploaded = false;
        }

         static double s_lastEnterLog = -1000.0;
         const double nowEnter = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
         if (nowEnter - s_lastEnterLog > 1.0)
         {
             s_lastEnterLog = nowEnter;
             Logger::Log(LogLevel::Info, std::format(
                 "🧪 SplashScreen::EnsureGPUTexture() enter | gpuUploaded={} splashTex=0x{:016X} fontTex=0x{:016X} cpuPixels={} imguiCtx={}",
                 gpuUploaded ? 1 : 0,
                 (uint64_t)splashTex,
                 (uint64_t)fontTex,
                 (void*)cpuPixels,
                 (void*)ImGui::GetCurrentContext()));
         }

         if (gpuUploaded)
             return;

         if (!cpuPixels)
         {
             Logger::Log(LogLevel::Error,
                 "❌ SplashScreen::EnsureGPUTexture() - No CPU pixel data loaded!");
             return;
         }

         // Do not hard-gate on IsUploadReady here. The upload path uses its own temporary
         // command allocator/list and CreateTextureFromMemory() validates DX12 objects.
         static double s_lastAttemptLog = -1000.0;
         const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
         if (now - s_lastAttemptLog > 1.0)
         {
             s_lastAttemptLog = now;
             Logger::Log(LogLevel::Info,
                 std::format("🔼 SplashScreen::EnsureGPUTexture() - attempting upload {}x{} (channels={})",
                     cpuWidth, cpuHeight, cpuChannels));
         }

         ImTextureID tex = ImGuiUtils::CreateTextureFromMemory(
             cpuPixels,
             cpuWidth,
             cpuHeight,
             cpuChannels);

         Logger::Log(LogLevel::Info,
             std::format("🧪 SplashScreen::EnsureGPUTexture() - CreateTextureFromMemory returned 0x{:016X}", (uint64_t)tex));

         if (!tex)
         {
             Logger::Log(LogLevel::Error,
                 "❌ SplashScreen::EnsureGPUTexture() - CreateTextureFromMemory returned null ImTextureID");
             return;
         }

         if (fontTex && tex == fontTex)
         {
             Logger::Log(LogLevel::Error, std::format(
                 "❌ SplashScreen::EnsureGPUTexture() - allocator returned FONT TexID (0x{:016X}) for splash. Will retry next frame.",
                 (uint64_t)tex));
             // Keep gpuUploaded=false so we retry.
             splashTex = 0;
             return;
         }

         splashTex = tex;
         gpuUploaded = true;

         Logger::Log(LogLevel::Info,
             std::format("✅ SplashScreen::EnsureGPUTexture() - Texture uploaded (TexID = 0x{:016X})",
                 (uint64_t)splashTex));
     }

    // --------------------------------------------------------------
    // 2. Set status text
    // --------------------------------------------------------------
    void SetStatusText(const char* text)
    {
        statusMessage = (text ? text : "");
        s_statusExplicit = true;
    }

    // --------------------------------------------------------------
    // 3. Render splash screen (GPU texture created here on first frame)
    // --------------------------------------------------------------
    void Render()
    {
        // --------------------------------------------------
        // 0. Respect splash lifetime
        // --------------------------------------------------
        if (!IsVisible())
            return;

        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;

        const float alpha = (s_state == SplashState::FadingOut) ? s_fadeAlpha : 1.0f;
        DrawRecommendedSplashLayout(vp, alpha);
    }

    static void DrawCenteredSplashImage(ImGuiViewport* vp, float alpha)
    {
        if (!vp || !splashTex)
            return;

        // Diagnostics: if we are drawing but see the font circle, the issue is likely shader/PSO sampling
        // or SRV contents being overwritten (not TexID equality). Log occasionally.
        {
            static double s_lastDrawLog = -1000.0;
            const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
            if (now - s_lastDrawLog > 1.0)
            {
                s_lastDrawLog = now;
                ImTextureID fontTex = 0;
                if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
                    fontTex = ImGui::GetIO().Fonts->TexID;

                Logger::Log(LogLevel::Info, std::format(
                    "🖼️ SplashDraw | splashTex=0x{:016X} fontTex=0x{:016X} cpuSize={}x{} vpSize=({:.0f},{:.0f}) alpha={:.2f}",
                    (uint64_t)splashTex,
                    (uint64_t)fontTex,
                    cpuWidth,
                    cpuHeight,
                    vp->Size.x,
                    vp->Size.y,
                    alpha));
            }
        }

        ImDrawList* draw = ImGui::GetForegroundDrawList(vp);
        if (!draw)
            return;

        const ImVec2 screen = vp->Size;
        if (screen.x <= 0 || screen.y <= 0)
            return;

        const float targetHeight = screen.y * 0.70f;
        const float aspect = (cpuHeight > 0) ? ((float)cpuWidth / (float)cpuHeight) : 1.0f;
        const float targetWidth = targetHeight * aspect;

        const float x0 = (screen.x - targetWidth) * 0.5f;
        const float y0 = (screen.y - targetHeight) * 0.5f;
        const ImVec2 pos(x0, y0);

        const ImU32 tint = IM_COL32(255, 255, 255, (int)(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));

        draw->AddImage(
            splashTex,
            pos,
            ImVec2(pos.x + targetWidth, pos.y + targetHeight),
            ImVec2(0, 0),
            ImVec2(1, 1),
            tint);

        if (!statusMessage.empty())
        {
            const float textY = pos.y + targetHeight + 20.0f;
            const ImVec2 textSize = ImGui::CalcTextSize(statusMessage.c_str());
            const float textX = (screen.x - textSize.x) * 0.5f;

            const ImU32 textCol = IM_COL32(255, 160, 64, (int)(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
            draw->AddText(ImVec2(textX, textY), textCol, statusMessage.c_str());
        }
    }

    static void DrawRecommendedSplashLayout(ImGuiViewport* vp, float alpha)
    {
        if (!vp)
            return;

        const BootProgress& b = Boot::GetProgress();
        const auto& lines = Boot::GetLines();

        const ImVec2 screenPos = vp->Pos;
        const ImVec2 screenSize = vp->Size;
        if (screenSize.x <= 1.0f || screenSize.y <= 1.0f)
            return;

        const float pad = 22.0f;
        const float panelW = (std::min)(560.0f, screenSize.x - pad * 2.0f);

        // Roughly match the mock: centered block, slightly above true center.
        const float panelH = 420.0f;
        const ImVec2 panelPos(screenPos.x + (screenSize.x - panelW) * 0.5f,
            screenPos.y + (screenSize.y - panelH) * 0.5f);

        ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f * std::clamp(alpha, 0.0f, 1.0f));

        ImGui::Begin("TFZ_SplashLayout", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs);

        // Top separator line inside the panel
        {
            const ImVec2 winPos = ImGui::GetWindowPos();
            const ImVec2 winSize = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float y = winPos.y + 26.0f;
            const ImU32 lineCol = IM_COL32(255, 255, 255, (int)(40.0f * alpha));
            dl->AddLine(ImVec2(winPos.x + 18.0f, y), ImVec2(winPos.x + winSize.x - 18.0f, y), lineCol, 1.0f);
        }

        // Content padding
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Dummy(ImVec2(1.0f, 44.0f));

        // Logo block (use splashTex if ready, otherwise placeholder text)
        const float logoH = 150.0f;
        if (splashTex)
        {
            const float aspect = (cpuHeight > 0) ? ((float)cpuWidth / (float)cpuHeight) : 1.0f;
            const float logoW = (std::min)(panelW - 80.0f, logoH * aspect);
            ImGui::SetCursorPosX((panelW - logoW) * 0.5f);
            ImGui::Image(splashTex, ImVec2(logoW, logoH), ImVec2(0, 0), ImVec2(1, 1),
                ImVec4(1, 1, 1, std::clamp(alpha, 0.0f, 1.0f)));
        }
        else
        {
            ImGui::SetCursorPosX((panelW - 200.0f) * 0.5f);
            ImGui::TextUnformatted("[ TFZ ENGINE LOGO ]");
            ImGui::Dummy(ImVec2(1.0f, logoH - ImGui::GetTextLineHeightWithSpacing()));
        }

        ImGui::Dummy(ImVec2(1.0f, 10.0f));

        // Title/subtitle
        {
            const char* title = "Booting TheFletchZone Engine";
            ImVec2 t = ImGui::CalcTextSize(title);
            ImGui::SetCursorPosX((panelW - t.x) * 0.5f);
            ImGui::TextUnformatted(title);
        }

        ImGui::Dummy(ImVec2(1.0f, 14.0f));

        // Boot lines (center-left like mock)
        const float linesW = panelW - 80.0f;
        const float linesH = 120.0f;
        ImGui::SetCursorPosX((panelW - linesW) * 0.5f);
        ImGui::BeginChild("TFZ_SplashLines", ImVec2(linesW, linesH), false, ImGuiWindowFlags_NoScrollbar);
        if (!lines.empty())
        {
            // Show only the last few lines to avoid overflowing the panel.
            const int maxLines = 5;
            const int lineCount = (int)lines.size();
            const int start = (lineCount > maxLines) ? (lineCount - maxLines) : 0;
            for (int i = start; i < lineCount; ++i)
                ImGui::TextUnformatted(lines[(size_t)i].text.c_str());
        }
        else if (!b.status.empty())
        {
            ImGui::TextUnformatted(b.status.c_str());
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(1.0f, 10.0f));

        // Progress bar: centered, with percent on the right (like mock)
        // NOTE: ensure we draw only one bar (the centered one).
        {
            const float barH = 18.0f;
            const float barW = (std::min)(320.0f, panelW - 140.0f);
            const float pctW = 60.0f;
            const float totalW = barW + 10.0f + pctW;
            ImGui::SetCursorPosX((panelW - totalW) * 0.5f);

            const float p = std::clamp(b.overall, 0.0f, 1.0f);
            ImGui::ProgressBar(p, ImVec2(barW, barH), "");
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::Text("%d%%", (int)std::round(p * 100.0f));
        }

        ImGui::End();
    }

    // --------------------------------------------------------------
    // 4. Cleanup
    // --------------------------------------------------------------
    void Shutdown()
    {
        Logger::Log(LogLevel::Info, "🧼 SplashScreen::Shutdown");

        if (cpuPixels)
        {
            ImGuiUtils::FreeCPUImage(cpuPixels);
            cpuPixels = nullptr;
        }

        splashTex = 0;
        gpuUploaded = false;
        statusMessage.clear();

        s_state = SplashState::Boot;
        s_fadeAlpha = 1.0f;
        splashRemaining = 0.0f;
        s_minShowTime = 0.0f;
        s_elapsedSinceShow = 0.0f;
        s_statusExplicit = false;
        s_showRequested = false;
        s_finishRequested = false;
    }

	// --------------------------------------------------------------
	// 5. Public API
	// --------------------------------------------------------------
    void SetMinimumShowTime(float seconds)
    {
        s_minShowTime = (std::max)(0.0f, seconds);
    }

	// Show the splash screen. This is a no-op if already shown.
    void Show()
    {
        s_showRequested = true;
        s_finishRequested = false;

        // Reset splash state for a new showing.
        s_state = SplashState::Boot;
        s_fadeAlpha = 1.0f;
        s_elapsedSinceShow = 0.0f;

        // Do not clear the user-provided status; just mark it as explicit if it's already set.
        if (!statusMessage.empty())
            s_statusExplicit = true;
    }

	// Request the splash screen to finish (fade out and hide). This is a no-op if not shown.
    void RequestFinish()
    {
        s_finishRequested = true;
    }

	// Legacy helper: show the splash screen for a fixed number of seconds, then request finish.
    void ShowForSeconds(float seconds)
    {
        // Legacy behavior: show and then request finish after a minimum on-screen time.
        // This keeps old call sites working but new boot flow should use Show/RequestFinish.
        SetMinimumShowTime(seconds);
        Show();
        s_finishRequested = true;
    }

	// Returns true if the splash screen should be drawn (i.e., not finished).
    bool IsVisible()
    {
        // Visible for all states except Done.
        return s_state != SplashState::Done;
    }

	// Update the splash screen state machine. Should be called every frame with the delta time since last update.
    void Update(float dt)
    {
        // If nobody requested splash to show, treat as finished.
        if (!s_showRequested)
        {
            s_state = SplashState::Done;
            return;
        }

        auto& gfx = Graphics::GetInstance();

        s_elapsedSinceShow += dt;

        switch (s_state)
        {
        case SplashState::Boot:
            if (!s_statusExplicit && statusMessage.empty())
                statusMessage = "Booting…";
            s_state = SplashState::WaitingForGPU;
            break;

        case SplashState::WaitingForGPU:
        {
            if (!s_statusExplicit && (statusMessage.empty() || statusMessage == "Booting…"))
                statusMessage = "Initializing GPU…";

            // Always call EnsureGPUTexture() so it can log/attempt/defers safely.
            // (EnsureGPUTexture() internally gates on IsUploadReady and throttles logs.)
            EnsureGPUTexture();

            // Only advance to fading if the texture actually uploaded.
            if (gpuUploaded && splashTex)
            {
                // Only finish (fade out) if the app requested it.
                if (s_finishRequested && s_elapsedSinceShow >= s_minShowTime)
                {
                    if (!s_statusExplicit)
                        statusMessage.clear();
                    s_state = SplashState::FadingOut;
                }
            }
            break;
        }

        case SplashState::FadingOut:
        {
            s_fadeAlpha -= dt * 1.5f;
            if (s_fadeAlpha <= 0.0f)
            {
                s_fadeAlpha = 0.0f;
                s_state = SplashState::Done;
            }
            break;
        }

        case SplashState::Done:
            break;
        }
    }

	// Returns true if the splash screen has finished and should no longer be drawn.
    bool IsFinished()
    {
        return s_state == SplashState::Done;
    }

	// Draw the splash screen. Should be called every frame if IsVisible() returns true.
    void DrawSplash()
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;

        const float alpha = (s_state == SplashState::FadingOut) ? s_fadeAlpha : 1.0f;
        DrawRecommendedSplashLayout(vp, alpha);
    }
}