#include "Scene.h"

#include "Logger.h"
#include "ShaderUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

extern ID3D12DescriptorHeap* g_SRVHeap;

namespace
{
    using Microsoft::WRL::ComPtr;

    struct SceneVertex
    {
        float pos[3];
        float color[4];
    };

    static_assert(offsetof(SceneVertex, pos) == 0, "SceneVertex::pos offset mismatch");
    static_assert(offsetof(SceneVertex, color) == sizeof(float) * 3, "SceneVertex::color offset mismatch");
    static_assert(sizeof(SceneVertex) == sizeof(float) * 7, "SceneVertex size mismatch");

    // Must exactly match `cbuffer SceneCB : register(b0)` layout in the HLSL.
    // HLSL packing rules (cbuffer):
    // - float4x4 = 64 bytes
    // - float3 consumes 12 bytes but occupies 16-byte register slot
    // - float can share remaining 4 bytes in that slot
    struct alignas(256) SceneCB
    {
        float viewProj[16];      // 64 bytes
        float cameraPos[3];      // 12 bytes
        float gridFadeDist = 0;  // 4 bytes (shares the 16-byte slot with cameraPos)

        // Fill to 256 bytes (D3D12 CBV alignment requirement).
        float pad[44]{};         // 44 * 4 = 176 bytes
    };

    static_assert(offsetof(SceneCB, viewProj) == 0, "SceneCB::viewProj offset mismatch");
    static_assert(offsetof(SceneCB, cameraPos) == 64, "SceneCB::cameraPos offset mismatch");
    static_assert(offsetof(SceneCB, gridFadeDist) == 76, "SceneCB::gridFadeDist offset mismatch");
    static_assert(sizeof(SceneCB) == 256, "SceneCB must be exactly 256 bytes");

    struct SceneDrawResources
    {
        ComPtr<ID3D12RootSignature> rootSig;

        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3D12Resource> vb;
        D3D12_VERTEX_BUFFER_VIEW vbv{};

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

