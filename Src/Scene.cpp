#include "Scene.h"

#include "Logger.h"
#include "ShaderUtils.h"
#include "CameraData.h"
#include "Graphics.h"
#include "Vertex.h"
#include "Frustum.h"
#include "Bounds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
        // Root Signature (b0 + SRV t0)
        // ---------------------------
        if (!g_scene.rootSig)
        {
            CD3DX12_DESCRIPTOR_RANGE srvRange{};
            srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

            CD3DX12_ROOT_PARAMETER params[2]{};
            params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0
            params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_VERTEX); // t0

            CD3DX12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.Init(
                _countof(params), params,
                0, nullptr,
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

                const bool isAxis = (i == kDiv / 2);
                const float c = isAxis ? 0.85f : 0.40f;
                const float a = isAxis ? 0.95f : 0.65f;

                constexpr float kGridY = -0.01f;

                push(-kHalf, kGridY, t, c, c, c, a);
                push(+kHalf, kGridY, t, c, c, c, a);

                push(t, kGridY, -kHalf, c, c, c, a);
                push(t, kGridY, +kHalf, c, c, c, a);
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

std::vector<InstanceData> Scene::s_Instances;
std::vector<Sphere> Scene::s_InstanceBounds;
std::vector<UINT> Scene::s_VisibleInstanceIndices;
std::vector<InstanceData> Scene::s_VisibleInstancesScratch;

// CP13 debug viz toggles/knobs (editor-controlled)
bool Scene::g_ShowFrustum = true;
bool Scene::g_ShowBounds = true;
bool Scene::g_FreezeCulling = false;
int  Scene::g_BoundsSegments = 24;

namespace
{
    struct DebugLineVertex
    {
        DirectX::XMFLOAT3 Pos{};
        uint32_t ColorRGBA = 0;
    };

    static std::vector<DebugLineVertex> g_DebugLineVerts;

    static void DebugAddLine(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, uint32_t rgba)
    {
        g_DebugLineVerts.push_back({ a, rgba });
        g_DebugLineVerts.push_back({ b, rgba });
    }

    static void DebugAddWireSphere(const DirectX::XMFLOAT3& c, float r, uint32_t rgba, int segs)
    {
        segs = (std::max)(segs, 8);

        auto addCircle = [&](int axisA, int axisB)
        {
            for (int i = 0; i < segs; ++i)
            {
                const float t0 = (DirectX::XM_2PI * float(i + 0)) / float(segs);
                const float t1 = (DirectX::XM_2PI * float(i + 1)) / float(segs);

                DirectX::XMFLOAT3 p0 = c;
                DirectX::XMFLOAT3 p1 = c;

                const float cs0 = cosf(t0), sn0 = sinf(t0);
                const float cs1 = cosf(t1), sn1 = sinf(t1);

                float* fp0[3] = { &p0.x, &p0.y, &p0.z };
                float* fp1[3] = { &p1.x, &p1.y, &p1.z };

                *fp0[axisA] += r * cs0; *fp0[axisB] += r * sn0;
                *fp1[axisA] += r * cs1; *fp1[axisB] += r * sn1;

                DebugAddLine(p0, p1, rgba);
            }
        };

        addCircle(0, 1); // XY
        addCircle(0, 2); // XZ
        addCircle(1, 2); // YZ
    }

    static void ExtractFrustumCornersWorld(const DirectX::XMMATRIX& viewProj, DirectX::XMFLOAT3 outCorners[8])
    {
        using namespace DirectX;

        const XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

        // D3D NDC: x,y in [-1,+1], z in [0,+1]
        const XMFLOAT3 ndc[8] = {
            {-1,-1,0}, {+1,-1,0}, {+1,+1,0}, {-1,+1,0},
            {-1,-1,1}, {+1,-1,1}, {+1,+1,1}, {-1,+1,1}
        };

        for (int i = 0; i < 8; ++i)
        {
            XMVECTOR p = XMVectorSet(ndc[i].x, ndc[i].y, ndc[i].z, 1.0f);
            p = XMVector4Transform(p, invVP);
            p = XMVectorScale(p, 1.0f / XMVectorGetW(p));
            XMStoreFloat3(&outCorners[i], p);
        }
    }

    static constexpr int kFrustumEdges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
}

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
UINT Scene::GetInstanceCount() { return (UINT)s_Instances.size(); }
uint64_t Scene::GetInstanceDataVersion() { return s_InstanceDataVersion; }
uint64_t Scene::GetInstanceUploadedVersion() { return s_InstanceUploadedVersion; }
void Scene::MarkInstancesDirty() { s_InstancesDirty = true; ++s_InstanceDataVersion; }
void Scene::MarkInstancesUploaded(uint64_t version) { s_InstancesDirty = false; s_InstanceUploadedVersion = version; }
ID3D12Resource* Scene::GetInstanceDefaultBuffer() { return s_InstanceBufferDefault.Get(); }

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
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
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
            Logger::Log(LogLevel::Error, "EnsureInstanceBufferDefault: CreateCommittedResource(DEFAULT) failed", "[Scene]");
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

#ifdef _DEBUG
    static bool s_LoggedDefaultInstanceCapacity = false;
    if (!s_LoggedDefaultInstanceCapacity)
    {
        s_LoggedDefaultInstanceCapacity = true;
        char buf[256];
        sprintf_s(buf,
            "[Instancing] DEFAULT instance buffer active | capacity=%u instances\n",
            s_InstanceBufferCapacity);
        OutputDebugStringA(buf);
    }
#endif

    Logger::Log(LogLevel::Info, std::format("[Scene] Instance DEFAULT buffer allocated | capacity={} bytes={}", s_InstanceBufferCapacity, bufferSize), "[Scene]");
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
        // Canonical centered unit cube bounds. ([-0.5,+0.5] on each axis)
        static constexpr float kCubeLocalRadius = 0.8660254f;

        Sphere s{};

        // Translation is in row3 for DirectXMath row-vector usage.
        DirectX::XMStoreFloat3(&s.Center, world.r[3]);

        const float maxScale = MaxBasisScaleFromWorld(world);
        s.Radius = kCubeLocalRadius * maxScale;
        return s;
    }
}

