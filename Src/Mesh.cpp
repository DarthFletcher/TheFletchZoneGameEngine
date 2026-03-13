#include "Mesh.h"

#include "Logger.h"
#include "Vertex.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
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

    static void FillCubeData(std::array<VertexPC, 24>& vertices, std::array<uint16_t, 36>& indices)
    {
        vertices =
        {{
            // +X (red)
            { { +0.5f, -0.5f, -0.5f }, { 1, 0, 0, 1 }, { 0, 1 } },
            { { +0.5f, +0.5f, -0.5f }, { 1, 0, 0, 1 }, { 0, 0 } },
            { { +0.5f, +0.5f, +0.5f }, { 1, 0, 0, 1 }, { 1, 0 } },
            { { +0.5f, -0.5f, +0.5f }, { 1, 0, 0, 1 }, { 1, 1 } },

            // -X (green)
            { { -0.5f, -0.5f, +0.5f }, { 0, 1, 0, 1 }, { 0, 1 } },
            { { -0.5f, +0.5f, +0.5f }, { 0, 1, 0, 1 }, { 0, 0 } },
            { { -0.5f, +0.5f, -0.5f }, { 0, 1, 0, 1 }, { 1, 0 } },
            { { -0.5f, -0.5f, -0.5f }, { 0, 1, 0, 1 }, { 1, 1 } },

            // +Y (blue)
            { { -0.5f, +0.5f, -0.5f }, { 0, 0, 1, 1 }, { 0, 1 } },
            { { -0.5f, +0.5f, +0.5f }, { 0, 0, 1, 1 }, { 0, 0 } },
            { { +0.5f, +0.5f, +0.5f }, { 0, 0, 1, 1 }, { 1, 0 } },
            { { +0.5f, +0.5f, -0.5f }, { 0, 0, 1, 1 }, { 1, 1 } },

            // -Y (yellow)
            { { -0.5f, -0.5f, +0.5f }, { 1, 1, 0, 1 }, { 0, 1 } },
            { { -0.5f, -0.5f, -0.5f }, { 1, 1, 0, 1 }, { 0, 0 } },
            { { +0.5f, -0.5f, -0.5f }, { 1, 1, 0, 1 }, { 1, 0 } },
            { { +0.5f, -0.5f, +0.5f }, { 1, 1, 0, 1 }, { 1, 1 } },

            // +Z (magenta)
            { { +0.5f, -0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 1 } },
            { { +0.5f, +0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 0 } },
            { { -0.5f, +0.5f, +0.5f }, { 1, 0, 1, 1 }, { 1, 0 } },
            { { -0.5f, -0.5f, +0.5f }, { 1, 0, 1, 1 }, { 1, 1 } },

            // -Z (cyan)
            { { -0.5f, -0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 1 } },
            { { -0.5f, +0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 0 } },
            { { +0.5f, +0.5f, -0.5f }, { 0, 1, 1, 1 }, { 1, 0 } },
            { { +0.5f, -0.5f, -0.5f }, { 0, 1, 1, 1 }, { 1, 1 } },
        }};

        indices =
        {{
            0, 1, 2,  0, 2, 3,
            4, 5, 6,  4, 6, 7,
            8, 9,10,  8,10,11,
           12,13,14, 12,14,15,
           16,17,18, 16,18,19,
           20,21,22, 20,22,23,
        }};
    }

    static void WaitForFence(ID3D12Fence* fence, HANDLE fenceEvent, UINT64 value)
    {
        if (!fence || !fenceEvent)
            return;

        if (fence->GetCompletedValue() < value)
        {
            (void)fence->SetEventOnCompletion(value, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    static const char* HeapTypeToString(D3D12_HEAP_TYPE t)
    {
        switch (t)
        {
        case D3D12_HEAP_TYPE_DEFAULT: return "DEFAULT";
        case D3D12_HEAP_TYPE_UPLOAD:  return "UPLOAD";
        case D3D12_HEAP_TYPE_READBACK:return "READBACK";
        case D3D12_HEAP_TYPE_CUSTOM:  return "CUSTOM";
        default: return "(unknown)";
        }
    }

    static void LogResourceHeapTypeOnce(const char* name, ID3D12Resource* r)
    {
        if (!name || !r)
            return;

        D3D12_HEAP_PROPERTIES props{};
        D3D12_HEAP_FLAGS flags{};
        const HRESULT hr = r->GetHeapProperties(&props, &flags);
        if (FAILED(hr))
            return;

        Logger::Log(LogLevel::Info, std::string("Phase 4B sanity: ") + name + " heap=" + HeapTypeToString(props.Type), "[Mesh]");
    }
}

bool CreateCubeMesh(ID3D12Device* device, MeshData& outMesh)
{
    if (!device)
        return false;

    outMesh = {};

    std::array<VertexPC, 24> vertices{};
    std::array<uint16_t, 36> indices{};
    FillCubeData(vertices, indices);

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

bool CreateCubeMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh)
{
    if (!device || !commandList || !commandAllocator || !queue || !fence || !fenceEvent)
        return false;

    outMesh = {};

    std::array<VertexPC, 24> vertices{};
    std::array<uint16_t, 36> indices{};
    FillCubeData(vertices, indices);

    const UINT vbSize = (UINT)(vertices.size() * sizeof(VertexPC));
    const UINT ibSize = (UINT)(indices.size() * sizeof(uint16_t));

    Microsoft::WRL::ComPtr<ID3D12Resource> vbDefault;
    Microsoft::WRL::ComPtr<ID3D12Resource> ibDefault;
    Microsoft::WRL::ComPtr<ID3D12Resource> vbUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> ibUpload;

    static bool s_loggedPhase4BOnce = false;
    if (!s_loggedPhase4BOnce)
    {
        s_loggedPhase4BOnce = true;
        Logger::Log(LogLevel::Info, "Phase 4B sanity: creating cube mesh using DEFAULT heap + UPLOAD staging", "[Mesh]");
        Logger::Log(LogLevel::Info, std::string("Phase 4B sanity: VB bytes=") + std::to_string(vbSize) +
            " IB bytes=" + std::to_string(ibSize) +
            " IndexCount=" + std::to_string((uint32_t)indices.size()) +
            " IndexFormat=R16_UINT", "[Mesh]");
    }

    // Default heap resources start in COPY_DEST
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
        const CD3DX12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(vbDefault.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !vbDefault)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to create VB (DEFAULT).", "[Mesh]");
            return false;
        }

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(ibDefault.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !ibDefault)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to create IB (DEFAULT).", "[Mesh]");
            return false;
        }
    }

    // Upload heap staging buffers
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
        const CD3DX12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(vbUpload.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !vbUpload)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to create VB staging (UPLOAD).", "[Mesh]");
            return false;
        }

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ibUpload.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !ibUpload)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to create IB staging (UPLOAD).", "[Mesh]");
            return false;
        }

        void* mapped = nullptr;
        hr = vbUpload->Map(0, nullptr, &mapped);
        if (FAILED(hr) || !mapped)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to map VB staging.", "[Mesh]");
            return false;
        }
        std::memcpy(mapped, vertices.data(), vbSize);
        vbUpload->Unmap(0, nullptr);

        mapped = nullptr;
        hr = ibUpload->Map(0, nullptr, &mapped);
        if (FAILED(hr) || !mapped)
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: failed to map IB staging.", "[Mesh]");
            return false;
        }
        std::memcpy(mapped, indices.data(), ibSize);
        ibUpload->Unmap(0, nullptr);
    }

    // Record copy + barriers on provided allocator/command list
    {
        const HRESULT hrA = commandAllocator->Reset();
        if (FAILED(hrA))
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: commandAllocator->Reset failed.", "[Mesh]");
            return false;
        }

        const HRESULT hrCL = commandList->Reset(commandAllocator, nullptr);
        if (FAILED(hrCL))
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: commandList->Reset failed.", "[Mesh]");
            return false;
        }

        commandList->CopyBufferRegion(vbDefault.Get(), 0, vbUpload.Get(), 0, vbSize);
        commandList->CopyBufferRegion(ibDefault.Get(), 0, ibUpload.Get(), 0, ibSize);

        CD3DX12_RESOURCE_BARRIER barriers[2] =
        {
            CD3DX12_RESOURCE_BARRIER::Transition(vbDefault.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(ibDefault.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER),
        };
        commandList->ResourceBarrier(_countof(barriers), barriers);

        const HRESULT hrClose = commandList->Close();
        if (FAILED(hrClose))
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: commandList->Close failed.", "[Mesh]");
            return false;
        }

        ID3D12CommandList* lists[] = { commandList };
        queue->ExecuteCommandLists(1, lists);

        const UINT64 signalValue = ++inOutFenceValue;
        const HRESULT hrSig = queue->Signal(fence, signalValue);
        if (FAILED(hrSig))
        {
            Logger::Log(LogLevel::Error, "CreateCubeMeshDefaultHeap: queue->Signal failed.", "[Mesh]");
            return false;
        }

        WaitForFence(fence, fenceEvent, signalValue);
    }

    outMesh.VertexBuffer = vbDefault;
    outMesh.IndexBuffer = ibDefault;

    // One-time heap type proof (DEFAULT for VB/IB).
    static bool s_loggedHeapTypesOnce = false;
    if (!s_loggedHeapTypesOnce)
    {
        s_loggedHeapTypesOnce = true;
        LogResourceHeapTypeOnce("Cube VB", outMesh.VertexBuffer.Get());
        LogResourceHeapTypeOnce("Cube IB", outMesh.IndexBuffer.Get());
        LogResourceHeapTypeOnce("Cube VB staging", vbUpload.Get());
        LogResourceHeapTypeOnce("Cube IB staging", ibUpload.Get());
    }

    outMesh.VBV.BufferLocation = outMesh.VertexBuffer->GetGPUVirtualAddress();
    outMesh.VBV.StrideInBytes = sizeof(VertexPC);
    outMesh.VBV.SizeInBytes = vbSize;

    outMesh.IBV.BufferLocation = outMesh.IndexBuffer->GetGPUVirtualAddress();
    outMesh.IBV.Format = DXGI_FORMAT_R16_UINT;
    outMesh.IBV.SizeInBytes = ibSize;

    outMesh.IndexCount = (uint32_t)indices.size();

    static bool s_loggedOnce = false;
    if (!s_loggedOnce)
    {
        s_loggedOnce = true;
        Logger::Log(LogLevel::Info, "CreateCubeMeshDefaultHeap: cube mesh created (DEFAULT heap).", "[Mesh]");
    }

    return true;
}
