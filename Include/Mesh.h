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

bool CreateCubeMesh(ID3D12Device* device, MeshData& outMesh);
bool CreateSphereMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices = 24, uint32_t stacks = 16);
bool CreatePlaneMesh(ID3D12Device* device, MeshData& outMesh);
bool CreateCylinderMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices = 24);

bool CreateCubeMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh);

bool CreateSphereMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices = 24,
    uint32_t stacks = 16);

bool CreatePlaneMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh);

bool CreateCylinderMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices = 24);

