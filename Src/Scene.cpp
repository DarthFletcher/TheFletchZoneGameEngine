#include "Scene.h"

#include "Logger.h"
#include "ShaderUtils.h"
#include "CameraData.h"
#include "Graphics.h"
#include "MaterialManager.h"
#include "Vertex.h"
#include "Frustum.h"
#include "Bounds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

extern ID3D12DescriptorHeap* g_SRVHeap;

namespace
{
    using Microsoft::WRL::ComPtr;

    static constexpr const char* kSceneTestMaterialName = "TestMaterial";
    static constexpr const char* kSceneTestTexturePath = "Assets/Textures/crate.png";
    static bool g_loggedTestMaterialApplied = false;
    static CameraData g_LastRenderCameraData{};
    static bool g_HasLastRenderCameraData = false;

    struct Ray
    {
        DirectX::XMFLOAT3 origin{};
        DirectX::XMFLOAT3 direction{};
    };

    static SceneInstance* FindSceneInstanceByIdMutable(uint32_t instanceId)
    {
        if (instanceId == 0)
            return nullptr;

        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            SceneInstance* instance = Scene::GetInstance(i);
            if (instance && instance->instanceId == instanceId)
                return instance;
        }
        return nullptr;
    }

    static const SceneInstance* FindSceneInstanceByIdConst(uint32_t instanceId)
    {
        if (instanceId == 0)
            return nullptr;

        const auto& instances = Scene::GetInstances();
        for (const auto& instance : instances)
        {
            if (instance.instanceId == instanceId)
                return &instance;
        }
        return nullptr;
    }

    static DirectX::XMMATRIX BuildSceneInstanceLocal(const SceneInstance& instance)
    {
        using namespace DirectX;
        return XMMatrixScaling(instance.scale.x, instance.scale.y, instance.scale.z) *
            XMMatrixRotationRollPitchYaw(instance.rotation.x, instance.rotation.y, instance.rotation.z) *
            XMMatrixTranslation(instance.position.x, instance.position.y, instance.position.z);
    }

    static DirectX::XMMATRIX BuildSceneInstanceWorldRecursive(const SceneInstance& instance, int depth = 0)
    {
        using namespace DirectX;

        if (depth > 64)
            return BuildSceneInstanceLocal(instance);

        const XMMATRIX local = BuildSceneInstanceLocal(instance);
        if (instance.parentInstanceId == 0)
            return local;

        const SceneInstance* parent = FindSceneInstanceByIdConst(instance.parentInstanceId);
        if (!parent || parent->instanceId == instance.instanceId)
            return local;

        return XMMatrixMultiply(local, BuildSceneInstanceWorldRecursive(*parent, depth + 1));
    }

    static DirectX::XMMATRIX BuildSceneInstanceWorld(const SceneInstance& instance)
    {
        return BuildSceneInstanceWorldRecursive(instance);
    }

    static bool IsDescendantOf(uint32_t instanceId, uint32_t potentialAncestorId)
    {
        const SceneInstance* current = FindSceneInstanceByIdConst(instanceId);
        int guard = 0;
        while (current && current->parentInstanceId != 0 && guard++ < 64)
        {
            if (current->parentInstanceId == potentialAncestorId)
                return true;
            current = FindSceneInstanceByIdConst(current->parentInstanceId);
        }
        return false;
    }

    static bool DecomposeWorldToLocal(const DirectX::XMMATRIX& localMatrix, SceneInstance& instance)
    {
        using namespace DirectX;

        XMVECTOR scaleV{}, rotQ{}, transV{};
        if (!XMMatrixDecompose(&scaleV, &rotQ, &transV, localMatrix))
            return false;

        XMStoreFloat3(&instance.scale, scaleV);
        XMStoreFloat3(&instance.position, transV);

        XMFLOAT4 q{};
        XMStoreFloat4(&q, rotQ);
        const float ysqr = q.y * q.y;
        const float t0 = +2.0f * (q.w * q.x + q.y * q.z);
        const float t1 = +1.0f - 2.0f * (q.x * q.x + ysqr);
        const float t2 = (std::clamp)(+2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
        const float t3 = +2.0f * (q.w * q.z + q.x * q.y);
        const float t4 = +1.0f - 2.0f * (ysqr + q.z * q.z);

        instance.rotation.x = std::atan2(t0, t1);
        instance.rotation.y = std::asin(t2);
        instance.rotation.z = std::atan2(t3, t4);
        return true;
    }

    static DirectX::XMFLOAT4 ComputeInstanceColor(size_t index, size_t count)
    {
        const float denom = count > 1 ? float(count - 1) : 1.0f;
        const float t = float(index) / denom;
        return DirectX::XMFLOAT4(0.35f + 0.45f * t, 0.55f, 0.85f - 0.45f * t, 1.0f);
    }

    static std::string MakeSceneInstanceName(uint32_t instanceId)
    {
        return std::format("Cube_{:03}", instanceId);
    }

    static Ray ScreenPointToWorldRay(float mouseX, float mouseY, float viewportWidth, float viewportHeight, const CameraData& camera)
    {
        using namespace DirectX;

        const float w = (std::max)(viewportWidth, 1.0f);
        const float h = (std::max)(viewportHeight, 1.0f);
        const float px = (2.0f * mouseX / w) - 1.0f;
        const float py = 1.0f - (2.0f * mouseY / h);

        const XMVECTOR nearClip = XMVectorSet(px, py, 0.0f, 1.0f);
        const XMVECTOR farClip = XMVectorSet(px, py, 1.0f, 1.0f);

        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMMATRIX invViewProj = XMMatrixInverse(nullptr, XMMatrixMultiply(view, proj));

        XMVECTOR nearWorld = XMVector4Transform(nearClip, invViewProj);
        XMVECTOR farWorld = XMVector4Transform(farClip, invViewProj);
        nearWorld = XMVectorScale(nearWorld, 1.0f / XMVectorGetW(nearWorld));
        farWorld = XMVectorScale(farWorld, 1.0f / XMVectorGetW(farWorld));

        Ray ray{};
        XMStoreFloat3(&ray.origin, nearWorld);
        XMStoreFloat3(&ray.direction, XMVector3Normalize(XMVectorSubtract(farWorld, nearWorld)));
        return ray;
    }

    static bool RayIntersectsAABB(const Ray& ray, const DirectX::XMFLOAT3& mins, const DirectX::XMFLOAT3& maxs, float& outT)
    {
        constexpr float kEpsilon = 1e-6f;
        float tMin = 0.0f;
        float tMax = FLT_MAX;

        const float origin[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
        const float dir[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
        const float bmin[3] = { mins.x, mins.y, mins.z };
        const float bmax[3] = { maxs.x, maxs.y, maxs.z };

        for (int axis = 0; axis < 3; ++axis)
        {
            if (fabsf(dir[axis]) < kEpsilon)
            {
                if (origin[axis] < bmin[axis] || origin[axis] > bmax[axis])
                    return false;
                continue;
            }

            const float invDir = 1.0f / dir[axis];
            float t1 = (bmin[axis] - origin[axis]) * invDir;
            float t2 = (bmax[axis] - origin[axis]) * invDir;
            if (t1 > t2)
                std::swap(t1, t2);

            tMin = (std::max)(tMin, t1);
            tMax = (std::min)(tMax, t2);
            if (tMax < tMin)
                return false;
        }

        outT = tMin;
        return true;
    }

    static bool RayIntersectsOBB(const Ray& ray, const SceneInstance& instance, float& outT)
    {
        using namespace DirectX;

        const XMMATRIX world = BuildSceneInstanceWorld(instance);
        const XMMATRIX invWorld = XMMatrixInverse(nullptr, world);

        XMFLOAT3 localOrigin{};
        XMFLOAT3 localDir{};
        XMStoreFloat3(&localOrigin, XMVector3TransformCoord(XMLoadFloat3(&ray.origin), invWorld));
        XMStoreFloat3(&localDir, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&ray.direction), invWorld)));

        Ray localRay{};
        localRay.origin = localOrigin;
        localRay.direction = localDir;

        const DirectX::XMFLOAT3 mins{ -0.5f, -0.5f, -0.5f };
        const DirectX::XMFLOAT3 maxs{ +0.5f, +0.5f, +0.5f };
        return RayIntersectsAABB(localRay, mins, maxs, outT);
    }

    static bool FindClosestInstanceHit(const Ray& ray, int& outBestIndex, float& outBestT)
    {
        outBestIndex = -1;
        outBestT = FLT_MAX;

        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (!instances[i].visible)
                continue;

            float t = 0.0f;
            if (RayIntersectsOBB(ray, instances[i], t) && t < outBestT)
            {
                outBestT = t;
                outBestIndex = static_cast<int>(i);
            }
        }

        return outBestIndex != -1;
    }

    static bool TrySelectInstanceAtViewportPointInternal(float mouseX, float mouseY, float viewportWidth, float viewportHeight)
    {
        if (!g_HasLastRenderCameraData)
            return false;

        const Ray ray = ScreenPointToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, g_LastRenderCameraData);
        return Scene::TrySelectInstanceFromRay(ray.origin, ray.direction);
    }

    static void UpdateAnimatedInstances(
        std::vector<InstanceData>& instances,
        const std::vector<Sphere>& instanceBounds,
        float timeSeconds)
    {
        using namespace DirectX;

        for (size_t i = 0; i < instances.size() && i < instanceBounds.size(); ++i)
        {
            const XMFLOAT3& center = instanceBounds[i].Center;
            const float angle = timeSeconds + static_cast<float>(i) * 0.1f;

            const XMMATRIX world =
                XMMatrixRotationY(angle) *
                XMMatrixTranslation(center.x, center.y, center.z);

            XMStoreFloat4x4(&instances[i].World, XMMatrixTranspose(world));
        }
    }