    static void EnsureSceneResources(ID3D12Device* device)
    {
        if (!device)
            return;

        if (g_scene.initFailed)
            return;

        // If core resources exist and (if built) grid resources exist, we are done.
        if (g_scene.rootSig && g_scene.pso && g_scene.vb && g_scene.cb)
        {
            // Grid resources are optional; build them lazily if missing.
        }

        if (!g_scene.initLogged)
        {
            Logger::Log(LogLevel::Info, "Phase 3B: creating minimal scene draw resources (triangle)", "[Scene]");
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
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 1;
            rsDesc.pParameters = &param;
            rsDesc.NumStaticSamplers = 0;
            rsDesc.pStaticSamplers = nullptr;
            rsDesc.Flags =
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

            ComPtr<ID3DBlob> sigBlob;
            ComPtr<ID3DBlob> errBlob;
            const HRESULT hrSer = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
            if (FAILED(hrSer) || !sigBlob)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: D3D12SerializeRootSignature failed", "[Scene]");
                return;
            }

            const HRESULT hrRS = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_scene.rootSig));
            if (FAILED(hrRS) || !g_scene.rootSig)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: CreateRootSignature failed", "[Scene]");
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
            Logger::Log(LogLevel::Error, std::string("Phase 3B: shader compile failed: ") + e.what(), "[Scene]");
            return;
        }

        // ---------------------------
        // PSO (opaque, no depth)
        // ---------------------------
        if (!g_scene.pso)
        {
            D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(SceneVertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(SceneVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
            rast.MultisampleEnable = FALSE;
            rast.AntialiasedLineEnable = FALSE;
            rast.ForcedSampleCount = 0;
            rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            D3D12_DEPTH_STENCIL_DESC ds{};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ds.StencilEnable = FALSE;
            ds.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ds.BackFace = ds.FrontFace;

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
            psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

            const HRESULT hrPSO = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_scene.pso));
            if (FAILED(hrPSO) || !g_scene.pso)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: CreateGraphicsPipelineState failed", "[Scene]");
                return;
            }
        }

        // ---------------------------
        // Vertex Buffer (upload heap, static geometry)
        // ---------------------------
        if (!g_scene.vb)
        {
            const std::array<SceneVertex, 3> verts = {
                SceneVertex{ { 0.0f,  0.5f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
                SceneVertex{ { 0.5f, -0.5f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
                SceneVertex{ { -0.5f,-0.5f, 0.0f }, { 0.2f, 0.4f, 1.0f, 1.0f } },
            };

            const UINT vbSize = (UINT)(sizeof(SceneVertex) * verts.size());

            const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC bufDesc = MakeBufferDesc(vbSize);

            const HRESULT hrVB = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&g_scene.vb));

            if (FAILED(hrVB) || !g_scene.vb)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: CreateCommittedResource(VB) failed", "[Scene]");
                return;
            }

            void* mapped = nullptr;
            const HRESULT hrMap = g_scene.vb->Map(0, nullptr, &mapped);
            if (FAILED(hrMap) || !mapped)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: VB Map failed", "[Scene]");
                return;
            }
            memcpy(mapped, verts.data(), vbSize);
            g_scene.vb->Unmap(0, nullptr);

            g_scene.vbv.BufferLocation = g_scene.vb->GetGPUVirtualAddress();
            g_scene.vbv.SizeInBytes = vbSize;
            g_scene.vbv.StrideInBytes = sizeof(SceneVertex);
        }

        // ---------------------------
        // Constant Buffer (upload heap, persistently mapped)
        // ---------------------------
        if (!g_scene.cb)
        {
            constexpr UINT cbSize = 256;
            const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
            const D3D12_RESOURCE_DESC bufDesc = MakeBufferDesc(cbSize);

            const HRESULT hrCB = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&g_scene.cb));

            if (FAILED(hrCB) || !g_scene.cb)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: CreateCommittedResource(CB) failed", "[Scene]");
                return;
            }

            g_scene.cbData = {};
            FillIdentity(g_scene.cbData.viewProj);
            g_scene.cbData.cameraPos[0] = 0.0f;
            g_scene.cbData.cameraPos[1] = 0.0f;
            g_scene.cbData.cameraPos[2] = -2.0f;
            g_scene.cbData.gridFadeDist = 10.0f;

            const HRESULT hrMap = g_scene.cb->Map(0, nullptr, &g_scene.cbCpu);
            if (FAILED(hrMap) || !g_scene.cbCpu)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: CB Map failed", "[Scene]");
                return;
            }

            memcpy(g_scene.cbCpu, &g_scene.cbData, sizeof(SceneCB));
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
                Logger::Log(LogLevel::Error, std::string("Phase 3B: grid shader compile failed: ") + e.what(), "[Scene]");
                return;
            }

            D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(SceneVertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, (UINT)offsetof(SceneVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };

            // Alpha blend (for distance fade)
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
            rast.CullMode = D3D12_CULL_MODE_NONE; // lines: no cull
            rast.FrontCounterClockwise = FALSE;
            rast.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            rast.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            rast.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            rast.DepthClipEnable = TRUE;
            rast.MultisampleEnable = FALSE;
            rast.AntialiasedLineEnable = FALSE;
            rast.ForcedSampleCount = 0;
            rast.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            D3D12_DEPTH_STENCIL_DESC ds{};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ds.StencilEnable = FALSE;
            ds.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            ds.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            ds.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            ds.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            ds.BackFace = ds.FrontFace;

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
                Logger::Log(LogLevel::Error, "Phase 3B: CreateGraphicsPipelineState(grid) failed", "[Scene]");
                return;
            }
        }

        if (!g_scene.gridVb)
        {
            // Build a simple XZ plane line grid centered at origin.
            // N lines each direction, covering [-extent, +extent].
            constexpr int kDiv = 20;
            constexpr float kExtent = 10.0f;
            constexpr float kHalf = kExtent;
            constexpr float kStep = (kExtent * 2.0f) / (float)kDiv;

            std::vector<SceneVertex> verts;
            verts.reserve((kDiv + 1) * 4);

            auto push = [&](float x, float y, float z, float r, float g, float b, float a)
            {
                SceneVertex v{};
                v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
                v.color[0] = r; v.color[1] = g; v.color[2] = b; v.color[3] = a;
                verts.push_back(v);
            };

            for (int i = 0; i <= kDiv; ++i)
            {
                const float t = -kHalf + (float)i * kStep;

                // Emphasize axis lines.
                const bool isAxis = (i == kDiv / 2);
                const float c = isAxis ? 0.85f : 0.40f;
                const float a = isAxis ? 0.95f : 0.65f;

                // Line parallel to X at Z=t
                push(-kHalf, 0.0f, t, c, c, c, a);
                push(+kHalf, 0.0f, t, c, c, c, a);

                // Line parallel to Z at X=t
                push(t, 0.0f, -kHalf, c, c, c, a);
                push(t, 0.0f, +kHalf, c, c, c, a);
            }

            const UINT vbSize = (UINT)(sizeof(SceneVertex) * verts.size());

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
                Logger::Log(LogLevel::Error, "Phase 3B: CreateCommittedResource(GridVB) failed", "[Scene]");
                return;
            }

            void* mapped = nullptr;
            const HRESULT hrMap = g_scene.gridVb->Map(0, nullptr, &mapped);
            if (FAILED(hrMap) || !mapped)
            {
                g_scene.initFailed = true;
                Logger::Log(LogLevel::Error, "Phase 3B: GridVB Map failed", "[Scene]");
                return;
            }
            memcpy(mapped, verts.data(), vbSize);
            g_scene.gridVb->Unmap(0, nullptr);

            g_scene.gridVbv.BufferLocation = g_scene.gridVb->GetGPUVirtualAddress();
            g_scene.gridVbv.SizeInBytes = vbSize;
            g_scene.gridVbv.StrideInBytes = sizeof(SceneVertex);
        }
    }
}

