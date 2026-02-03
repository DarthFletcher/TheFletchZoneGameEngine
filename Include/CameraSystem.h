#pragma once

#include "CameraData.h"

#include <cstdint>

class CameraSystem
{
public:
    static void InitializeOnce();

    // Phase 3C: exactly one deterministic update per frame.
    static void Update(uint64_t frameIndex, float dt, uint32_t viewportW, uint32_t viewportH);

    static const CameraData& GetActiveData();
};