#ifndef NDEBUG
    // Temporary isolation toggle for DEVICE_HUNG investigation.
    // When true, the main cube draw uses exactly 1 instance to isolate instancing/SRV issues.
    static bool g_DebugForceSingleInstanceDraw = false;
#endif

#ifndef NDEBUG
    static void LogVertexPCLayoutOnce()
    {
        static bool s_logged = false;
        if (s_logged)
            return;
        s_logged = true;

        char buffer[256] = {};
        sprintf_s(buffer,
            "[VertexAudit] VertexPC size=%zu posOff=%zu colorOff=%zu\n",
            sizeof(VertexPC),
            offsetof(VertexPC, Position),
            offsetof(VertexPC, Color));
        OutputDebugStringA(buffer);
    }
#endif

    static constexpr UINT Align256(UINT x)
    {
        return (x + 255u) & ~255u;
    }

    // Must exactly match `cbuffer SceneCB : register(b0)` layout in the HLSL.
    struct alignas(256) SceneCB
    {
        float viewProj[16];
        float cameraPos[3];
        float gridFadeDist = 0;
        float pad[44]{};
    };

    static_assert(sizeof(SceneCB) == 256, "SceneCB must be exactly 256 bytes");

    static constexpr UINT kSceneCbStride = Align256(sizeof(SceneCB));
    static constexpr UINT kMaxSceneObjects = 64;

    struct SceneObject
    {
        DirectX::XMFLOAT3 Position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Scale{ 1.0f, 1.0f, 1.0f };
    };

    static std::vector<SceneObject> g_sceneObjects;
    static bool g_loggedPhase45 = false;

    struct SceneDrawResources
    {
        ComPtr<ID3D12RootSignature> rootSig;

        ComPtr<ID3D12PipelineState> pso;

        // Grid pass (separate PSO + VB)
        ComPtr<ID3D12PipelineState> gridPso;
        ComPtr<ID3D12Resource> gridVb;
        D3D12_VERTEX_BUFFER_VIEW gridVbv{};

        ComPtr<ID3D12Resource> cb;
        SceneCB cbData{};
        void* cbCpu = nullptr;

        bool initFailed = false;
        bool initLogged = false;
    };

    static SceneDrawResources g_scene;
    static bool g_loggedInvalidCtx = false;
    static bool g_loggedMissingCamera = false;

    static void FillIdentity(float (&m)[16])
    {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
    }

    static D3D12_HEAP_PROPERTIES MakeHeapProps(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES p{};
        p.Type = type;
        p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        p.CreationNodeMask = 1;
        p.VisibleNodeMask = 1;
        return p;
    }

    static D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 size)
    {
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Alignment = 0;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.SampleDesc.Count = 1;
        d.SampleDesc.Quality = 0;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = D3D12_RESOURCE_FLAG_NONE;
        return d;
    }

    static void EnsureSceneObjectsInitialized()
    {
        // Legacy path retained for now; instance list is the new draw source-of-truth.
        if (!g_sceneObjects.empty())
            return;

        g_sceneObjects.push_back(SceneObject{ {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f} });
        g_sceneObjects.push_back(SceneObject{ {3.0f, 0.5f, 3.0f}, {0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f} });
    }

    static void EnsureTestSceneMaterial()
    {
        MaterialManager& materials = MaterialManager::GetInstance();
        Material* material = materials.GetMaterial(kSceneTestMaterialName);
        if (!material)
            material = materials.CreateMaterial(kSceneTestMaterialName);

        if (!material)
            return;

        if (!material->albedo)
        {
            Texture* texture = TextureManager::GetInstance().LoadTexture(kSceneTestTexturePath);
            if (texture)
                materials.SetAlbedoTexture(material, texture);
        }

        if (!g_loggedTestMaterialApplied && material->albedo)
        {
            g_loggedTestMaterialApplied = true;
            Logger::Log(LogLevel::Info, "Test material applied to scene", "Material");
        }
    }

    static void EnsureSceneResources(ID3D12Device* device)
    {
        Graphics::GetInstance().AssertNotInRender("EnsureSceneResources()");

#ifndef NDEBUG
        LogVertexPCLayoutOnce();
#endif

        if (!device)
            return;

        if (g_scene.initFailed)
            return;

        // Core resources for this pass.
        if (g_scene.rootSig && g_scene.pso && g_scene.gridPso && g_scene.gridVb)
            return;

        if (!g_scene.initLogged)
        {
            Logger::Log(LogLevel::Info, "Phase 4A: creating minimal scene draw resources (consume engine mesh)", "[Scene]");
            g_scene.initLogged = true;
        }

        // ---------------------------
        // Root Signature (b0 + SRV t0 + b1 + SRV t1)
        // ---------------------------
        if (!g_scene.rootSig)
        {
            CD3DX12_DESCRIPTOR_RANGE srvRanges[2]{};
            srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
            srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

            CD3DX12_ROOT_PARAMETER params[4]{};
            params[Graphics::SceneRootParamSceneCB].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0
            params[Graphics::SceneRootParamInstanceSRV].InitAsDescriptorTable(1, &srvRanges[0], D3D12_SHADER_VISIBILITY_VERTEX); // t0
            params[Graphics::SceneRootParamMaterialCB].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL); // b1
            params[Graphics::SceneRootParamMaterialAlbedoSRV].InitAsDescriptorTable(1, &srvRanges[1], D3D12_SHADER_VISIBILITY_PIXEL); // t1

            CD3DX12_STATIC_SAMPLER_DESC samplerDesc(
                0,
                D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                0.0f,
                16,
                D3D12_COMPARISON_FUNC_ALWAYS,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
                0.0f,
                D3D12_FLOAT32_MAX,
                D3D12_SHADER_VISIBILITY_PIXEL);

            CD3DX12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.Init(
                _countof(params), params,
                1, &samplerDesc,
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS);

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            const HRESULT hrSer = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
            if (FAILED(hrSer) || !sigBlob)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "D3D12SerializeRootSignature failed", "[Scene]");
                return;
            }

            const HRESULT hrRS = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_scene.rootSig));
            if (FAILED(hrRS) || !g_scene.rootSig)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "CreateRootSignature failed", "[Scene]");
                return;
            }
        }

        // ---------------------------
        // Shaders
        // ---------------------------
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        try
        {
            vs = CompileShaderFromRelativeFile(L"shaders\\scene_triangle_vs.hlsl", "main", "vs_5_1");
            ps = CompileShaderFromRelativeFile(L"shaders\\scene_triangle_ps.hlsl", "main", "ps_5_1");
        }
        catch (const std::exception& e)
        {
            g_scene.initFailed = true;
            Logger::Log(LogLevel::Error, std::string("shader compile failed: ") + e.what(), "[Scene]");
            return;
        }

        // ---------------------------
        // PSO (opaque, depth enabled)
        // ---------------------------
        if (!g_scene.pso)
        {
            D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)sizeof(float) * 3, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(VertexPC, UV), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_BLEND_DESC blend{};
            blend.AlphaToCoverageEnable = FALSE;
            blend.IndependentBlendEnable = FALSE;
            {
                auto& rt = blend.RenderTarget[0];
                rt.BlendEnable = FALSE;
                rt.LogicOpEnable = FALSE;
                rt.SrcBlend = D3D12_BLEND_ONE;
                rt.DestBlend = D3D12_BLEND_ZERO;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ZERO;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.LogicOp = D3D12_LOGIC_OP_NOOP;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }

            D3D12_RASTERIZER_DESC rast{};
            rast.FillMode = D3D12_FILL_MODE_SOLID;
            rast.CullMode = D3D12_CULL_MODE_BACK;
            rast.FrontCounterClockwise = FALSE;
            rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            rast.DepthClipEnable = TRUE;

            D3D12_DEPTH_STENCIL_DESC ds{};
            ds.DepthEnable = TRUE;
            ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            ds.StencilEnable = FALSE;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g_scene.rootSig.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState = blend;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = rast;
            psoDesc.DepthStencilState = ds;
            psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            psoDesc.SampleDesc.Quality = 0;
            psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

            const HRESULT hrPSO = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_scene.pso));
            if (FAILED(hrPSO) || !g_scene.pso)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "CreateGraphicsPipelineState failed", "[Scene]");
                return;
            }
        }

        // ---------------------------
        // Constant Buffer (scene-owned) removed.
        // CB data is uploaded via `Graphics::AllocateFrameCB()` per-frame.
        // ---------------------------
        if (!g_scene.cbData.viewProj[0])
        {
            g_scene.cbData = {};
            FillIdentity(g_scene.cbData.viewProj);
            g_scene.cbData.gridFadeDist = 10.0f;
        }

        // ---------------------------
        // Grid pass resources (separate PSO + VB)
        // ---------------------------
        if (!g_scene.gridPso)
        {
            ComPtr<ID3DBlob> gvs;
            ComPtr<ID3DBlob> gps;
            try
            {
                gvs = CompileShaderFromRelativeFile(L"shaders\\scene_grid_vs.hlsl", "main", "vs_5_1");
                gps = CompileShaderFromRelativeFile(L"shaders\\scene_grid_ps.hlsl", "main", "ps_5_1");
            }
            catch (const std::exception& e)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, std::string("grid shader compile failed: ") + e.what(), "[Scene]");
                return;
            }

            D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(VertexPC, Position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(VertexPC, Color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            D3D12_BLEND_DESC blend{};
            blend.AlphaToCoverageEnable = FALSE;
            blend.IndependentBlendEnable = FALSE;
            {
                auto& rt = blend.RenderTarget[0];
                rt.BlendEnable = TRUE;
                rt.LogicOpEnable = FALSE;
                rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.LogicOp = D3D12_LOGIC_OP_NOOP;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }

            D3D12_RASTERIZER_DESC rast{};
            rast.FillMode = D3D12_FILL_MODE_SOLID;
            rast.CullMode = D3D12_CULL_MODE_NONE;
            rast.DepthClipEnable = TRUE;

            D3D12_DEPTH_STENCIL_DESC ds{};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ds.StencilEnable = FALSE;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature = g_scene.rootSig.Get();
            psoDesc.VS = { gvs->GetBufferPointer(), gvs->GetBufferSize() };
            psoDesc.PS = { gps->GetBufferPointer(), gps->GetBufferSize() };
            psoDesc.BlendState = blend;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.RasterizerState = rast;
            psoDesc.DepthStencilState = ds;
            psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;
            psoDesc.SampleDesc.Quality = 0;
            psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

            const HRESULT hrPSO = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_scene.gridPso));
            if (FAILED(hrPSO) || !g_scene.gridPso)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "CreateGraphicsPipelineState(grid) failed", "[Scene]");
                return;
            }
        }

        if (!g_scene.gridVb)
        {
            constexpr int kDiv = 20;
            constexpr float kExtent = 10.0f;
            constexpr float kHalf = kExtent;
            constexpr float kStep = (kExtent * 2.0f) / (float)kDiv;

            std::vector<VertexPC> verts;
            verts.reserve((kDiv + 1) * 4);

            auto push = [&](float x, float y, float z, float r, float g, float b, float a)
            {
                VertexPC v{};
                v.Position = { x, y, z };
                v.Color = { r, g, b, a };
                verts.push_back(v);
            };

            for (int i = 0; i <= kDiv; ++i)
            {
                const float t = -kHalf + (float)i * kStep;

                constexpr float kGridY = -0.01f;

                if (i == kDiv / 2)
                {
                    // X axis
                    push(-kHalf, kGridY, t, 0.95f, 0.25f, 0.25f, 0.95f);
                    push(+kHalf, kGridY, t, 0.95f, 0.25f, 0.25f, 0.95f);

                    // Z axis
                    push(t, kGridY, -kHalf, 0.25f, 0.45f, 0.95f, 0.95f);
                    push(t, kGridY, +kHalf, 0.25f, 0.45f, 0.95f, 0.95f);
                }
                else
                {
                    const float c = 0.40f;
                    const float a = 0.65f;

                    push(-kHalf, kGridY, t, c, c, c, a);
                    push(+kHalf, kGridY, t, c, c, c, a);

                    push(t, kGridY, -kHalf, c, c, c, a);
                    push(t, kGridY, +kHalf, c, c, c, a);
                }
            }

            const UINT vbSize = (UINT)(sizeof(VertexPC) * verts.size());

            const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC bufDesc = MakeBufferDesc(vbSize);

            const HRESULT hrVB = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&g_scene.gridVb));

            if (FAILED(hrVB) || !g_scene.gridVb)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "CreateCommittedResource(GridVB) failed", "[Scene]");
                return;
            }

            void* mapped = nullptr;
            const HRESULT hrMap = g_scene.gridVb->Map(0, nullptr, &mapped);
            if (FAILED(hrMap) || !mapped)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "GridVB Map failed", "[Scene]");
                return;
            }
            std::memcpy(mapped, verts.data(), vbSize);
            g_scene.gridVb->Unmap(0, nullptr);

            g_scene.gridVbv.BufferLocation = g_scene.gridVb->GetGPUVirtualAddress();
            g_scene.gridVbv.SizeInBytes = vbSize;
            g_scene.gridVbv.StrideInBytes = sizeof(VertexPC);
        }
    }
}