void Scene::EnsureInstancesInitialized()
{
    // Allow stress scaling via ImGui (CPU-side only).
    static int s_targetInstanceCount = 25;
    static int s_lastAppliedCount = -1;

    // Pull desired instance count from the Instancing panel (stored in ImGui state storage).
    if (ImGui::GetCurrentContext())
    {
        ImGuiStorage* store = ImGui::GetStateStorage();
        if (store)
        {
            const int desired = store->GetInt(ImGui::GetID("TFZ_Instancing_TargetCount"), s_targetInstanceCount);
            s_targetInstanceCount = (std::max)(1, desired);
        }
    }

    if (!s_Instances.empty() && s_lastAppliedCount == s_targetInstanceCount)
        return;

    s_lastAppliedCount = s_targetInstanceCount;

    s_Instances.clear();
    s_Instances.reserve((size_t)s_targetInstanceCount);

    s_InstanceBounds.clear();
    s_InstanceBounds.reserve((size_t)s_targetInstanceCount);

    // Deterministic grid fill.
    // Fill a square grid of ceil(sqrt(N)) x ceil(sqrt(N)), truncating to N.
    const int dim = (int)ceilf(sqrtf((float)s_targetInstanceCount));

    for (int x = 0; x < dim; ++x)
    {
        for (int z = 0; z < dim; ++z)
        {
            if ((int)s_Instances.size() >= s_targetInstanceCount)
                break;

            const int gx = x - dim / 2;
            const int gz = z - dim / 2;

            InstanceData inst{};

            const DirectX::XMMATRIX world = DirectX::XMMatrixTranslation(
                float(gx) * 3.0f,
                0.5f,
                float(gz) * 3.0f);

            // Bounds must be computed from the non-transposed CPU world matrix.
            const Sphere bounds = ComputeInstanceSphereFromWorld(world);

            // HLSL structured buffers default to column-major matrices.
            // Store the transpose so `mul(float4(pos,1), World)` produces correct world space.
            const DirectX::XMMATRIX worldT = DirectX::XMMatrixTranspose(world);
            DirectX::XMStoreFloat4x4(&inst.World, worldT);

            const float fx = (dim > 1) ? float(x) / float(dim - 1) : 0.5f;
            const float fz = (dim > 1) ? float(z) / float(dim - 1) : 0.5f;
            inst.Color = DirectX::XMFLOAT4(fx, 0.5f, fz, 1.0f);

            s_Instances.push_back(inst);
            s_InstanceBounds.push_back(bounds);
        }
        if ((int)s_Instances.size() >= s_targetInstanceCount)
            break;
    }

    MarkInstancesDirty();

#ifndef NDEBUG
    static bool s_loggedBoundsOnce = false;
    if (!s_loggedBoundsOnce)
    {
        s_loggedBoundsOnce = true;
        const float r = s_InstanceBounds.empty() ? 0.0f : s_InstanceBounds.front().Radius;
        Logger::Log(LogLevel::Debug,
            "[Culling] Bounds built | instances=" + std::to_string(s_Instances.size()) + " radius=" + std::to_string(r),
            "[Scene]");
    }
#endif
}

