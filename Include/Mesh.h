#pragma once

#include "DX12Common.h"

struct MeshData
{
    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;

    D3D12_VERTEX_BUFFER_VIEW VBV{};
    D3D12_INDEX_BUFFER_VIEW  IBV{};

    UINT IndexCount = 0;

    void Reset()
    {
        VertexBuffer.Reset();
        IndexBuffer.Reset();
        VBV = {};
        IBV = {};
        IndexCount = 0;
    }
};

bool CreateCubeMesh(ID3D12Device* device, MeshData& outMesh);