std::vector<SceneInstance> Scene::s_SceneInstances;
std::vector<InstanceData> Scene::s_Instances;
SceneStats Scene::s_LastStats{};
uint32_t Scene::s_TargetInstanceCount = 25;
uint32_t Scene::s_NextInstanceId = 1;
bool Scene::s_SceneLayoutDirty = true;
std::vector<Sphere> Scene::s_InstanceBounds;
std::vector<UINT> Scene::s_VisibleInstanceIndices;
std::vector<InstanceData> Scene::s_VisibleInstancesScratch;
uint32_t Scene::s_SelectedInstanceId = 0;
uint32_t Scene::s_HoveredInstanceId = 0;
std::vector<uint32_t> Scene::s_SelectedInstanceIds;

const InstanceData* Scene::GetVisibleInstancesCPU()
{
    return s_VisibleInstancesScratch.empty() ? nullptr : s_VisibleInstancesScratch.data();
}

UINT Scene::GetVisibleInstanceCount()
{
    return (UINT)s_VisibleInstancesScratch.size();
}

Microsoft::WRL::ComPtr<ID3D12Resource> Scene::s_InstanceBufferDefault;
UINT Scene::s_InstanceBufferCapacity = 0;

D3D12_CPU_DESCRIPTOR_HANDLE Scene::s_InstanceSRVCpu = {};
D3D12_GPU_DESCRIPTOR_HANDLE Scene::s_InstanceSRVGpu = {};

// CP9-B: dirty tracking
bool Scene::s_InstancesDirty = true;
uint64_t Scene::s_InstanceDataVersion = 1;
uint64_t Scene::s_InstanceUploadedVersion = 0;

