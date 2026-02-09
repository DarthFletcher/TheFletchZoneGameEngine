#include "Scene.h"

#include "Logger.h"
#include "ShaderUtils.h"

#include "CameraData.h"
#include "Graphics.h"
#include "Mesh.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

extern ID3D12DescriptorHeap* g_SRVHeap;

namespace
{
    using Microsoft::WRL::ComPtr;

    struct SceneCB
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
        // Phase 4A: Scene is a consumer only. It owns ONLY its per-frame constant buffer.
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

    static void EnsureSceneResources(ID3D12Device* device)
    {
        if (!device)
            return;

        if (g_scene.initFailed)
            return;

        // If core resources exist and (if built) grid resources exist, we are done.
        if (g_scene.cb)
        {
            // Grid resources are optional; build them lazily if missing.
        }

        if (!g_scene.initLogged)
        {
            Logger::Log(LogLevel::Info, "Phase 4A: creating scene per-frame constant buffer", "[Scene]");
            g_scene.initLogged = true;
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

            // Initialized; real per-frame values are written in `Scene::Render()` from `ctx.camera`.
            g_scene.cbData = {};
            FillIdentity(g_scene.cbData.viewProj);
            g_scene.cbData.cameraPos[0] = 0.0f;
            g_scene.cbData.cameraPos[1] = 0.0f;
            g_scene.cbData.cameraPos[2] = 0.0f;
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
    }
}

void Scene::Render(const SceneRenderContext& ctx)
{
    // ====================================================================
    // Phase 3F: PASS BOUNDARY & OWNERSHIP NOTES
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

    // Clamp to guard against accidental invalid sizes.
    constexpr uint32_t kMin = 1u;
    constexpr uint32_t kMax = 16384u;

    const uint32_t w = (std::min)(kMax, (std::max)(kMin, ctx.viewportWidth));
    const uint32_t h = (std::min)(kMax, (std::max)(kMin, ctx.viewportHeight));

    EnsureSceneResources(ctx.device);
    if (g_scene.initFailed || !g_scene.cb)
        return;

    // Phase 3C: build CB from engine-owned camera (LH) and transpose to match HLSL mul(float4, matrix).
    {
        using namespace DirectX;

        const XMMATRIX view = XMLoadFloat4x4(&ctx.camera->view);
        const XMMATRIX proj = XMLoadFloat4x4(&ctx.camera->proj);
        const XMMATRIX vp = XMMatrixMultiply(view, proj);
        const XMMATRIX vpT = XMMatrixTranspose(vp);

        XMFLOAT4X4 vpStore{};
        XMStoreFloat4x4(&vpStore, vpT);
        memcpy(g_scene.cbData.viewProj, &vpStore, sizeof(float) * 16);

        g_scene.cbData.cameraPos[0] = ctx.camera->position.x;
        g_scene.cbData.cameraPos[1] = ctx.camera->position.y;
        g_scene.cbData.cameraPos[2] = ctx.camera->position.z;

        // Keep as a constant for now (Phase 3C).
        g_scene.cbData.gridFadeDist = 10.0f;
    }

    // Exactly one CB upload per Scene::Render.
    if (g_scene.cbCpu)
        memcpy(g_scene.cbCpu, &g_scene.cbData, sizeof(SceneCB));

    // ====================================================================
    // Phase 3F: EXPLICIT STATE SETUP
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

    // Fully define dynamic state that can bleed between draws.
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx.commandList->OMSetBlendFactor(blendFactor);
    ctx.commandList->OMSetStencilRef(0);

    // CBV at root parameter 0 (matches existing scene root signature contract).
    ctx.commandList->SetGraphicsRootConstantBufferView(0, g_scene.cb->GetGPUVirtualAddress());

    const MeshData& cube = Graphics::GetInstance().GetCubeMesh();
    if (!cube.vertexBuffer || !cube.indexBuffer || cube.indexCount == 0)
        return;

    ctx.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.commandList->IASetVertexBuffers(0, 1, &cube.vbv);
    ctx.commandList->IASetIndexBuffer(&cube.ibv);
    ctx.commandList->DrawIndexedInstanced(cube.indexCount, 1, 0, 0, 0);

    (void)ctx.frameIndex;
}
