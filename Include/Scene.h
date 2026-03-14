#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>

#include "InstanceData.h"
#include "Bounds.h"

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

struct CameraData;

struct SceneInstance
{
    uint32_t instanceId = 0;
    std::string name;
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
    bool visible = true;
    int materialIndex = 0;
};

struct SceneStats
{
    uint32_t totalObjects = 0;
    uint32_t visibleObjects = 0;
    uint32_t drawCalls = 0;
};

struct SceneRenderContext
{
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;

    uint64_t frameIndex = 0;

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
    static void InitializeResources(ID3D12Device* device);
    static bool IsReady();
    static SceneStats GetLastStats();
    static void SetTargetInstanceCount(uint32_t count);
    static uint32_t GetTargetInstanceCount();

    static bool TrySelectInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
    static bool TrySelectInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir);
    static void SetSelectedInstanceIndex(int index);
    static int GetSelectedInstanceIndex();
    static void SetSelectedInstanceId(uint32_t instanceId);
    static uint32_t GetSelectedInstanceId();
    static bool GetSelectedInstanceTransform(DirectX::XMFLOAT3& outPosition, DirectX::XMFLOAT3& outRotation, DirectX::XMFLOAT3& outScale);
    static SceneInstance* GetSelectedInstance();
    static SceneInstance* GetInstance(size_t index);
    static const std::vector<SceneInstance>& GetInstances();
    static bool TryGetLastRenderCameraData(CameraData& outCamera);
    static void RebuildRenderInstancesFromSceneData();
    static void DeleteSelectedInstance();
    static void DuplicateSelectedInstance();
    static void CreateCube(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static bool SaveToFile(const std::string& path);
    static bool LoadFromFile(const std::string& path);

    static const InstanceData* GetInstancesCPU();
    static UINT GetInstanceCount();
    static uint64_t GetInstanceDataVersion();
    static uint64_t GetInstanceUploadedVersion();
    static void MarkInstancesDirty();
    static void MarkInstancesUploaded(uint64_t version);
    static ID3D12Resource* GetInstanceDefaultBuffer();
    static const InstanceData* GetVisibleInstancesCPU();
    static UINT GetVisibleInstanceCount();

private:
    static std::vector<SceneInstance> s_SceneInstances;
    static std::vector<InstanceData> s_Instances;
    static SceneStats s_LastStats;
    static uint32_t s_TargetInstanceCount;
    static uint32_t s_NextInstanceId;
    static bool s_SceneLayoutDirty;
    static void EnsureInstancesInitialized();
    static void ValidateSelection();

    static std::vector<Sphere> s_InstanceBounds;
    static std::vector<UINT> s_VisibleInstanceIndices;
    static std::vector<InstanceData> s_VisibleInstancesScratch;

    static uint32_t s_SelectedInstanceId;

    static D3D12_CPU_DESCRIPTOR_HANDLE s_InstanceSRVCpu;
    static D3D12_GPU_DESCRIPTOR_HANDLE s_InstanceSRVGpu;

    static Microsoft::WRL::ComPtr<ID3D12Resource> s_InstanceBufferDefault;
    static UINT s_InstanceBufferCapacity;
    static void EnsureInstanceBufferDefault(ID3D12Device* device, UINT requiredCount);

    static bool s_InstancesDirty;
    static uint64_t s_InstanceDataVersion;
    static uint64_t s_InstanceUploadedVersion;
};
