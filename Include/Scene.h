#pragma once

#include <cstdint>
#include <vector>
#include <wrl.h>
#include <d3d12.h>

#include "InstanceData.h"
#include "Bounds.h"

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

    // Deterministic GPU resource ownership: must be called outside Render().
    static void InitializeResources(ID3D12Device* device);
    static bool IsReady();

    // CP9-B: CPU -> GPU instance upload contract (Graphics-owned upload; Scene-owned data).
    static const InstanceData* GetInstancesCPU();
    static UINT GetInstanceCount();
    static uint64_t GetInstanceDataVersion();
    static uint64_t GetInstanceUploadedVersion();
    static void MarkInstancesDirty();
    static void MarkInstancesUploaded(uint64_t version);

    // CP9-B: expose DEFAULT buffer to Graphics upload path.
    static ID3D12Resource* GetInstanceDefaultBuffer();

    // CP11-D: visible subset upload source (Scene-owned)
    static const InstanceData* GetVisibleInstancesCPU();
    static UINT GetVisibleInstanceCount();

private:
    static std::vector<InstanceData> s_Instances;
    static void EnsureInstancesInitialized();

    // CP11-B: per-instance bounds (bounding sphere)
    static std::vector<Sphere> s_InstanceBounds;

    // CP11-C: visible list (computed per frame; rendering unchanged for now)
    static std::vector<UINT> s_VisibleInstanceIndices;

    // CP11-D: contiguous visible instance scratch (built per frame)
    static std::vector<InstanceData> s_VisibleInstancesScratch;

    static D3D12_CPU_DESCRIPTOR_HANDLE s_InstanceSRVCpu;
    static D3D12_GPU_DESCRIPTOR_HANDLE s_InstanceSRVGpu;

    // DEFAULT heap instance storage (staged upload).
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_InstanceBufferDefault;
    static UINT s_InstanceBufferCapacity;
    static void EnsureInstanceBufferDefault(ID3D12Device* device, UINT requiredCount);

    // CP9-B: dirty tracking
    static bool     s_InstancesDirty;
    static uint64_t s_InstanceDataVersion;
    static uint64_t s_InstanceUploadedVersion;
};
