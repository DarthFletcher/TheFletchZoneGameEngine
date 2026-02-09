#pragma once

#include <cstdint>

#include "DX12Common.h"
#include <wrl.h>

// Phase 4A: POD mesh data owned by engine systems (Graphics).
// Mesh is NOT responsible for PSO, root signature, or shaders.
struct MeshData
{
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};

    UINT indexCount = 0;
};

// Builds a solid cube mesh (24 vertices, 36 indices).
// Upload heap only. No command list submission.
void CreateCubeMesh(ID3D12Device* device, MeshData& outMesh);
