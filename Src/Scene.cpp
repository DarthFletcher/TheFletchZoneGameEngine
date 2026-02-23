#include "Scene.h"

#include "Logger.h"
#include "ShaderUtils.h"
#include "CameraData.h"
#include "Graphics.h"
#include "Vertex.h"

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
        if (!g_sceneObjects.empty())
            return;

        // Place cubes on top of the grid (grid at y=0, cube assumed ~1 unit tall).
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
        // Root Signature (b0 only)
        // ---------------------------
        if (!g_scene.rootSig)
        {
            D3D12_ROOT_PARAMETER param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            param.Descriptor.ShaderRegister = 0;
            param.Descriptor.RegisterSpace = 0;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 1;
            rsDesc.pParameters = &param;
            rsDesc.NumStaticSamplers = 0;
            rsDesc.pStaticSamplers = nullptr;
            rsDesc.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

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

void Scene::Render(const SceneRenderContext& ctx)
{
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

    EnsureSceneResources(ctx.device);
    if (g_scene.initFailed || !g_scene.rootSig || !g_scene.pso || !g_scene.cb || !g_scene.gridPso || !g_scene.gridVb)
        return;

    // Phase 4A.5: compute view-projection (NOT transposed).
    DirectX::XMMATRIX viewProj;
    {
        using namespace DirectX;
        const XMMATRIX view = XMLoadFloat4x4(&ctx.camera->view);
        const XMMATRIX proj = XMLoadFloat4x4(&ctx.camera->proj);
        viewProj = XMMatrixMultiply(view, proj);

        g_scene.cbData.cameraPos[0] = ctx.camera->position.x;
        g_scene.cbData.cameraPos[1] = ctx.camera->position.y;
        g_scene.cbData.cameraPos[2] = ctx.camera->position.z;
        g_scene.cbData.gridFadeDist = 10.0f;
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

    // Fully define dynamic state that can bleed between draws.
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx.commandList->OMSetBlendFactor(blendFactor);
    ctx.commandList->OMSetStencilRef(0);

    // Common CB: bind a fresh slice (shared root signature for both PSOs).
    {
        Graphics& gfx = Graphics::GetInstance();
        const auto alloc = gfx.AllocateFrameCB(sizeof(SceneCB));
        std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));
        ctx.commandList->SetGraphicsRootConstantBufferView(0, alloc.GpuAddress);
    }

    // ====================================================================
    // Phase 4A: OPAQUE GEOMETRY PASS (Engine-owned cube)
    // ====================================================================
    // (Phase 4A.5 replaces this with multi-instance rendering)

    // ====================================================================
    // Phase 4A.5: OPAQUE GEOMETRY PASS (Engine-owned cube, multi-instance)
    // ====================================================================
    ctx.commandList->SetPipelineState(g_scene.pso.Get());
    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const MeshData& cube = Graphics::GetInstance().GetCubeMesh();
    if (cube.IndexCount > 0)
    {
        ctx.commandList->IASetVertexBuffers(0, 1, &cube.VBV);
        ctx.commandList->IASetIndexBuffer(&cube.IBV);

        using namespace DirectX;
        const UINT objectCount = (UINT)(std::min<size_t>(g_sceneObjects.size(), kMaxSceneObjects));

        Graphics& gfx = Graphics::GetInstance();

        for (UINT i = 0; i < objectCount; ++i)
        {
            const SceneObject& obj = g_sceneObjects[i];

            const XMMATRIX S = XMMatrixScaling(obj.Scale.x, obj.Scale.y, obj.Scale.z);
            const XMMATRIX R = XMMatrixRotationRollPitchYaw(obj.Rotation.x, obj.Rotation.y, obj.Rotation.z);
            const XMMATRIX T = XMMatrixTranslation(obj.Position.x, obj.Position.y, obj.Position.z);
            const XMMATRIX world = XMMatrixMultiply(XMMatrixMultiply(S, R), T);

            const XMMATRIX wvp = XMMatrixMultiply(world, viewProj);
            const XMMATRIX wvpT = XMMatrixTranspose(wvp);

            XMFLOAT4X4 wvpStore{};
            XMStoreFloat4x4(&wvpStore, wvpT);
            memcpy(g_scene.cbData.viewProj, &wvpStore, sizeof(float) * 16);

            g_scene.cbData.cameraPos[0] = ctx.camera->position.x;
            g_scene.cbData.cameraPos[1] = ctx.camera->position.y;
            g_scene.cbData.cameraPos[2] = ctx.camera->position.z;
            g_scene.cbData.gridFadeDist = 10.0f;

            const auto alloc = gfx.AllocateFrameCB(sizeof(SceneCB));
            std::memcpy(alloc.CpuPtr, &g_scene.cbData, sizeof(SceneCB));
            ctx.commandList->SetGraphicsRootConstantBufferView(0, alloc.GpuAddress);

            ctx.commandList->DrawIndexedInstanced(cube.IndexCount, 1, 0, 0, 0);
        }
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

    (void)ctx.frameIndex;
}
