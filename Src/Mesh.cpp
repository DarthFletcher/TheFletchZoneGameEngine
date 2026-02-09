#include "Mesh.h"

#include "Logger.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <format>

namespace
{
    struct VertexPCN
    {
        float px, py, pz;
        float nx, ny, nz;
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

void CreateCubeMesh(ID3D12Device* device, MeshData& outMesh)
{
    outMesh = {};

    if (!device)
        return;

    constexpr float s = 0.5f;

    constexpr float kR = 0.85f;
    constexpr float kG = 0.85f;
    constexpr float kB = 0.90f;
    constexpr float kA = 1.0f;

    const std::array<VertexPCN, 24> vertices = {
        // +Z (front)
        VertexPCN{ -s, -s, +s,  0,  0, +1,  kR, kG, kB, kA },
        VertexPCN{ -s, +s, +s,  0,  0, +1,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, +s,  0,  0, +1,  kR, kG, kB, kA },
        VertexPCN{ +s, -s, +s,  0,  0, +1,  kR, kG, kB, kA },

        // -Z (back)
        VertexPCN{ +s, -s, -s,  0,  0, -1,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, -s,  0,  0, -1,  kR, kG, kB, kA },
        VertexPCN{ -s, +s, -s,  0,  0, -1,  kR, kG, kB, kA },
        VertexPCN{ -s, -s, -s,  0,  0, -1,  kR, kG, kB, kA },

        // +X (right)
        VertexPCN{ +s, -s, +s, +1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, +s, +1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, -s, +1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, -s, -s, +1,  0,  0,  kR, kG, kB, kA },

        // -X (left)
        VertexPCN{ -s, -s, -s, -1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ -s, +s, -s, -1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ -s, +s, +s, -1,  0,  0,  kR, kG, kB, kA },
        VertexPCN{ -s, -s, +s, -1,  0,  0,  kR, kG, kB, kA },

        // +Y (top)
        VertexPCN{ -s, +s, +s,  0, +1,  0,  kR, kG, kB, kA },
        VertexPCN{ -s, +s, -s,  0, +1,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, -s,  0, +1,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, +s, +s,  0, +1,  0,  kR, kG, kB, kA },

        // -Y (bottom)
        VertexPCN{ -s, -s, -s,  0, -1,  0,  kR, kG, kB, kA },
        VertexPCN{ -s, -s, +s,  0, -1,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, -s, +s,  0, -1,  0,  kR, kG, kB, kA },
        VertexPCN{ +s, -s, -s,  0, -1,  0,  kR, kG, kB, kA },
    };

    const std::array<uint16_t, 36> indices = {
        0, 1, 2,  0, 2, 3,        // +Z
        4, 5, 6,  4, 6, 7,        // -Z
        8, 9,10,  8,10,11,        // +X
        12,13,14, 12,14,15,       // -X
        16,17,18, 16,18,19,       // +Y
        20,21,22, 20,22,23,       // -Y
    };

    const UINT vbSize = (UINT)(vertices.size() * sizeof(VertexPCN));
    const UINT ibSize = (UINT)(indices.size() * sizeof(uint16_t));

    const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    {
        const D3D12_RESOURCE_DESC desc = MakeBufferDesc(vbSize);
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&outMesh.vertexBuffer));

        if (FAILED(hr) || !outMesh.vertexBuffer)
            return;

        void* cpu = nullptr;
        if (SUCCEEDED(outMesh.vertexBuffer->Map(0, nullptr, &cpu)) && cpu)
        {
            std::memcpy(cpu, vertices.data(), vbSize);
            outMesh.vertexBuffer->Unmap(0, nullptr);
        }
        else
        {
            outMesh.vertexBuffer.Reset();
            return;
        }

        outMesh.vbv.BufferLocation = outMesh.vertexBuffer->GetGPUVirtualAddress();
        outMesh.vbv.StrideInBytes = sizeof(VertexPCN);
        outMesh.vbv.SizeInBytes = vbSize;
    }

    {
        const D3D12_RESOURCE_DESC desc = MakeBufferDesc(ibSize);
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&outMesh.indexBuffer));

        if (FAILED(hr) || !outMesh.indexBuffer)
        {
            outMesh.vertexBuffer.Reset();
            return;
        }

        void* cpu = nullptr;
        if (SUCCEEDED(outMesh.indexBuffer->Map(0, nullptr, &cpu)) && cpu)
        {
            std::memcpy(cpu, indices.data(), ibSize);
            outMesh.indexBuffer->Unmap(0, nullptr);
        }
        else
        {
            outMesh.indexBuffer.Reset();
            outMesh.vertexBuffer.Reset();
            return;
        }

        outMesh.ibv.BufferLocation = outMesh.indexBuffer->GetGPUVirtualAddress();
        outMesh.ibv.SizeInBytes = ibSize;
        outMesh.ibv.Format = DXGI_FORMAT_R16_UINT;
    }

    outMesh.indexCount = (UINT)indices.size();

    Logger::Log(LogLevel::Info, std::format("Phase 4A: cube mesh created (vtx={}, idx={})", vertices.size(), indices.size()), "[Mesh]");
}
