#pragma once

#include <algorithm>
#include <string>
#include <vector>

struct BootProgress
{
    float overall = 0.0f;              // 0..1
    const char* stageName = "Booting...";
    float stageProgress = 0.0f;        // 0..1
    int stageIndex = 0;
    int stageCount = 1;
    std::string status;
};

struct BootLine
{
    float t = 0.0f;
    std::string text;
};

namespace Boot
{
    BootProgress& GetProgress();
    const std::vector<BootLine>& GetLines();

    void Reset(int stageCount);

    void SetStage(const char* name, int index);
    void SetStageProgress(float progress, const char* status = nullptr);

    void PushLine(const std::string& text, float nowSeconds);
}
