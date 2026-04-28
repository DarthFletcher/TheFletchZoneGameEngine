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

struct SceneGridSettings
{
    bool fadeEnabled = true;
    bool majorLinesEnabled = true;
    float fadeDistance = 40.0f;
    float visibility = 1.0f;
    float majorLineBoost = 1.35f;
    float axisEmphasis = 1.25f;
    float extent = 10.0f;
    int divisions = 20;
};

enum class ScenePrimitive : uint8_t
{
    Cube = 0,
    Sphere = 1,
    Plane = 2,
    Cylinder = 3,
    Empty = 4,
    Capsule = 5,
    Torus = 6,
    Cone = 7
};

enum class VaultType : uint8_t
{
    None = 0,
    Node = 1,
    Ring = 2,
    Core = 3
};

struct CameraComponent
{
    bool enabled = false;
    bool isMain = false;
    float fovY = DirectX::XMConvertToRadians(60.0f);
    float nearClip = 0.1f;
    float farClip = 100.0f;
};

struct SceneInstance
{
    uint32_t instanceId = 0;
    uint32_t parentInstanceId = 0;
    uint32_t prefabLocalInstanceId = 0;
    std::string name;
    std::string prefabSourcePath;
    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
    bool visible = true;
    int materialIndex = 0;
    ScenePrimitive primitive = ScenePrimitive::Cube;
    VaultType vaultType = VaultType::None;
    CameraComponent camera;
};

struct SceneStats
{
    uint32_t totalObjects = 0;
    uint32_t visibleObjects = 0;
    uint32_t drawCalls = 0;
};