bool Scene::IsReady()
{
    return !g_scene.initFailed && g_scene.rootSig && g_scene.pso && g_scene.gridPso && g_scene.gridVb;
}

void Scene::InitializeResources(ID3D12Device* device)
{
    Graphics::GetInstance().AssertNotInRender("Scene::InitializeResources()");

    if (!device)
        return;

    EnsureSceneResources(device);
}

void Scene::Render(const SceneRenderContext& ctx)
{
#ifndef NDEBUG
    static auto s_sceneRenderLastLog = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    const auto now = std::chrono::steady_clock::now();
    if (now - s_sceneRenderLastLog >= std::chrono::seconds(1))
    {
        s_sceneRenderLastLog = now;
        Logger::Log(LogLevel::Debug,
            std::format("[ScenePass] Scene::Render ENTER | vp={}x{} frameIndex={} instances={} visibleIdx={} scratch={} instBuf={}",
                ctx.viewportWidth,
                ctx.viewportHeight,
                (uint64_t)ctx.frameIndex,
                (uint64_t)s_Instances.size(),
                (uint64_t)s_VisibleInstanceIndices.size(),
                (uint64_t)s_VisibleInstancesScratch.size(),
                s_InstanceBufferDefault ? 1 : 0),
            "[Scene]");
    }
#endif

    EnsureInstancesInitialized();
    EnsureInstanceBufferDefault(ctx.device, (UINT)s_Instances.size());

    // CP9-B: no per-frame CPU memcpy into a persistently mapped UPLOAD buffer.
    // Graphics owns the staged upload into the DEFAULT instance buffer.

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

    EnsureSceneObjectsInitialized();
    if (!g_loggedPhase45)
    {
        g_loggedPhase45 = true;
        Logger::Log(LogLevel::Info, "[Scene] Phase 4A.5 multi-instance rendering active");
    }

    const float t = static_cast<float>(ctx.frameIndex) * (1.0f / 60.0f);
    if (g_sceneObjects.size() >= 2)
    {
        g_sceneObjects[0].Rotation.y = t;
        g_sceneObjects[1].Rotation.x = t;
    }

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
    if (!Scene::g_FreezeCulling)
    {
        s_VisibleInstanceIndices.clear();
        s_VisibleInstanceIndices.reserve(s_InstanceBounds.size());

        for (UINT i = 0; i < (UINT)s_InstanceBounds.size(); ++i)
        {
            if (SphereInsideFrustum(s_InstanceBounds[i], fr))
                s_VisibleInstanceIndices.push_back(i);
        }
    }

    // CP13: build debug line list (CPU-side only)
    g_DebugLineVerts.clear();

    if (Scene::g_ShowFrustum)
    {
        DirectX::XMFLOAT3 corners[8]{};
        ExtractFrustumCornersWorld(viewProj, corners);

        constexpr uint32_t kFrustumColor = 0xFF00FFFF; // RGBA cyan
        for (const auto& e : kFrustumEdges)
            DebugAddLine(corners[e[0]], corners[e[1]], kFrustumColor);
    }

    if (Scene::g_ShowBounds)
    {
        // Visible mask for quick coloring (green = visible, red = culled)
        std::vector<uint8_t> visibleMask(s_InstanceBounds.size(), 0);
        for (UINT idx : s_VisibleInstanceIndices)
            if (idx < visibleMask.size())
                visibleMask[idx] = 1;

        const int segs = Scene::g_BoundsSegments;
        for (size_t i = 0; i < s_InstanceBounds.size(); ++i)
        {
            const Sphere& s = s_InstanceBounds[i];
            const uint32_t col = visibleMask[i] ? 0xFF00FF00u : 0xFFFF0000u; // RGBA
            DebugAddWireSphere(s.Center, s.Radius, col, segs);
        }
    }

#ifndef NDEBUG
    // CP12 Step 3: frustum orientation diagnostics (1Hz, logging only)
    {
        static auto s_lastOrientLog = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        const auto nowO = std::chrono::steady_clock::now();
        if (nowO - s_lastOrientLog >= std::chrono::seconds(1))
        {
            s_lastOrientLog = nowO;

            using namespace DirectX;

            const XMVECTOR camPos = XMLoadFloat3(&ctx.camera->position);

            // Approximate camera forward from the inverse view matrix (world space).
            const XMMATRIX viewM = XMLoadFloat4x4(&ctx.camera->view);
            const XMMATRIX invView = XMMatrixInverse(nullptr, viewM);
            const XMVECTOR forward = XMVector3Normalize(invView.r[2]);

            XMFLOAT3 camP{};
            XMFLOAT3 fwd{};
            XMStoreFloat3(&camP, camPos);
            XMStoreFloat3(&fwd, forward);

            // Pick a stable sample sphere (instance 0) if available.
            float d0[6] = {};
            bool hasSample = !s_InstanceBounds.empty();
            if (hasSample)
            {
                const Sphere& s = s_InstanceBounds[0];
                const XMVECTOR c = XMLoadFloat3(&s.Center);

                for (int i = 0; i < 6; i++)
                {
                    const XMFLOAT4& eq = fr.Planes[i].Eq;
                    const XMVECTOR n = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&eq));
                    d0[i] = XMVectorGetX(XMVector3Dot(c, n)) + eq.w;
                }
            }

            Logger::Log(LogLevel::Debug,
                std::format("[FrustumDiag] camPos=({:.2f},{:.2f},{:.2f}) fwd=({:.3f},{:.3f},{:.3f}) total={} visible={} sampleR={:.3f} dists(L,R,B,T,N,F)=({:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}) dbgLineVerts={}",
                    camP.x, camP.y, camP.z,
                    fwd.x, fwd.y, fwd.z,
                    (uint64_t)s_Instances.size(),
                    (uint64_t)s_VisibleInstanceIndices.size(),
                    hasSample ? s_InstanceBounds[0].Radius : 0.0f,
                    d0[0], d0[1], d0[2], d0[3], d0[4], d0[5],
                    (uint64_t)g_DebugLineVerts.size()),
                "[Scene]");
        }
    }
