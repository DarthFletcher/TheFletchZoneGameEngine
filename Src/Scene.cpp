#include "Scene.h"

#include "Logger.h"

#include <algorithm>

namespace
{
    static bool g_loggedInit = false;
    static bool g_loggedInvalidCtx = false;
}

void Scene::Render(const SceneRenderContext& ctx)
{
    // Phase 3A: logs only, no GPU work.
    if (!g_loggedInit)
    {
        Logger::Log(LogLevel::Info, "Phase 3A: Scene render path wired (scaffolding only)", "[Scene]");
        Logger::Log(LogLevel::Info, "Phase 3A: ShaderID ownership placeholder active (no compilation yet)", "[Shader]");
        g_loggedInit = true;
    }

    if (!ctx.device || !ctx.commandList)
    {
        if (!g_loggedInvalidCtx)
        {
            Logger::Log(LogLevel::Error, "Scene::Render called with null device/commandList", "[Scene]");
            g_loggedInvalidCtx = true;
        }
        return;
    }

    // Clamp to guard against accidental invalid sizes.
    constexpr uint32_t kMin = 1u;
    constexpr uint32_t kMax = 16384u;

    const uint32_t w = (std::min)(kMax, (std::max)(kMin, ctx.viewportWidth));
    const uint32_t h = (std::min)(kMax, (std::max)(kMin, ctx.viewportHeight));

    (void)w;
    (void)h;
    (void)ctx.frameIndex;
}
