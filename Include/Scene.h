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

    uint32_t frameIndex = 0;
};

enum class ShaderID : uint32_t
{
    None = 0,

    // Placeholders for Phase 3A ownership/wiring.
    Scene_Triangle_VS,
    Scene_Triangle_PS,
};

namespace Scene
{
    void Render(const SceneRenderContext& ctx);
}