#endif

    // CP11-D: build contiguous visible instance buffer (CPU scratch)
    s_VisibleInstancesScratch.clear();
    s_VisibleInstancesScratch.reserve(s_VisibleInstanceIndices.size());
    for (UINT idx : s_VisibleInstanceIndices)
    {
        if (idx < s_Instances.size())
            s_VisibleInstancesScratch.push_back(s_Instances[idx]);
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

#ifndef NDEBUG
    static bool s_loggedInstanceSrvOnce = false;
    if (!s_loggedInstanceSrvOnce)
    {
        s_loggedInstanceSrvOnce = true;
        Logger::Log(LogLevel::Debug,
            "[Instancing] Instance SRV handles | CPU=0x" + std::to_string((uint64_t)s_InstanceSRVCpu.ptr) +
            " GPU=0x" + std::to_string((uint64_t)s_InstanceSRVGpu.ptr),
            "[Scene]");
    }
#endif

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

    // Publish Scene root signature to Graphics for Engine Debug Layer consumers.
    Graphics::GetInstance().SetExternalSceneRootSignature(g_scene.rootSig.Get());

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
        ctx.commandList->SetGraphicsRootConstantBufferView(0, alloc.GpuAddress);
    }

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
        if (instanceCount > 0)
            ctx.commandList->DrawIndexedInstanced(cube.IndexCount, instanceCount, 0, 0, 0);
    }

    // For subsequent passes (grid), re-bind a fresh slice.
    {
        Graphics& gfx = Graphics::GetInstance();
        const auto alloc = gfx.AllocateFrameCB(sizeof(SceneCB));
        std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));
        ctx.commandList->SetGraphicsRootConstantBufferView(0, alloc.GpuAddress);
    }

    // ====================================================================
    // Phase 4A: GRID OVERLAY PASS (Lines)
    // ====================================================================
    // Draws alpha-blended grid lines over opaque geometry.
    // PSO: alpha blend enabled, no culling (lines are double-sided)
    // Shares same root signature + CB as opaque pass.
    // ====================================================================
    ctx.commandList->SetPipelineState(g_scene.gridPso.Get());
    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    ctx.commandList->IASetVertexBuffers(0, 1, &g_scene.gridVbv);
    ctx.commandList->DrawInstanced(g_scene.gridVbv.SizeInBytes / g_scene.gridVbv.StrideInBytes, 1, 0, 0);

    // ====================================================================
    // CP13: DEBUG LINE OVERLAY PASS (Frustum + Bounds)
    // ====================================================================
    if (!g_DebugLineVerts.empty())
    {
        Graphics& gfx = Graphics::GetInstance();
        gfx.EnsureDebugLineResources();

        ID3D12PipelineState* pso = gfx.GetDebugLinePSO();
        ID3D12Resource* vb = gfx.GetDebugLineVB();
        void* mapped = gfx.GetDebugLineVBMapped();
        const UINT maxVerts = gfx.GetDebugLineVBMaxVerts();

        if (pso && vb && mapped && maxVerts > 0)
        {
            const UINT vertCount = (UINT)(std::min<size_t>(g_DebugLineVerts.size(), (size_t)maxVerts));
            std::memcpy(mapped, g_DebugLineVerts.data(), (size_t)vertCount * sizeof(DebugLineVertex));

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = vb->GetGPUVirtualAddress();
            vbv.SizeInBytes = vertCount * (UINT)sizeof(DebugLineVertex);
            vbv.StrideInBytes = (UINT)sizeof(DebugLineVertex);

            // Debug line PSO uses the same root signature as the Scene pass.
            ctx.commandList->SetPipelineState(pso);
            ctx.commandList->SetGraphicsRootSignature(gfx.GetSceneRootSignature());

            // Re-bind the existing per-frame scene CB (b0) for ViewProj.
            {
                Graphics& gfx2 = Graphics::GetInstance();
                const auto alloc = gfx2.AllocateFrameCB(sizeof(SceneCB));
                std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));
                ctx.commandList->SetGraphicsRootConstantBufferView(0, alloc.GpuAddress);
            }

            ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            ctx.commandList->IASetVertexBuffers(0, 1, &vbv);
            ctx.commandList->DrawInstanced(vertCount, 1, 0, 0);
        }

        g_DebugLineVerts.clear();
    }

    (void)ctx.frameIndex;
}
