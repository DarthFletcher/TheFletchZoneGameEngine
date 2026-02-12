#include "Mesh.h"

#include "Logger.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    // Phase 4A: keep vertex format aligned with the existing scene pipeline (POSITION + COLOR).
    // More attributes (normal/uv) come later once the input layout/shaders are updated.
    struct VertexPC
    {
        float px, py, pz;
        float r, g, b, a;
    };

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
}

bool CreateCubeMesh(ID3D12Device* device, MeshData& outMesh)
{
    if (!device)
        return false;

    outMesh = {};

    // Unit cube centered at origin: -0.5..+0.5.
    // 24 vertices: 4 per face (unique vertices per face for future normal/UV expansion).
    const std::array<VertexPC, 24> vertices =
    {{
        // +X (red)
        { +0.5f, -0.5f, -0.5f, 1, 0, 0, 1 },
        { +0.5f, +0.5f, -0.5f, 1, 0, 0, 1 },
        { +0.5f, +0.5f, +0.5f, 1, 0, 0, 1 },
        { +0.5f, -0.5f, +0.5f, 1, 0, 0, 1 },

        // -X (green)
        { -0.5f, -0.5f, +0.5f, 0, 1, 0, 1 },
        { -0.5f, +0.5f, +0.5f, 0, 1, 0, 1 },
        { -0.5f, +0.5f, -0.5f, 0, 1, 0, 1 },
        { -0.5f, -0.5f, -0.5f, 0, 1, 0, 1 },

        // +Y (blue)
        { -0.5f, +0.5f, -0.5f, 0, 0, 1, 1 },
        { -0.5f, +0.5f, +0.5f, 0, 0, 1, 1 },
        { +0.5f, +0.5f, +0.5f, 0, 0, 1, 1 },
        { +0.5f, +0.5f, -0.5f, 0, 0, 1, 1 },

        // -Y (yellow)
        { -0.5f, -0.5f, +0.5f, 1, 1, 0, 1 },
        { -0.5f, -0.5f, -0.5f, 1, 1, 0, 1 },
        { +0.5f, -0.5f, -0.5f, 1, 1, 0, 1 },
        { +0.5f, -0.5f, +0.5f, 1, 1, 0, 1 },

        // +Z (magenta)
        { +0.5f, -0.5f, +0.5f, 1, 0, 1, 1 },
        { +0.5f, +0.5f, +0.5f, 1, 0, 1, 1 },
        { -0.5f, +0.5f, +0.5f, 1, 0, 1, 1 },
        { -0.5f, -0.5f, +0.5f, 1, 0, 1, 1 },

        // -Z (cyan)
        { -0.5f, -0.5f, -0.5f, 0, 1, 1, 1 },
        { -0.5f, +0.5f, -0.5f, 0, 1, 1, 1 },
        { +0.5f, +0.5f, -0.5f, 0, 1, 1, 1 },
        { +0.5f, -0.5f, -0.5f, 0, 1, 1, 1 },
    }};

    // 36 indices: 6 faces * 2 triangles * 3.
    const std::array<uint16_t, 36> indices =
    {{
        0, 1, 2,  0, 2, 3,        // +X
        4, 5, 6,  4, 6, 7,        // -X
        8, 9,10,  8,10,11,        // +Y
       12,13,14, 12,14,15,        // -Y
       16,17,18, 16,18,19,        // +Z
       20,21,22, 20,22,23,        // -Z
    }};

    const UINT vbSize = (UINT)(vertices.size() * sizeof(VertexPC));
    const UINT ibSize = (UINT)(indices.size() * sizeof(uint16_t));

    const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    // Vertex buffer (UPLOAD heap)
    {
        const D3D12_RESOURCE_DESC desc = MakeBufferDesc(vbSize);

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(outMesh.VertexBuffer.ReleaseAndGetAddressOf()));

        if (FAILED(hr) || !outMesh.VertexBuffer)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMesh: failed to create vertex buffer (UPLOAD).", "[Mesh]");
            outMesh = {};
            return false;
        }

        void* mapped = nullptr;
        const HRESULT hrMap = outMesh.VertexBuffer->Map(0, nullptr, &mapped);
        if (FAILED(hrMap) || !mapped)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMesh: failed to map vertex buffer.", "[Mesh]");
            outMesh = {};
            return false;
        }

        std::memcpy(mapped, vertices.data(), vbSize);
        outMesh.VertexBuffer->Unmap(0, nullptr);

        outMesh.VBV.BufferLocation = outMesh.VertexBuffer->GetGPUVirtualAddress();
        outMesh.VBV.SizeInBytes = vbSize;
        outMesh.VBV.StrideInBytes = sizeof(VertexPC);
    }

    // Index buffer (UPLOAD heap)
    {
        const D3D12_RESOURCE_DESC desc = MakeBufferDesc(ibSize);

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(outMesh.IndexBuffer.ReleaseAndGetAddressOf()));

        if (FAILED(hr) || !outMesh.IndexBuffer)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMesh: failed to create index buffer (UPLOAD).", "[Mesh]");
            outMesh = {};
            return false;
        }

        void* mapped = nullptr;
        const HRESULT hrMap = outMesh.IndexBuffer->Map(0, nullptr, &mapped);
        if (FAILED(hrMap) || !mapped)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMesh: failed to map index buffer.", "[Mesh]");
            outMesh = {};
            return false;
        }

        std::memcpy(mapped, indices.data(), ibSize);
        outMesh.IndexBuffer->Unmap(0, nullptr);

        outMesh.IBV.BufferLocation = outMesh.IndexBuffer->GetGPUVirtualAddress();
        outMesh.IBV.SizeInBytes = ibSize;
        outMesh.IBV.Format = DXGI_FORMAT_R16_UINT;
    }

    outMesh.IndexCount = (uint32_t)indices.size();

    static bool s_loggedOnce = false;
    if (!s_loggedOnce)
    {
        s_loggedOnce = true;
        Logger::Log(LogLevel::Info, "CreateCubeMesh: cube mesh created (UPLOAD heap only).", "[Mesh]");
    }

    return true;
}
