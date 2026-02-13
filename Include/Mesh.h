#pragma once

#include "DX12Common.h"
#include <cstdint>

struct MeshData
{
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;

    D3D12_VERTEX_BUFFER_VIEW VBV{};
    D3D12_INDEX_BUFFER_VIEW  IBV{};

    uint32_t IndexCount = 0;
};

// Creates a cube mesh in UPLOAD heap only (CPU-writable). No command list submission.
bool CreateCubeMesh(ID3D12Device* device, MeshData& outMesh);

// Phase 4B: creates the cube mesh in DEFAULT heap.
// Uses temporary UPLOAD heap staging buffers and records copy + barriers on `commandList`.
// Caller must provide a queue + fence and guarantees this runs outside the active frame loop.
bool CreateCubeMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh);