const InstanceData* Scene::GetInstancesCPU() { return s_Instances.empty() ? nullptr : s_Instances.data(); }
UINT Scene::GetInstanceCount() { return (UINT)s_SceneInstances.size(); }
uint64_t Scene::GetInstanceDataVersion() { return s_InstanceDataVersion; }
uint64_t Scene::GetInstanceUploadedVersion() { return s_InstanceUploadedVersion; }
void Scene::MarkInstancesDirty() { s_InstancesDirty = true; ++s_InstanceDataVersion; }
void Scene::MarkInstancesUploaded(uint64_t version) { s_InstancesDirty = false; s_InstanceUploadedVersion = version; }
ID3D12Resource* Scene::GetInstanceDefaultBuffer() { return s_InstanceBufferDefault.Get(); }
const std::vector<SceneInstance>& Scene::GetInstances() { return s_SceneInstances; }
SceneInstance* Scene::GetInstance(size_t index) { return index < s_SceneInstances.size() ? &s_SceneInstances[index] : nullptr; }
SceneInstance* Scene::GetSelectedInstance()
{
    if (s_SelectedInstanceId == 0)
        return nullptr;

    for (auto& instance : s_SceneInstances)
    {
        if (instance.instanceId == s_SelectedInstanceId)
            return &instance;
    }

    return nullptr;
}

bool Scene::TryGetLastRenderCameraData(CameraData& outCamera)
{
    if (!g_HasLastRenderCameraData)
        return false;

    outCamera = g_LastRenderCameraData;
    return true;
}

void Scene::EnsureInstanceBufferDefault(ID3D12Device* device, UINT requiredCount)
{
    if (!device)
        return;

    const UINT required = (std::max)(requiredCount, 1u);
    if (s_InstanceBufferDefault && required <= s_InstanceBufferCapacity)
        return;

    auto NextPowerOfTwo = [](UINT v) -> UINT
    {
        if (v == 0)
            return 1;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    };

    const UINT minCap = (std::max)(required, 32u);
    const UINT newCapacity = NextPowerOfTwo(minCap);
    const size_t bufferSize = sizeof(InstanceData) * (size_t)newCapacity;

    Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuf;
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_NONE);

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(defaultBuf.ReleaseAndGetAddressOf()));

        if (FAILED(hr) || !defaultBuf)
        {
            Logger::Log(LogLevel::Error, "EnsureInstanceBufferDefault: CreateCommittedResource(UPLOAD) failed", "[Scene]");
            return;
        }
    }

    // Allocate SRV descriptor once (engine-owned heap).
    if (s_InstanceSRVCpu.ptr == 0 || s_InstanceSRVGpu.ptr == 0)
    {
        s_InstanceSRVCpu = Graphics::AllocateSRV();

        const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = g_SRVHeap ? g_SRVHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
        s_InstanceSRVGpu.ptr = gpuStart.ptr + (UINT64)(s_InstanceSRVCpu.ptr - (g_SRVHeap ? g_SRVHeap->GetCPUDescriptorHandleForHeapStart().ptr : 0)) / inc * inc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = newCapacity;
    srvDesc.Buffer.StructureByteStride = sizeof(InstanceData);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(defaultBuf.Get(), &srvDesc, s_InstanceSRVCpu);

    s_InstanceBufferDefault = defaultBuf;
    s_InstanceBufferCapacity = newCapacity;

#ifndef NDEBUG
    static bool s_LoggedDefaultInstanceCapacity = false;
    if (!s_LoggedDefaultInstanceCapacity)
    {
        s_LoggedDefaultInstanceCapacity = true;
        char buf[256];
        sprintf_s(buf,
            "[Instancing] UPLOAD-backed instance buffer active | capacity=%u instances\n",
            s_InstanceBufferCapacity);
        OutputDebugStringA(buf);
    }
#endif

    Logger::Log(LogLevel::Info, std::format("[Scene] Instance upload buffer allocated | capacity={} bytes={}", s_InstanceBufferCapacity, bufferSize), "[Scene]");
}

namespace
{
    static float MaxBasisScaleFromWorld(const DirectX::XMMATRIX& W)
    {
        using namespace DirectX;

        const float sx = XMVectorGetX(XMVector3Length(W.r[0]));
        const float sy = XMVectorGetX(XMVector3Length(W.r[1]));
        const float sz = XMVectorGetX(XMVector3Length(W.r[2]));

        float s = (std::max)(sx, (std::max)(sy, sz));
        if (!std::isfinite(s) || s <= 0.0f)
            s = 1.0f;
        return s;
    }

    static Sphere ComputeInstanceSphereFromWorld(const DirectX::XMMATRIX& world)
    {
        static constexpr float kCubeLocalRadius = 0.8660254f;

        Sphere s{};
        DirectX::XMStoreFloat3(&s.Center, world.r[3]);

        const float maxScale = MaxBasisScaleFromWorld(world);
        s.Radius = kCubeLocalRadius * maxScale;
        return s;
    }
}

void Scene::RebuildRenderInstancesFromSceneData()
{
    using namespace DirectX;

    s_Instances.clear();
    s_Instances.reserve(s_SceneInstances.size());
    s_InstanceBounds.clear();
    s_InstanceBounds.reserve(s_SceneInstances.size());

    for (size_t i = 0; i < s_SceneInstances.size(); ++i)
    {
        const SceneInstance& sceneInstance = s_SceneInstances[i];
        const XMMATRIX world = BuildSceneInstanceWorld(sceneInstance);

        InstanceData inst{};
        XMStoreFloat4x4(&inst.World, XMMatrixTranspose(world));
        inst.Color = ComputeInstanceColor(i, s_SceneInstances.size());
        if (sceneInstance.instanceId == s_HoveredInstanceId)
            inst.Color = XMFLOAT4(1.0f, 0.92f, 0.35f, 1.0f);
        if (IsInstanceSelected(sceneInstance.instanceId))
            inst.Color = XMFLOAT4(1.0f, 0.62f, 0.20f, 2.0f);

        s_Instances.push_back(inst);
        s_InstanceBounds.push_back(ComputeInstanceSphereFromWorld(world));
    }
}

void Scene::EnsureInstancesInitialized()
{
    if (!s_SceneLayoutDirty)
    {
        ValidateSelection();
        return;
    }

    const int targetInstanceCount = (int)(std::max)(1u, s_TargetInstanceCount);

    s_SceneInstances.clear();
    s_SceneInstances.reserve((size_t)targetInstanceCount);
    s_Instances.clear();
    s_InstanceBounds.clear();
    s_SelectedInstanceId = 0;
    s_NextInstanceId = 1;

    const int dim = (int)ceilf(sqrtf((float)targetInstanceCount));

    for (int x = 0; x < dim; ++x)
    {
        for (int z = 0; z < dim; ++z)
        {
            if ((int)s_SceneInstances.size() >= targetInstanceCount)
                break;

            const int gx = x - dim / 2;
            const int gz = z - dim / 2;

            SceneInstance instance{};
            instance.instanceId = s_NextInstanceId++;
            instance.parentInstanceId = 0;
            instance.name = MakeSceneInstanceName(instance.instanceId);
            instance.position = { float(gx) * 3.0f, 0.5f, float(gz) * 3.0f };
            instance.rotation = { 0.0f, 0.0f, 0.0f };
            instance.scale = { 1.0f, 1.0f, 1.0f };
            instance.visible = true;
            instance.materialIndex = 0;
            s_SceneInstances.push_back(instance);
        }
        if ((int)s_SceneInstances.size() >= targetInstanceCount)
            break;
    }

    s_SceneLayoutDirty = false;
    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();

#ifndef NDEBUG
    static bool s_loggedBoundsOnce = false;
    if (!s_loggedBoundsOnce)
    {
        s_loggedBoundsOnce = true;
        const float r = s_InstanceBounds.empty() ? 0.0f : s_InstanceBounds.front().Radius;
        Logger::Log(LogLevel::Debug,
            "[Culling] Bounds built | instances=" + std::to_string(s_SceneInstances.size()) + " radius=" + std::to_string(r),
            "[Scene]");
    }
#endif
}

bool Scene::IsReady()
{
    return !g_scene.initFailed && g_scene.rootSig && g_scene.pso && g_scene.gridPso && g_scene.gridVb;
}

SceneStats Scene::GetLastStats()
{
    return s_LastStats;
}

void Scene::SetTargetInstanceCount(uint32_t count)
{
    s_TargetInstanceCount = (std::max)(1u, count);
    s_SceneLayoutDirty = true;
}

uint32_t Scene::GetTargetInstanceCount()
{
    return s_TargetInstanceCount;
}

