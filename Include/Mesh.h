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

