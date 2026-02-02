#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

// Phase 3A: minimal scene render scaffolding.
// No draw calls yet; this only validates wiring and ownership.

struct SceneRenderContext
{
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;

    uint64_t frameIndex = 0;
};

enum class ShaderID : uint32_t
{
    Invalid = 0,
    SceneSolid,
    SceneGrid,
    Count,
};

class Scene
{
public:
    static void Render(const SceneRenderContext& ctx);
};
