#include "Scene.h"

#include "Logger.h"

#include <algorithm>

namespace
{
    static bool g_loggedInit = false;
}

void Scene::Render(const SceneRenderContext& ctx)
{
    // Phase 3A: logs only, no GPU work.
    if (!g_loggedInit)
    {
        Logger::Log(LogLevel::Info, "Scene render path attached (Phase 3A scaffolding)", "Scene");
        Logger::Log(LogLevel::Info, "Shader ownership scaffolding attached (no compilation yet)", "Shader");
        g_loggedInit = true;
    }

    if (!ctx.device || !ctx.commandList)
    {
        Logger::Log(LogLevel::Error, "Scene::Render called with null device/commandList", "Scene");
        return;
    }

    // Clamp to guard against accidental zero sizes.
    const uint32_t w = (std::max)(1u, ctx.viewportWidth);
    const uint32_t h = (std::max)(1u, ctx.viewportHeight);

    // Do not spam per-frame; only log on invalid inputs.
    if (w != ctx.viewportWidth || h != ctx.viewportHeight)
    {
        Logger::Log(LogLevel::Warning, "Scene::Render received non-positive viewport; clamped", "Scene");
    }

    (void)ctx.frameIndex;
}