void Scene::ValidateSelection()
{
    for (auto& instance : s_SceneInstances)
    {
        if (instance.parentInstanceId == instance.instanceId ||
            (instance.parentInstanceId != 0 && !FindSceneInstanceByIdConst(instance.parentInstanceId)) ||
            IsDescendantOf(instance.parentInstanceId, instance.instanceId))
        {
            instance.parentInstanceId = 0;
        }
    }

    s_SelectedInstanceIds.erase(
        std::remove_if(s_SelectedInstanceIds.begin(), s_SelectedInstanceIds.end(), [](uint32_t id)
            {
                return std::none_of(s_SceneInstances.begin(), s_SceneInstances.end(), [id](const SceneInstance& instance)
                    {
                        return instance.instanceId == id;
                    });
            }),
        s_SelectedInstanceIds.end());
}

void Scene::SetSelectedInstanceIndex(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= s_SceneInstances.size())
    {
        ClearSelection();
        return;
    }

    SetSelectedInstanceId(s_SceneInstances[(size_t)index].instanceId);
}

int Scene::GetSelectedInstanceIndex()
{
    if (s_SelectedInstanceId == 0)
        return -1;

    for (size_t i = 0; i < s_SceneInstances.size(); ++i)
    {
        if (s_SceneInstances[i].instanceId == s_SelectedInstanceId)
            return static_cast<int>(i);
    }

    return -1;
}

void Scene::SetSelectedInstanceId(uint32_t instanceId)
{
    s_SelectedInstanceId = instanceId;
    s_SelectedInstanceIds.clear();
    if (instanceId != 0)
        s_SelectedInstanceIds.push_back(instanceId);
    ValidateSelection();
}

uint32_t Scene::GetSelectedInstanceId()
{
    return s_SelectedInstanceId;
}

bool Scene::IsInstanceSelected(uint32_t instanceId)
{
    return std::find(s_SelectedInstanceIds.begin(), s_SelectedInstanceIds.end(), instanceId) != s_SelectedInstanceIds.end();
}

void Scene::AddSelectedInstanceId(uint32_t instanceId)
{
    if (instanceId == 0 || IsInstanceSelected(instanceId))
        return;

    s_SelectedInstanceIds.push_back(instanceId);
    s_SelectedInstanceId = instanceId;
    ValidateSelection();
}

void Scene::RemoveSelectedInstanceId(uint32_t instanceId)
{
    if (instanceId == 0)
        return;

    s_SelectedInstanceIds.erase(std::remove(s_SelectedInstanceIds.begin(), s_SelectedInstanceIds.end(), instanceId), s_SelectedInstanceIds.end());
    if (s_SelectedInstanceId == instanceId)
        s_SelectedInstanceId = s_SelectedInstanceIds.empty() ? 0 : s_SelectedInstanceIds.back();
    ValidateSelection();
}

void Scene::ToggleSelectedInstanceId(uint32_t instanceId)
{
    if (instanceId == 0)
        return;

    if (IsInstanceSelected(instanceId))
        RemoveSelectedInstanceId(instanceId);
    else
        AddSelectedInstanceId(instanceId);
}

void Scene::ClearSelection()
{
    s_SelectedInstanceId = 0;
    s_SelectedInstanceIds.clear();
}

const std::vector<uint32_t>& Scene::GetSelectedInstanceIds()
{
    return s_SelectedInstanceIds;
}

bool Scene::TryGetSelectionCenter(DirectX::XMFLOAT3& outCenter)
{
    if (s_SelectedInstanceIds.empty())
        return false;

    DirectX::XMFLOAT3 sum{ 0.0f, 0.0f, 0.0f };
    size_t count = 0;
    for (uint32_t id : s_SelectedInstanceIds)
    {
        const DirectX::XMFLOAT3 worldPos = GetInstanceWorldPosition(id);
        if (id == 0)
            continue;

        sum.x += worldPos.x;
        sum.y += worldPos.y;
        sum.z += worldPos.z;
        ++count;
    }

    if (count == 0)
        return false;

    const float invCount = 1.0f / static_cast<float>(count);
    outCenter = { sum.x * invCount, sum.y * invCount, sum.z * invCount };
    return true;
}

DirectX::XMFLOAT3 Scene::GetSelectionCenterOrActivePosition()
{
    DirectX::XMFLOAT3 center{};
    if (TryGetSelectionCenter(center))
        return center;

    return GetInstanceWorldPosition(s_SelectedInstanceId);
}

bool Scene::CanParentInstance(uint32_t childInstanceId, uint32_t parentInstanceId)
{
    if (childInstanceId == 0)
        return false;
    if (parentInstanceId == 0)
        return true;
    if (childInstanceId == parentInstanceId)
        return false;
    if (!FindSceneInstanceByIdConst(childInstanceId) || !FindSceneInstanceByIdConst(parentInstanceId))
        return false;
    return !IsDescendantOf(parentInstanceId, childInstanceId);
}

bool Scene::SetParentInstance(uint32_t childInstanceId, uint32_t parentInstanceId, bool keepWorldTransform)
{
    SceneInstance* child = FindSceneInstanceByIdMutable(childInstanceId);
    if (!child || !CanParentInstance(childInstanceId, parentInstanceId))
        return false;

    const DirectX::XMMATRIX childWorldBefore = BuildSceneInstanceWorld(*child);
    child->parentInstanceId = parentInstanceId;

    if (keepWorldTransform)
    {
        DirectX::XMMATRIX local = childWorldBefore;
        if (parentInstanceId != 0)
        {
            if (const SceneInstance* parent = FindSceneInstanceByIdConst(parentInstanceId))
            {
                const DirectX::XMMATRIX parentWorld = BuildSceneInstanceWorld(*parent);
                local = XMMatrixMultiply(childWorldBefore, XMMatrixInverse(nullptr, parentWorld));
            }
        }

        if (!DecomposeWorldToLocal(local, *child))
            return false;
    }

    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();
    return true;
}

uint32_t Scene::GetParentInstanceId(uint32_t instanceId)
{
    if (const SceneInstance* instance = FindSceneInstanceByIdConst(instanceId))
        return instance->parentInstanceId;
    return 0;
}

DirectX::XMFLOAT3 Scene::GetInstanceWorldPosition(uint32_t instanceId)
{
    if (const SceneInstance* instance = FindSceneInstanceByIdConst(instanceId))
    {
        DirectX::XMFLOAT4X4 world{};
        XMStoreFloat4x4(&world, BuildSceneInstanceWorld(*instance));
        return { world._41, world._42, world._43 };
    }
    return {};
}

bool Scene::TryGetInstanceWorldMatrix(uint32_t instanceId, DirectX::XMFLOAT4X4& outWorld)
{
    if (const SceneInstance* instance = FindSceneInstanceByIdConst(instanceId))
    {
        XMStoreFloat4x4(&outWorld, BuildSceneInstanceWorld(*instance));
        return true;
    }
    return false;
}

std::vector<uint32_t> Scene::GetChildInstanceIds(uint32_t parentInstanceId)
{
    std::vector<uint32_t> result;
    for (const auto& instance : s_SceneInstances)
    {
        if (instance.parentInstanceId == parentInstanceId)
            result.push_back(instance.instanceId);
    }
    return result;
}

bool Scene::TrySelectInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight)
{
    return TrySelectInstanceAtViewportPoint(mouseX, mouseY, viewportWidth, viewportHeight, false, false);
}

bool Scene::TrySelectInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir)
{
    return TrySelectInstanceFromRay(rayOrigin, rayDir, false, false);
}

bool Scene::TrySelectInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight, bool additive, bool toggle)
{
    if (!g_HasLastRenderCameraData)
        return false;

    const Ray ray = ScreenPointToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, g_LastRenderCameraData);
    return TrySelectInstanceFromRay(ray.origin, ray.direction, additive, toggle);
}

bool Scene::TrySelectInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir, bool additive, bool toggle)
{
    Ray ray{};
    ray.origin = rayOrigin;
    ray.direction = rayDir;

    int bestIndex = -1;
    float bestT = FLT_MAX;
    FindClosestInstanceHit(ray, bestIndex, bestT);

    if (bestIndex == -1)
    {
        if (!additive && !toggle)
            ClearSelection();
        return false;
    }

    const uint32_t instanceId = s_SceneInstances[(size_t)bestIndex].instanceId;
    if (toggle)
        ToggleSelectedInstanceId(instanceId);
    else if (additive)
        AddSelectedInstanceId(instanceId);
    else
        SetSelectedInstanceId(instanceId);

    return s_SelectedInstanceId != 0;
}

