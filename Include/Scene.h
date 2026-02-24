#pragma once

#include <cstdint>
#include <vector>
#include <wrl.h>
#include <d3d12.h>

#include "InstanceData.h"

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

// Phase 3A: minimal scene render scaffolding.
// No draw calls yet; this only validates wiring and ownership.

struct CameraData;

struct SceneRenderContext
{
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;

    uint64_t frameIndex = 0;

    // Phase 3C: authoritative camera data for this scene render.
    // Scene must not access any global camera.
    const CameraData* camera = nullptr;
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

private:
    static std::vector<InstanceData> s_Instances;
    static void EnsureInstancesInitialized();

    static Microsoft::WRL::ComPtr<ID3D12Resource> s_InstanceBuffer;
    static uint8_t* s_InstanceMappedPtr;
    static UINT s_InstanceCapacity;
    static D3D12_CPU_DESCRIPTOR_HANDLE s_InstanceSRVCpu;
    static D3D12_GPU_DESCRIPTOR_HANDLE s_InstanceSRVGpu;
    static void EnsureInstanceBuffer(ID3D12Device* device);

    // CP9-A: GPU-local DEFAULT heap instance storage (staged upload).
    // Old UPLOAD buffer path is retained in CP9-A (no behavior change yet).
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_InstanceBufferDefault;
    static UINT s_InstanceBufferCapacity;
    static void EnsureInstanceBufferDefault(ID3D12Device* device, UINT requiredCount);
};