void Scene::Render(const SceneRenderContext& ctx)
{
    if (!ctx.device || !ctx.commandList)
    {
        if (!g_loggedInvalidCtx)
        {
            Logger::Log(LogLevel::Error, "Scene::Render called with null device/commandList", "[Scene]");
            g_loggedInvalidCtx = true;
        }
        return;
    }

    // Clamp to guard against accidental invalid sizes.
    constexpr uint32_t kMin = 1u;
    constexpr uint32_t kMax = 16384u;

    const uint32_t w = (std::min)(kMax, (std::max)(kMin, ctx.viewportWidth));
    const uint32_t h = (std::min)(kMax, (std::max)(kMin, ctx.viewportHeight));

    EnsureSceneResources(ctx.device);
    if (g_scene.initFailed || !g_scene.rootSig || !g_scene.pso || !g_scene.vb || !g_scene.cb || !g_scene.gridPso || !g_scene.gridVb)
        return;

    // Frame-safe deterministic CB write (single constant for now).
    if (g_scene.cbCpu)
        memcpy(g_scene.cbCpu, &g_scene.cbData, sizeof(SceneCB));

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

    // Common CB
    ctx.commandList->SetGraphicsRootConstantBufferView(0, g_scene.cb->GetGPUVirtualAddress());

    // ---------------------------
    // Solid pass (triangle)
    // ---------------------------
    ctx.commandList->SetPipelineState(g_scene.pso.Get());
    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.commandList->IASetVertexBuffers(0, 1, &g_scene.vbv);
    ctx.commandList->DrawInstanced(3, 1, 0, 0);

    // ---------------------------
    // Grid pass (lines)
    // ---------------------------
    ctx.commandList->SetPipelineState(g_scene.gridPso.Get());
    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    ctx.commandList->IASetVertexBuffers(0, 1, &g_scene.gridVbv);
    ctx.commandList->DrawInstanced(g_scene.gridVbv.SizeInBytes / g_scene.gridVbv.StrideInBytes, 1, 0, 0);

    (void)ctx.frameIndex;
}