bool Scene::TryHoverInstanceAtViewportPoint(float mouseX, float mouseY, float viewportWidth, float viewportHeight)
{
    if (!g_HasLastRenderCameraData)
        return false;

    const Ray ray = ScreenPointToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, g_LastRenderCameraData);
    return TryHoverInstanceFromRay(ray.origin, ray.direction);
}

bool Scene::TryHoverInstanceFromRay(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir)
{
    Ray ray{};
    ray.origin = rayOrigin;
    ray.direction = rayDir;

    int bestIndex = -1;
    float bestT = FLT_MAX;
    if (!FindClosestInstanceHit(ray, bestIndex, bestT))
    {
        ClearHoveredInstance();
        return false;
    }

    s_HoveredInstanceId = s_SceneInstances[(size_t)bestIndex].instanceId;
    return true;
}

void Scene::ClearHoveredInstance()
{
    s_HoveredInstanceId = 0;
}

uint32_t Scene::GetHoveredInstanceId()
{
    return s_HoveredInstanceId;
}

bool Scene::GetSelectedInstanceTransform(DirectX::XMFLOAT3& outPosition, DirectX::XMFLOAT3& outRotation, DirectX::XMFLOAT3& outScale)
{
    SceneInstance* selected = GetSelectedInstance();
    if (!selected)
        return false;

    outPosition = selected->position;
    outRotation = selected->rotation;
    outScale = selected->scale;
    return true;
}

void Scene::DeleteSelectedInstance()
{
    const uint32_t selectedId = s_SelectedInstanceId;
    if (selectedId == 0)
        return;

    for (auto& instance : s_SceneInstances)
    {
        if (instance.parentInstanceId == selectedId)
        {
            SetParentInstance(instance.instanceId, 0, true);
        }
    }

    const auto it = std::find_if(s_SceneInstances.begin(), s_SceneInstances.end(), [selectedId](const SceneInstance& instance)
        {
            return instance.instanceId == selectedId;
        });
    if (it == s_SceneInstances.end())
        return;

    s_SceneInstances.erase(it);
    RemoveSelectedInstanceId(selectedId);
    if (s_HoveredInstanceId == selectedId)
        s_HoveredInstanceId = 0;
    s_TargetInstanceCount = static_cast<uint32_t>(s_SceneInstances.size());
    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();
}

void Scene::DuplicateSelectedInstance()
{
    SceneInstance* selected = GetSelectedInstance();
    if (!selected)
        return;

    SceneInstance copy = *selected;
    copy.instanceId = s_NextInstanceId++;
    copy.name += "_Copy";
    copy.position.x += 0.5f;
    copy.position.z += 0.5f;

    s_SceneInstances.push_back(copy);
    s_SelectedInstanceId = copy.instanceId;
    s_TargetInstanceCount = static_cast<uint32_t>(s_SceneInstances.size());
    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();
}

void Scene::CreateCube(const DirectX::XMFLOAT3& position)
{
    SceneInstance instance{};
    instance.instanceId = s_NextInstanceId++;
    instance.parentInstanceId = 0;
    instance.name = MakeSceneInstanceName(instance.instanceId);
    instance.position = position;
    instance.rotation = { 0.0f, 0.0f, 0.0f };
    instance.scale = { 1.0f, 1.0f, 1.0f };
    instance.visible = true;
    instance.materialIndex = 0;

    s_SceneInstances.push_back(instance);
    s_SelectedInstanceId = instance.instanceId;
    s_TargetInstanceCount = static_cast<uint32_t>(s_SceneInstances.size());
    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();
}

static std::string EscapeJsonString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value)
    {
        switch (c)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

static std::string UnescapeJsonString(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' && i + 1 < value.size())
        {
            ++i;
            switch (value[i])
            {
            case '\\': result += '\\'; break;
            case '"': result += '"'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: result += value[i]; break;
            }
        }
        else
        {
            result += value[i];
        }
    }
    return result;
}

std::string Scene::SerializeToString()
{
    std::ostringstream out;
    out << "{\n  \"sceneVersion\": 1,\n  \"instances\": [\n";
    for (size_t i = 0; i < s_SceneInstances.size(); ++i)
    {
        const SceneInstance& inst = s_SceneInstances[i];
        out << "    {\n";
        out << "      \"id\": " << inst.instanceId << ",\n";
        out << "      \"parent\": " << inst.parentInstanceId << ",\n";
        out << "      \"name\": \"" << EscapeJsonString(inst.name) << "\",\n";
        out << "      \"position\": [" << inst.position.x << ", " << inst.position.y << ", " << inst.position.z << "],\n";
        out << "      \"rotation\": [" << inst.rotation.x << ", " << inst.rotation.y << ", " << inst.rotation.z << "],\n";
        out << "      \"scale\": [" << inst.scale.x << ", " << inst.scale.y << ", " << inst.scale.z << "],\n";
        out << "      \"visible\": " << (inst.visible ? "true" : "false") << ",\n";
        out << "      \"material\": " << inst.materialIndex << "\n";
        out << "    }" << (i + 1 < s_SceneInstances.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

bool Scene::LoadFromString(const std::string& content)
{
    s_SceneInstances.clear();
    s_SelectedInstanceId = 0;
    s_HoveredInstanceId = 0;
    s_SelectedInstanceIds.clear();

    size_t searchPos = 0;
    uint32_t maxId = 0;

    while ((searchPos = content.find("\"id\"", searchPos)) != std::string::npos)
    {
        auto readNumber = [&](const std::string& key, size_t fromPos, float& outValue) -> bool
        {
            const size_t keyPos = content.find(key, fromPos);
            if (keyPos == std::string::npos) return false;
            const size_t colon = content.find(':', keyPos);
            if (colon == std::string::npos) return false;
            size_t begin = content.find_first_of("-0123456789", colon);
            if (begin == std::string::npos) return false;
            size_t end = content.find_first_not_of("-+.0123456789eE", begin);
            outValue = std::stof(content.substr(begin, end - begin));
            return true;
        };
        auto readBool = [&](const std::string& key, size_t fromPos, bool& outValue) -> bool
        {
            const size_t keyPos = content.find(key, fromPos);
            if (keyPos == std::string::npos) return false;
            const size_t colon = content.find(':', keyPos);
            if (colon == std::string::npos) return false;
            const size_t begin = content.find_first_not_of(" \t\r\n", colon + 1);
            if (begin == std::string::npos) return false;
            if (content.compare(begin, 4, "true") == 0) { outValue = true; return true; }
            if (content.compare(begin, 5, "false") == 0) { outValue = false; return true; }
            return false;
        };
        auto readString = [&](const std::string& key, size_t fromPos, std::string& outValue) -> bool
        {
            const size_t keyPos = content.find(key, fromPos);
            if (keyPos == std::string::npos) return false;
            const size_t colon = content.find(':', keyPos);
            const size_t begin = content.find('"', colon + 1);
            if (colon == std::string::npos || begin == std::string::npos) return false;
            size_t end = begin + 1;
            while ((end = content.find('"', end)) != std::string::npos)
            {
                if (content[end - 1] != '\\')
                    break;
                ++end;
            }
            if (end == std::string::npos) return false;
            outValue = UnescapeJsonString(content.substr(begin + 1, end - begin - 1));
            return true;
        };
        auto readVector3 = [&](const std::string& key, size_t fromPos, DirectX::XMFLOAT3& outValue) -> bool
        {
            const size_t keyPos = content.find(key, fromPos);
            if (keyPos == std::string::npos) return false;
            const size_t open = content.find('[', keyPos);
            const size_t close = content.find(']', open);
            if (open == std::string::npos || close == std::string::npos) return false;
            std::string values = content.substr(open + 1, close - open - 1);
            std::replace(values.begin(), values.end(), ',', ' ');
            std::istringstream stream(values);
            stream >> outValue.x >> outValue.y >> outValue.z;
            return !stream.fail();
        };

        SceneInstance inst{};
        float idValue = 0.0f;
        float materialValue = 0.0f;
        float parentValue = 0.0f;
        if (!readNumber("\"id\"", searchPos, idValue)) break;
        if (!readNumber("\"parent\"", searchPos, parentValue)) parentValue = 0.0f;
        if (!readString("\"name\"", searchPos, inst.name)) break;
        if (!readVector3("\"position\"", searchPos, inst.position)) break;
        if (!readVector3("\"rotation\"", searchPos, inst.rotation)) break;
        if (!readVector3("\"scale\"", searchPos, inst.scale)) break;
        if (!readBool("\"visible\"", searchPos, inst.visible)) break;
        if (!readNumber("\"material\"", searchPos, materialValue)) break;

        inst.instanceId = static_cast<uint32_t>(idValue);
        inst.parentInstanceId = static_cast<uint32_t>(parentValue);
        inst.materialIndex = static_cast<int>(materialValue);
        s_SceneInstances.push_back(inst);
        maxId = (std::max)(maxId, inst.instanceId);
        ++searchPos;
    }

    s_NextInstanceId = maxId + 1;
    s_TargetInstanceCount = static_cast<uint32_t>(s_SceneInstances.size());
    s_SceneLayoutDirty = false;
    RebuildRenderInstancesFromSceneData();
    MarkInstancesDirty();
    return true;
}

bool Scene::SaveToFile(const std::string& path)
{
    std::filesystem::path outPath(path);
    if (outPath.has_parent_path())
        std::filesystem::create_directories(outPath.parent_path());

    std::ofstream file(outPath, std::ios::trunc);
    if (!file.is_open())
    {
        Logger::Log(LogLevel::Error, std::format("Failed to open scene file for save: {}", path), "[Scene]");
        return false;
    }

    file << SerializeToString();
    if (!file.good())
        return false;

    Logger::Log(LogLevel::Info, std::format("Saved scene: {}", path), "[Scene]");
    return true;
}

bool Scene::LoadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        Logger::Log(LogLevel::Error, std::format("Failed to open scene file for load: {}", path), "[Scene]");
        return false;
    }

    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!LoadFromString(content))
        return false;

    Logger::Log(LogLevel::Info, std::format("Loaded scene: {}", path), "[Scene]");
    return true;
}