struct ParentTransformState
{
    uint32_t instanceId = 0;
    uint32_t parentInstanceId = 0;
    DirectX::XMFLOAT3 localPosition{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 localRotation{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 localScale{ 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4X4 worldMatrix{};
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

struct PrefabOverrideState
{
    bool name = false;
    bool rotation = false;
    bool scale = false;
    bool visible = false;
    bool material = false;
    bool vaultType = false;

    bool Any() const
    {
        return name || rotation || scale || visible || material || vaultType;
    }
};

enum class PrefabProperty : uint8_t
{
    Name,
    Rotation,
    Scale,
    Visible,
    Material,
    VaultType,
};

class Scene
{
public:
    static void Render(const SceneRenderContext& ctx);
    static void InitializeResources(ID3D12Device* device);
    static bool IsReady();
    static SceneStats GetLastStats();
    static void SetGridSettings(const SceneGridSettings& settings);
    static SceneGridSettings GetGridSettings();
    static void SetTargetInstanceCount(uint32_t count);
    static uint32_t GetTargetInstanceCount();
    static void UpdateLastRenderCameraData(const CameraData& camera);

    static bool TrySelectInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
    static bool TrySelectInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir);
    static bool TrySelectInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight, bool additive, bool toggle);
    static bool TrySelectInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir, bool additive, bool toggle);
    static bool TryHoverInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
    static bool TryHoverInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir);
    static void ClearHoveredInstance();
    static uint32_t GetHoveredInstanceId();
    static void SetSelectedInstanceIndex(int index);
    static int GetSelectedInstanceIndex();
    static void SetSelectedInstanceId(uint32_t instanceId);
    static uint32_t GetSelectedInstanceId();
    static bool IsInstanceSelected(uint32_t instanceId);
    static void AddSelectedInstanceId(uint32_t instanceId);
    static void RemoveSelectedInstanceId(uint32_t instanceId);
    static void ToggleSelectedInstanceId(uint32_t instanceId);
    static void ClearSelection();
    static void RestoreSelectionState(uint32_t activeInstanceId, const std::vector<uint32_t>& selectedInstanceIds);
    static const std::vector<uint32_t>& GetSelectedInstanceIds();
    static bool TryGetSelectionCenter(DirectX::XMFLOAT3& outCenter);
    static bool TryGetSelectionBounds(DirectX::XMFLOAT3& outCenter, float& outRadius);
    static DirectX::XMFLOAT3 GetSelectionCenterOrActivePosition();
    static void FilterRootInstanceIds(const std::vector<uint32_t>& instanceIds, std::vector<uint32_t>& outRootIds);
    static void CaptureParentTransformState(const std::vector<uint32_t>& instanceIds, std::vector<ParentTransformState>& outStates);
    static bool RestoreParentTransformState(const std::vector<ParentTransformState>& states);
    static bool CanParentInstance(uint32_t childInstanceId, uint32_t parentInstanceId);
    static bool CanPreserveWorldTransformOnReparent(uint32_t childInstanceId, uint32_t parentInstanceId, std::string* outReason = nullptr);
    static bool SetParentInstance(uint32_t childInstanceId, uint32_t parentInstanceId, bool keepWorldTransform = true);
    static uint32_t GetParentInstanceId(uint32_t instanceId);
    static DirectX::XMFLOAT3 GetInstanceWorldPosition(uint32_t instanceId);
    static bool TryGetInstanceWorldMatrix(uint32_t instanceId, DirectX::XMFLOAT4X4& outWorld);
    static std::vector<uint32_t> GetChildInstanceIds(uint32_t parentInstanceId);
    static bool GetSelectedInstanceTransform(DirectX::XMFLOAT3& outPosition, DirectX::XMFLOAT3& outRotation, DirectX::XMFLOAT3& outScale);
    static SceneInstance* GetSelectedInstance();
    static SceneInstance* GetInstance(size_t index);
    static const std::vector<SceneInstance>& GetInstances();
    static SceneInstance* GetMainCameraInstanceMutable();
    static const SceneInstance* GetMainCameraInstance();
    static bool TryBuildMainCameraData(float aspect, CameraData& outCameraData);
    static bool TryGetLastRenderCameraData(CameraData& outCamera);
    static void RebuildRenderInstancesFromSceneData();
    static void DeleteSelectedInstance();
    static bool DeleteInstances(const std::vector<uint32_t>& instanceIds);
    static bool DeleteInstanceById(uint32_t instanceId);
    static bool DuplicateInstances(const std::vector<uint32_t>& sourceInstanceIds, std::vector<uint32_t>& outCreatedInstanceIds);
    static void DuplicateSelectedInstance();
    static bool RenameInstance(uint32_t instanceId, const std::string& newName);
    static void CreateEmpty(const DirectX::XMFLOAT3& position = { 0.0f, 0.0f, 0.0f });
    static void CreateCamera(const DirectX::XMFLOAT3& position = { 0.0f, 2.0f, -5.0f });
    static void CreateCube(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void CreateSphere(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void CreatePlane(const DirectX::XMFLOAT3& position = { 0.0f, 0.0f, 0.0f });
    static void CreateCylinder(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void CreateCapsule(const DirectX::XMFLOAT3& position = { 0.0f, 0.75f, 0.0f });
    static void CreateTorus(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void CreateCone(const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void CreatePrimitive(ScenePrimitive primitive, const DirectX::XMFLOAT3& position = { 0.0f, 0.5f, 0.0f });
    static void NewScene();
    static bool LoadPrefabAssetInstances(const std::string& path, std::vector<SceneInstance>& outInstances);
    static bool GetPrefabAssetCategory(const std::string& path, std::string& outCategory);
    static bool SetPrefabAssetCategory(const std::string& path, const std::string& category);
    static bool GetPrefabAssetTags(const std::string& path, std::vector<std::string>& outTags);
    static bool SetPrefabAssetTags(const std::string& path, const std::vector<std::string>& tags);
    static bool GetPrefabAssetBasePrefab(const std::string& path, std::string& outBasePrefabPath);
    static bool HasCircularPrefabInheritance(const std::string& path, std::string* outCyclePath = nullptr);
    static bool SaveSelectedAsPrefab(const std::string& path);
    static bool SaveSelectedAsPrefabVariant(const std::string& path, const std::string& basePrefabPath);
    static bool InstantiatePrefab(const std::string& path, const DirectX::XMFLOAT3& position);
    static bool ApplySelectedToPrefab();
    static bool ApplySelectedPrefabProperty(PrefabProperty property);
    static bool RevertSelectedToPrefab();
    static bool RevertSelectedPrefabProperty(PrefabProperty property);
    static bool UnpackSelectedPrefabs();
    static bool TryGetPrefabOverrideState(uint32_t instanceId, PrefabOverrideState& outState);
    static bool SerializeInstances(const std::vector<uint32_t>& instanceIds, std::string& outData);
    static bool DeserializeInstances(const std::string& data, std::vector<uint32_t>& outRestoredIds);
    static bool SaveToFile(const std::string& path);
    static bool LoadFromFile(const std::string& path);
    static std::string SerializeToString();
    static bool LoadFromString(const std::string& content);

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
    static uint32_t s_HoveredInstanceId;
    static std::vector<uint32_t> s_SelectedInstanceIds;

    static D3D12_CPU_DESCRIPTOR_HANDLE s_InstanceSRVCpu;
    static D3D12_GPU_DESCRIPTOR_HANDLE s_InstanceSRVGpu;

    static Microsoft::WRL::ComPtr<ID3D12Resource> s_InstanceBufferDefault;
    static UINT s_InstanceBufferCapacity;
    static void EnsureInstanceBufferDefault(ID3D12Device* device, UINT requiredCount);

    static bool s_InstancesDirty;
    static uint64_t s_InstanceDataVersion;
    static uint64_t s_InstanceUploadedVersion;
};
