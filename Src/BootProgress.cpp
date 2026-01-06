#include "BootProgress.h"

namespace
{
    BootProgress g_boot;
    std::vector<BootLine> g_lines;

    static float Clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }
}

BootProgress& Boot::GetProgress()
{
    return g_boot;
}

const std::vector<BootLine>& Boot::GetLines()
{
    return g_lines;
}

void Boot::Reset(int stageCount)
{
    g_boot = BootProgress{};
    g_boot.stageCount = (std::max)(1, stageCount);
    g_lines.clear();
}

void Boot::SetStage(const char* name, int index)
{
    g_boot.stageName = name ? name : "";
    g_boot.stageIndex = (std::max)(0, index);
    g_boot.stageCount = (std::max)(1, g_boot.stageCount);
    g_boot.stageProgress = 0.0f;
    g_boot.overall = (float)g_boot.stageIndex / (float)g_boot.stageCount;
}

void Boot::SetStageProgress(float progress, const char* status)
{
    g_boot.stageProgress = Clamp01(progress);

    const int count = (std::max)(1, g_boot.stageCount);
    const int idx = (std::max)(0, (std::min)(g_boot.stageIndex, count - 1));
    g_boot.overall = Clamp01(((float)idx + g_boot.stageProgress) / (float)count);

    if (status)
        g_boot.status = status;
}

void Boot::PushLine(const std::string& text, float nowSeconds)
{
    g_lines.push_back({ nowSeconds, text });
    if (g_lines.size() > 18)
        g_lines.erase(g_lines.begin());
}