void Scene::InitializeResources(ID3D12Device* device)
{
    Graphics::GetInstance().AssertNotInRender("Scene::InitializeResources()");

    if (!device)
        return;

    EnsureInstancesInitialized();
    RebuildRenderInstancesFromSceneData();
    EnsureInstanceBufferDefault(device, (UINT)s_Instances.size());
    (void)TextureManager::GetInstance().GetWhiteTexture();
    EnsureTestSceneMaterial();
    EnsureSceneResources(device);
}

void Scene::Render(const SceneRenderContext& ctx)
{
    s_LastStats = {};

    // ====================================================================
    // Phase 4A: PASS BOUNDARY & OWNERSHIP NOTES
    // ====================================================================

    // This function is a CONSUMER of the command list, not an owner.
    // It must NOT:
    //   - Open or close the command list
    //   - Transition the main window backbuffer
    //   - Signal fences or flush the GPU
    //   - Assume any specific GPU state (PSO, heaps, viewports)
    //
    // Ownership:
    //   - Scene RT transitions: owned by Graphics::RenderSceneToTarget()
    //   - Command list lifecycle: owned by Graphics::BeginFrame/EndFrame()
    //   - Camera data: owned by CameraSystem, read-only here via ctx.camera
    //
    // State assumptions:
    //   - Command list is open and recording
    //   - Scene RT is in RENDER_TARGET state (set by caller)
    //   - g_SRVHeap may or may not be bound (we re-bind defensively)
    //
    // ImGui bleed: ImGui may have left PSO/heap/viewport bound. We reset all.
    // ====================================================================

    if (!ctx.device || !ctx.commandList)
    {
        if (!g_loggedInvalidCtx)
        {
            Logger::Log(LogLevel::Error, "Scene::Render called with null device/commandList", "[Scene]");
            g_loggedInvalidCtx = true;
        }
        return;
    }

    if (!ctx.camera)
    {
        if (!g_loggedMissingCamera)
        {
            Logger::Log(LogLevel::Error, "Phase 3C: Scene::Render missing ctx.camera (camera ownership not wired)", "[Scene]");
            g_loggedMissingCamera = true;
        }
        return;
    }

    g_LastRenderCameraData = *ctx.camera;
    g_HasLastRenderCameraData = true;

    EnsureInstancesInitialized();
    RebuildRenderInstancesFromSceneData();

#ifndef NDEBUG
    {
        static auto s_initLogLast = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        const auto nowInit = std::chrono::steady_clock::now();
        if (nowInit - s_initLogLast >= std::chrono::seconds(1))
        {
            s_initLogLast = nowInit;
            Logger::Log(LogLevel::Debug,
                std::format("[Scene] EnsureInstancesInitialized | instances={} bounds={}", s_Instances.size(), s_InstanceBounds.size()),
                "[Scene]");
        }
    }
#endif

    EnsureSceneObjectsInitialized();
    if (!g_loggedPhase45)
    {
        g_loggedPhase45 = true;
        Logger::Log(LogLevel::Info, "[Scene] Phase 4A.5 multi-instance rendering active");
    }

    // Editor transforms are authoritative; do not mutate render instances during draw.

    // Clamp to guard against accidental invalid sizes.
    constexpr uint32_t kMin = 1u;
    constexpr uint32_t kMax = 16384u;

    const uint32_t w = (std::min)(kMax, (std::max)(kMin, ctx.viewportWidth));
    const uint32_t h = (std::min)(kMax, (std::max)(kMin, ctx.viewportHeight));

    // Never create GPU resources during the render phase.
    // Scene resources must be created from a non-render phase (e.g. Graphics::Initialize / BeginFrame).
    if (!IsReady())
        return;

    // Phase 4A.5: compute view-projection (NOT transposed).
    DirectX::XMMATRIX viewProj;
    DirectX::XMFLOAT4X4 viewProjF{};
    {
        using namespace DirectX;
        const XMMATRIX view = XMLoadFloat4x4(&ctx.camera->view);
        const XMMATRIX proj = XMLoadFloat4x4(&ctx.camera->proj);
        viewProj = XMMatrixMultiply(view, proj);

        XMStoreFloat4x4(&viewProjF, viewProj);

        g_scene.cbData.cameraPos[0] = ctx.camera->position.x;
        g_scene.cbData.cameraPos[1] = ctx.camera->position.y;
        g_scene.cbData.cameraPos[2] = ctx.camera->position.z;
        g_scene.cbData.gridFadeDist = 10.0f;
    }

    // CP11-A: extract frustum planes from ViewProj (CPU-only; no filtering yet).
    const Frustum fr = BuildFrustumFromViewProj(viewProjF);

    // CP11-C: build visible list
    s_VisibleInstanceIndices.clear();
    s_VisibleInstanceIndices.reserve(s_InstanceBounds.size());

    for (UINT i = 0; i < (UINT)s_InstanceBounds.size(); ++i)
    {
        if (i < s_SceneInstances.size() && s_SceneInstances[i].visible && SphereInsideFrustum(s_InstanceBounds[i], fr))
            s_VisibleInstanceIndices.push_back(i);
    }

#ifndef NDEBUG
    static auto s_lastVisibleLog = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    const auto nowVis = std::chrono::steady_clock::now();
    if (nowVis - s_lastVisibleLog >= std::chrono::seconds(1))
    {
        s_lastVisibleLog = nowVis;
        Logger::Log(LogLevel::Debug,
            "[Culling] Visible instances = " + std::to_string(s_VisibleInstanceIndices.size()) +
            " / " + std::to_string(s_InstanceBounds.size()),
            "[Scene]");
    }
#endif

    // CP11-D: build contiguous visible instance buffer (CPU scratch)
    s_VisibleInstancesScratch.clear();
    s_VisibleInstancesScratch.reserve(s_VisibleInstanceIndices.size());
    for (UINT idx : s_VisibleInstanceIndices)
    {
        if (idx < s_Instances.size())
        {
            InstanceData inst = s_Instances[idx];
            s_VisibleInstancesScratch.push_back(inst);
        }
    }

    s_LastStats.totalObjects = static_cast<uint32_t>(s_SceneInstances.size());
    s_LastStats.visibleObjects = static_cast<uint32_t>(s_VisibleInstancesScratch.size());

    if (s_InstanceBufferDefault && !s_VisibleInstancesScratch.empty())
    {
        void* mapped = nullptr;
        const HRESULT hrMap = s_InstanceBufferDefault->Map(0, nullptr, &mapped);
        if (SUCCEEDED(hrMap) && mapped)
        {
            const size_t bytes = sizeof(InstanceData) * s_VisibleInstancesScratch.size();
            std::memcpy(mapped, s_VisibleInstancesScratch.data(), bytes);
            s_InstanceBufferDefault->Unmap(0, nullptr);
        }
        else
        {
            Logger::Log(LogLevel::Error, std::format(
                "[Scene] Failed to map instance buffer for visible upload HR=0x{:08X}",
                (UINT)hrMap), "[Scene]");
        }
    }

    // ====================================================================
    // Phase 4A: EXPLICIT STATE SETUP
    // ====================================================================
    // WHY: ImGui or other passes may have left incompatible state bound.
    // D3D12 does NOT auto-reset state between draws. We must explicitly
    // bind our own PSO, root signature, heaps, and dynamic state.
    //
    // Order matters:
    //   1. Descriptor heaps (required before any SRV/CBV bindings)
    //   2. Viewport + scissor (defines rasterization region)
    //   3. Root signature (defines shader bindings)
    //   4. Blend factor + stencil ref (dynamic state that persists)
    //   5. CBV (constant buffer for transforms)
    //   6. PSO (defines shaders, blend, rasterizer, depth)
    //   7. IA state (topology, vertex buffers)
    //   8. Draw calls
    // ====================================================================

    // ---------------------------
    // Explicit state setup (no ImGui bleed)
    // ---------------------------
    ID3D12DescriptorHeap* heaps[] = { g_SRVHeap };
    if (g_SRVHeap)
        ctx.commandList->SetDescriptorHeaps(1, heaps);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT sc{};
    sc.left = 0;
    sc.top = 0;
    sc.right = (LONG)w;
    sc.bottom = (LONG)h;

    ctx.commandList->RSSetViewports(1, &vp);
    ctx.commandList->RSSetScissorRects(1, &sc);

    // Bind shared root signature once (both PSOs compatible).
    ctx.commandList->SetGraphicsRootSignature(g_scene.rootSig.Get());

    // Bind instance SRV table (t0) at root param 1.
    if (s_InstanceSRVGpu.ptr != 0)
        ctx.commandList->SetGraphicsRootDescriptorTable(Graphics::SceneRootParamInstanceSRV, s_InstanceSRVGpu);

    // Fully define dynamic state that can bleed between draws.
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx.commandList->OMSetBlendFactor(blendFactor);
    ctx.commandList->OMSetStencilRef(0);

    // Common CB: bind a fresh slice (shared root signature for both PSOs).
    {
        Graphics& gfx = Graphics::GetInstance();

        // Per-frame CB content: view-projection only (instancing supplies World/Color).
        DirectX::XMFLOAT4X4 vpStore{};
        DirectX::XMStoreFloat4x4(&vpStore, DirectX::XMMatrixTranspose(viewProj));
        memcpy(g_scene.cbData.viewProj, &vpStore, sizeof(float) * 16);

        const auto alloc = gfx.AllocateFrameCB(sizeof(SceneCB));
        std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));

#ifndef NDEBUG
        if ((alloc.GpuAddress & 0xFFull) != 0)
        {
            Logger::Log(LogLevel::Error,
                std::format("[CBV] Misaligned CB GPU VA for b0: 0x{:X}", (uint64_t)alloc.GpuAddress),
                "[Scene]");
        }
        else
        {
            Logger::Log(LogLevel::Debug,
                std::format("[CBV] b0 GPU VA: 0x{:X}", (uint64_t)alloc.GpuAddress),
                "[Scene]");
        }
#endif

        ctx.commandList->SetGraphicsRootConstantBufferView(Graphics::SceneRootParamSceneCB, alloc.GpuAddress);
    }

    Material* material = MaterialManager::GetInstance().GetMaterial(kSceneTestMaterialName);
    if (!material)
        material = MaterialManager::GetInstance().CreateMaterial(kSceneTestMaterialName);
    Graphics::GetInstance().BindMaterial(material);

#ifndef NDEBUG
    static auto s_lastInstancingCountsLog = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    const auto nowInst = std::chrono::steady_clock::now();
    if (nowInst - s_lastInstancingCountsLog >= std::chrono::seconds(1))
    {
        s_lastInstancingCountsLog = nowInst;
        Logger::Log(LogLevel::Debug,
            "[Instancing] Counts | total=" + std::to_string(s_Instances.size()) +
            " visible=" + std::to_string(s_VisibleInstanceIndices.size()) +
            " scratch=" + std::to_string(s_VisibleInstancesScratch.size()),
            "[Scene]");
    }
#endif

    // ====================================================================
    // Phase 4A: OPAQUE GEOMETRY PASS (Engine-owned cube, instanced)
    // ====================================================================
    ctx.commandList->SetPipelineState(g_scene.pso.Get());
    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const MeshData& cube = Graphics::GetInstance().GetCubeMesh();
    if (cube.IndexCount > 0)
    {
        ctx.commandList->IASetVertexBuffers(0, 1, &cube.VBV);
        ctx.commandList->IASetIndexBuffer(&cube.IBV);

        const UINT instanceCount = (UINT)s_VisibleInstancesScratch.size();
#ifndef NDEBUG
        {
            static auto s_drawLast = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            const auto nowDraw = std::chrono::steady_clock::now();
            if (nowDraw - s_drawLast >= std::chrono::seconds(1))
            {
                s_drawLast = nowDraw;
                Logger::Log(LogLevel::Debug, std::format("[Scene] DrawIndexedInstanced | visible={}", (size_t)instanceCount), "[Scene]");
            }
        }
#endif
        if (instanceCount > 0)
        {
#ifndef NDEBUG
            const UINT drawInstances = g_DebugForceSingleInstanceDraw ? 1u : instanceCount;
#else
            const UINT drawInstances = instanceCount;
#endif
            ctx.commandList->DrawIndexedInstanced(cube.IndexCount, drawInstances, 0, 0, 0);
            s_LastStats.drawCalls++;
        }
    }

    // For subsequent passes (grid), re-bind a fresh slice.
    {
        Graphics& gfx = Graphics::GetInstance();
        const auto alloc = gfx.AllocateFrameCB(sizeof(SceneCB));
        std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));

#ifndef NDEBUG
        if ((alloc.GpuAddress & 0xFFull) != 0)
        {
            Logger::Log(LogLevel::Error,
                std::format("[CBV] Misaligned CB GPU VA for b0 (grid rebind): 0x{:X}", (uint64_t)alloc.GpuAddress),
                "[Scene]");
        }
        else
        {
            Logger::Log(LogLevel::Debug,
                std::format("[CBV] b0 GPU VA (grid rebind): 0x{:X}", (uint64_t)alloc.GpuAddress),
                "[Scene]");
        }
#endif

        ctx.commandList->SetGraphicsRootConstantBufferView(Graphics::SceneRootParamSceneCB, alloc.GpuAddress);
    }

    // ====================================================================
    // Phase 4A: GRID OVERLAY PASS (Lines)
    // ====================================================================
#ifndef NDEBUG
    constexpr bool kDisableGridOverlayForDiag = false;
#else
    constexpr bool kDisableGridOverlayFor_diag = false;
#endif

    if (!kDisableGridOverlayForDiag)
    {
        ctx.commandList->SetPipelineState(g_scene.gridPso.Get());
        ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        ctx.commandList->IASetVertexBuffers(0, 1, &g_scene.gridVbv);
        ctx.commandList->DrawInstanced(g_scene.gridVbv.SizeInBytes / g_scene.gridVbv.StrideInBytes, 1, 0, 0);
    }

#ifndef NDEBUG
    {
        static auto s_lastSceneStatsLog = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        const auto nowStats = std::chrono::steady_clock::now();
        if (nowStats - s_lastSceneStatsLog >= std::chrono::seconds(1))
        {
            s_lastSceneStatsLog = nowStats;
            Logger::Log(LogLevel::Debug, std::format(
                "SceneStats: total={} visible={} draws={}",
                s_LastStats.totalObjects,
                s_LastStats.visibleObjects,
                s_LastStats.drawCalls),
                "ScenePass");
        }
    }
#endif

    (void)ctx.frameIndex;
}
