#include "Mesh.h"

#include "Logger.h"
#include "Vertex.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

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

    static void FillCubeData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices)
    {
        vertices =
        {
            { { +0.5f, -0.5f, -0.5f }, { 1, 0, 0, 1 }, { +1, 0, 0 }, { 0, 1 } },
            { { +0.5f, +0.5f, -0.5f }, { 1, 0, 0, 1 }, { +1, 0, 0 }, { 0, 0 } },
            { { +0.5f, +0.5f, +0.5f }, { 1, 0, 0, 1 }, { +1, 0, 0 }, { 1, 0 } },
            { { +0.5f, -0.5f, +0.5f }, { 1, 0, 0, 1 }, { +1, 0, 0 }, { 1, 1 } },
            { { -0.5f, -0.5f, +0.5f }, { 0, 1, 0, 1 }, { -1, 0, 0 }, { 0, 1 } },
            { { -0.5f, +0.5f, +0.5f }, { 0, 1, 0, 1 }, { -1, 0, 0 }, { 0, 0 } },
            { { -0.5f, +0.5f, -0.5f }, { 0, 1, 0, 1 }, { -1, 0, 0 }, { 1, 0 } },
            { { -0.5f, -0.5f, -0.5f }, { 0, 1, 0, 1 }, { -1, 0, 0 }, { 1, 1 } },
            { { -0.5f, +0.5f, -0.5f }, { 0, 0, 1, 1 }, { 0, +1, 0 }, { 0, 1 } },
            { { -0.5f, +0.5f, +0.5f }, { 0, 0, 1, 1 }, { 0, +1, 0 }, { 0, 0 } },
            { { +0.5f, +0.5f, +0.5f }, { 0, 0, 1, 1 }, { 0, +1, 0 }, { 1, 0 } },
            { { +0.5f, +0.5f, -0.5f }, { 0, 0, 1, 1 }, { 0, +1, 0 }, { 1, 1 } },
            { { -0.5f, -0.5f, +0.5f }, { 1, 1, 0, 1 }, { 0, -1, 0 }, { 0, 1 } },
            { { -0.5f, -0.5f, -0.5f }, { 1, 1, 0, 1 }, { 0, -1, 0 }, { 0, 0 } },
            { { +0.5f, -0.5f, -0.5f }, { 1, 1, 0, 1 }, { 0, -1, 0 }, { 1, 0 } },
            { { +0.5f, -0.5f, +0.5f }, { 1, 1, 0, 1 }, { 0, -1, 0 }, { 1, 1 } },
            { { +0.5f, -0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 0, +1 }, { 0, 1 } },
            { { +0.5f, +0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 0, +1 }, { 0, 0 } },
            { { -0.5f, +0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 0, +1 }, { 1, 0 } },
            { { -0.5f, -0.5f, +0.5f }, { 1, 0, 1, 1 }, { 0, 0, +1 }, { 1, 1 } },
            { { -0.5f, -0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 0, -1 }, { 0, 1 } },
            { { -0.5f, +0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 0, -1 }, { 0, 0 } },
            { { +0.5f, +0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 0, -1 }, { 1, 0 } },
            { { +0.5f, -0.5f, -0.5f }, { 0, 1, 1, 1 }, { 0, 0, -1 }, { 1, 1 } },
        };
        indices = { 0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11, 12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23 };
    }

    static void FillPlaneData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices)
    {
        vertices =
        {
            { { -0.5f, 0.0f, -0.5f }, { 1,1,1,1 }, { 0, 1, 0 }, { 0, 1 } },
            { { -0.5f, 0.0f, +0.5f }, { 1,1,1,1 }, { 0, 1, 0 }, { 0, 0 } },
            { { +0.5f, 0.0f, +0.5f }, { 1,1,1,1 }, { 0, 1, 0 }, { 1, 0 } },
            { { +0.5f, 0.0f, -0.5f }, { 1,1,1,1 }, { 0, 1, 0 }, { 1, 1 } },
            { { -0.5f, 0.0f, -0.5f }, { 1,1,1,1 }, { 0,-1, 0 }, { 0, 1 } },
            { { +0.5f, 0.0f, -0.5f }, { 1,1,1,1 }, { 0,-1, 0 }, { 1, 1 } },
            { { +0.5f, 0.0f, +0.5f }, { 1,1,1,1 }, { 0,-1, 0 }, { 1, 0 } },
            { { -0.5f, 0.0f, +0.5f }, { 1,1,1,1 }, { 0,-1, 0 }, { 0, 0 } },
        };
        indices =
        {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
        };
    }

    static void FillSphereData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices, uint32_t slices, uint32_t stacks)
    {
        slices = (std::max)(slices, 3u);
        stacks = (std::max)(stacks, 2u);
        vertices.clear();
        indices.clear();
        vertices.reserve((slices + 1) * (stacks + 1));
        indices.reserve(slices * stacks * 6);

        for (uint32_t stack = 0; stack <= stacks; ++stack)
        {
            const float v = static_cast<float>(stack) / static_cast<float>(stacks);
            const float phi = v * DirectX::XM_PI;
            const float y = std::cos(phi) * 0.5f;
            const float r = std::sin(phi) * 0.5f;

            for (uint32_t slice = 0; slice <= slices; ++slice)
            {
                const float u = static_cast<float>(slice) / static_cast<float>(slices);
                const float theta = u * DirectX::XM_2PI;
                const float x = std::sin(theta) * r;
                const float z = std::cos(theta) * r;
                DirectX::XMFLOAT3 pos{ x, y, z };
                DirectX::XMVECTOR n = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&pos));
                DirectX::XMFLOAT3 normal{};
                DirectX::XMStoreFloat3(&normal, n);
                vertices.push_back({ pos, {1,1,1,1}, normal, { u, v } });
            }
        }

        for (uint32_t stack = 0; stack < stacks; ++stack)
        {
            for (uint32_t slice = 0; slice < slices; ++slice)
            {
                const uint16_t a = static_cast<uint16_t>(stack * (slices + 1) + slice);
                const uint16_t b = static_cast<uint16_t>(a + slices + 1);
                const uint16_t c = static_cast<uint16_t>(a + 1);
                const uint16_t d = static_cast<uint16_t>(b + 1);
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(c); indices.push_back(d); indices.push_back(b);
            }
        }
    }

    static void FillConeData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices, uint32_t slices)
    {
        slices = (std::max)(slices, 3u);
        vertices.clear();
        indices.clear();
        vertices.reserve((slices + 1u) * 2u + 2u);
        indices.reserve(slices * 12u);

        constexpr float radius = 0.5f;
        constexpr float baseY = -0.5f;
        constexpr float apexY = 0.5f;

        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * DirectX::XM_2PI;
            const float x = std::sin(theta) * radius;
            const float z = std::cos(theta) * radius;
            DirectX::XMVECTOR sideNormal = DirectX::XMVector3Normalize(DirectX::XMVectorSet(x, radius, z, 0.0f));
            DirectX::XMFLOAT3 normal{};
            DirectX::XMStoreFloat3(&normal, sideNormal);
            vertices.push_back({ { x, baseY, z }, {1,1,1,1}, normal, { u, 1.0f } });
            vertices.push_back({ { 0.0f, apexY, 0.0f }, {1,1,1,1}, normal, { u, 0.0f } });
        }

        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            const uint16_t base = static_cast<uint16_t>(slice * 2u);
            indices.push_back(base);
            indices.push_back(static_cast<uint16_t>(base + 1u));
            indices.push_back(static_cast<uint16_t>(base + 2u));
        }

        const uint16_t baseCenter = static_cast<uint16_t>(vertices.size());
        vertices.push_back({ { 0.0f, baseY, 0.0f }, {1,1,1,1}, { 0,-1,0 }, { 0.5f, 0.5f } });
        const uint16_t baseStart = static_cast<uint16_t>(vertices.size());
        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * DirectX::XM_2PI;
            const float x = std::sin(theta) * radius;
            const float z = std::cos(theta) * radius;
            vertices.push_back({ { x, baseY, z }, {1,1,1,1}, { 0,-1,0 }, { x + 0.5f, z + 0.5f } });
        }

        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            indices.push_back(baseCenter);
            indices.push_back(static_cast<uint16_t>(baseStart + slice + 1u));
            indices.push_back(static_cast<uint16_t>(baseStart + slice));
        }
    }

    static void FillTorusData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices, uint32_t majorSegments, uint32_t minorSegments)
    {
        majorSegments = (std::max)(majorSegments, 8u);
        minorSegments = (std::max)(minorSegments, 6u);

        constexpr float majorRadius = 0.5f;
        constexpr float minorRadius = 0.18f;

        vertices.clear();
        indices.clear();
        vertices.reserve((majorSegments + 1u) * (minorSegments + 1u));
        indices.reserve(majorSegments * minorSegments * 6u);

        for (uint32_t major = 0; major <= majorSegments; ++major)
        {
            const float u = static_cast<float>(major) / static_cast<float>(majorSegments);
            const float phi = u * DirectX::XM_2PI;
            const float cp = std::cos(phi);
            const float sp = std::sin(phi);

            for (uint32_t minor = 0; minor <= minorSegments; ++minor)
            {
                const float v = static_cast<float>(minor) / static_cast<float>(minorSegments);
                const float theta = v * DirectX::XM_2PI;
                const float ct = std::cos(theta);
                const float st = std::sin(theta);

                const float ringRadius = majorRadius + minorRadius * ct;
                const float x = ringRadius * sp;
                const float y = minorRadius * st;
                const float z = ringRadius * cp;

                DirectX::XMFLOAT3 normal{ ct * sp, st, ct * cp };
                vertices.push_back({ { x, y, z }, { 1,1,1,1 }, normal, { u, v } });
            }
        }

        const uint16_t stride = static_cast<uint16_t>(minorSegments + 1u);
        for (uint16_t major = 0; major < majorSegments; ++major)
        {
            const uint16_t currentStart = static_cast<uint16_t>(major * stride);
            const uint16_t nextStart = static_cast<uint16_t>((major + 1u) * stride);
            for (uint16_t minor = 0; minor < minorSegments; ++minor)
            {
                const uint16_t a = static_cast<uint16_t>(currentStart + minor);
                const uint16_t b = static_cast<uint16_t>(nextStart + minor);
                const uint16_t c = static_cast<uint16_t>(a + 1u);
                const uint16_t d = static_cast<uint16_t>(b + 1u);
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(c); indices.push_back(d); indices.push_back(b);
            }
        }
    }

    static void FillCapsuleData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices, uint32_t slices, uint32_t hemiStacks)
    {
        slices = (std::max)(slices, 6u);
        hemiStacks = (std::max)(hemiStacks, 2u);

        vertices.clear();
        indices.clear();

        constexpr float radius = 0.5f;
        constexpr float cylinderHalfHeight = 0.25f;
        const uint32_t ringCount = (hemiStacks + 1u) * 2u;
        vertices.reserve((ringCount) * (slices + 1u));
        indices.reserve((ringCount - 1u) * slices * 6u);

        auto appendRing = [&](float centerY, float startAngle, float endAngle)
        {
            for (uint32_t stack = 0; stack <= hemiStacks; ++stack)
            {
                const float t = static_cast<float>(stack) / static_cast<float>(hemiStacks);
                const float angle = startAngle + (endAngle - startAngle) * t;
                const float ringRadius = std::cos(angle) * radius;
                const float yOffset = std::sin(angle) * radius;
                const float y = centerY + yOffset;
                const float v = static_cast<float>(vertices.size() / (slices + 1u)) / static_cast<float>(ringCount - 1u);

                for (uint32_t slice = 0; slice <= slices; ++slice)
                {
                    const float u = static_cast<float>(slice) / static_cast<float>(slices);
                    const float theta = u * DirectX::XM_2PI;
                    const float x = std::sin(theta) * ringRadius;
                    const float z = std::cos(theta) * ringRadius;
                    DirectX::XMFLOAT3 normal{ 0.0f, 0.0f, 0.0f };
                    if (centerY < 0.0f)
                    {
                        normal = { x / radius, yOffset / radius, z / radius };
                    }
                    else
                    {
                        normal = { x / radius, yOffset / radius, z / radius };
                    }

                    vertices.push_back({ { x, y, z }, { 1,1,1,1 }, normal, { u, v } });
                }
            }
        };

        appendRing(-cylinderHalfHeight, -DirectX::XM_PIDIV2, 0.0f);
        appendRing(+cylinderHalfHeight, 0.0f, DirectX::XM_PIDIV2);

        const uint16_t stride = static_cast<uint16_t>(slices + 1u);
        for (uint16_t ring = 0; ring + 1u < ringCount; ++ring)
        {
            const uint16_t currentStart = ring * stride;
            const uint16_t nextStart = static_cast<uint16_t>((ring + 1u) * stride);
            for (uint16_t slice = 0; slice < slices; ++slice)
            {
                const uint16_t a = static_cast<uint16_t>(currentStart + slice);
                const uint16_t b = static_cast<uint16_t>(nextStart + slice);
                const uint16_t c = static_cast<uint16_t>(a + 1u);
                const uint16_t d = static_cast<uint16_t>(b + 1u);
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(c); indices.push_back(d); indices.push_back(b);
            }
        }
    }

    static void FillCylinderData(std::vector<VertexPC>& vertices, std::vector<uint16_t>& indices, uint32_t slices)
    {
        slices = (std::max)(slices, 3u);
        vertices.clear();
        indices.clear();
        vertices.reserve((slices + 1) * 4 + 2);
        indices.reserve(slices * 12);

        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * DirectX::XM_2PI;
            const float x = std::sin(theta) * 0.5f;
            const float z = std::cos(theta) * 0.5f;
            const DirectX::XMFLOAT3 normal{ x * 2.0f, 0.0f, z * 2.0f };
            vertices.push_back({ { x, -0.5f, z }, {1,1,1,1}, normal, { u, 1.0f } });
            vertices.push_back({ { x, +0.5f, z }, {1,1,1,1}, normal, { u, 0.0f } });
        }

        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            const uint16_t base = static_cast<uint16_t>(slice * 2);
            indices.push_back(base); indices.push_back(base + 2); indices.push_back(base + 1);
            indices.push_back(base + 2); indices.push_back(base + 3); indices.push_back(base + 1);
        }

        const uint16_t topCenter = static_cast<uint16_t>(vertices.size());
        vertices.push_back({ { 0.0f, +0.5f, 0.0f }, {1,1,1,1}, { 0,1,0 }, { 0.5f, 0.5f } });
        const uint16_t bottomCenter = static_cast<uint16_t>(vertices.size());
        vertices.push_back({ { 0.0f, -0.5f, 0.0f }, {1,1,1,1}, { 0,-1,0 }, { 0.5f, 0.5f } });

        const uint16_t topStart = static_cast<uint16_t>(vertices.size());
        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * DirectX::XM_2PI;
            const float x = std::sin(theta) * 0.5f;
            const float z = std::cos(theta) * 0.5f;
            vertices.push_back({ { x, +0.5f, z }, {1,1,1,1}, { 0,1,0 }, { x + 0.5f, z + 0.5f } });
        }
        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            indices.push_back(topCenter);
            indices.push_back(static_cast<uint16_t>(topStart + slice));
            indices.push_back(static_cast<uint16_t>(topStart + slice + 1));
        }

        const uint16_t bottomStart = static_cast<uint16_t>(vertices.size());
        for (uint32_t slice = 0; slice <= slices; ++slice)
        {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * DirectX::XM_2PI;
            const float x = std::sin(theta) * 0.5f;
            const float z = std::cos(theta) * 0.5f;
            vertices.push_back({ { x, -0.5f, z }, {1,1,1,1}, { 0,-1,0 }, { x + 0.5f, z + 0.5f } });
        }
        for (uint32_t slice = 0; slice < slices; ++slice)
        {
            indices.push_back(bottomCenter);
            indices.push_back(static_cast<uint16_t>(bottomStart + slice + 1));
            indices.push_back(static_cast<uint16_t>(bottomStart + slice));
        }
    }

    static bool CreateUploadMesh(ID3D12Device* device, const std::vector<VertexPC>& vertices, const std::vector<uint16_t>& indices, MeshData& outMesh)
    {
        if (!device || vertices.empty() || indices.empty())
            return false;
        outMesh = {};
        const UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(VertexPC));
        const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint16_t));
        const D3D12_HEAP_PROPERTIES heapProps = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        {
            const D3D12_RESOURCE_DESC desc = MakeBufferDesc(vbSize);
            const HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(outMesh.VertexBuffer.ReleaseAndGetAddressOf()));
            if (FAILED(hr) || !outMesh.VertexBuffer) return false;
            void* mapped = nullptr;
            if (FAILED(outMesh.VertexBuffer->Map(0, nullptr, &mapped)) || !mapped) return false;
            std::memcpy(mapped, vertices.data(), vbSize);
            outMesh.VertexBuffer->Unmap(0, nullptr);
            outMesh.VBV.BufferLocation = outMesh.VertexBuffer->GetGPUVirtualAddress();
            outMesh.VBV.SizeInBytes = vbSize;
            outMesh.VBV.StrideInBytes = sizeof(VertexPC);
        }
        {
            const D3D12_RESOURCE_DESC desc = MakeBufferDesc(ibSize);
            const HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(outMesh.IndexBuffer.ReleaseAndGetAddressOf()));
            if (FAILED(hr) || !outMesh.IndexBuffer) return false;
            void* mapped = nullptr;
            if (FAILED(outMesh.IndexBuffer->Map(0, nullptr, &mapped)) || !mapped) return false;
            std::memcpy(mapped, indices.data(), ibSize);
            outMesh.IndexBuffer->Unmap(0, nullptr);
            outMesh.IBV.BufferLocation = outMesh.IndexBuffer->GetGPUVirtualAddress();
            outMesh.IBV.SizeInBytes = ibSize;
            outMesh.IBV.Format = DXGI_FORMAT_R16_UINT;
        }
        outMesh.IndexCount = static_cast<uint32_t>(indices.size());
        return true;
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

    static bool CreateDefaultMesh(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandAllocator* commandAllocator, ID3D12CommandQueue* queue, ID3D12Fence* fence, HANDLE fenceEvent, UINT64& inOutFenceValue, const std::vector<VertexPC>& vertices, const std::vector<uint16_t>& indices, MeshData& outMesh, const char* label)
    {
        if (!device || !commandList || !commandAllocator || !queue || !fence || !fenceEvent || vertices.empty() || indices.empty())
            return false;
        outMesh = {};
        const UINT vbSize = static_cast<UINT>(vertices.size() * sizeof(VertexPC));
        const UINT ibSize = static_cast<UINT>(indices.size() * sizeof(uint16_t));
        Microsoft::WRL::ComPtr<ID3D12Resource> vbDefault, ibDefault, vbUpload, ibUpload;
        const D3D12_HEAP_PROPERTIES defaultHeap = MakeHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_HEAP_PROPERTIES uploadHeap = MakeHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC vbDesc = MakeBufferDesc(vbSize);
        const D3D12_RESOURCE_DESC ibDesc = MakeBufferDesc(ibSize);
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&vbDefault))) ||
            FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ibDefault))) ||
            FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vbUpload))) ||
            FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ibUpload))))
            return false;
        void* mapped = nullptr;
        if (FAILED(vbUpload->Map(0, nullptr, &mapped)) || !mapped) return false;
        std::memcpy(mapped, vertices.data(), vbSize);
        vbUpload->Unmap(0, nullptr);
        if (FAILED(ibUpload->Map(0, nullptr, &mapped)) || !mapped) return false;
        std::memcpy(mapped, indices.data(), ibSize);
        ibUpload->Unmap(0, nullptr);
        if (FAILED(commandAllocator->Reset()) || FAILED(commandList->Reset(commandAllocator, nullptr))) return false;
        commandList->CopyBufferRegion(vbDefault.Get(), 0, vbUpload.Get(), 0, vbSize);
        commandList->CopyBufferRegion(ibDefault.Get(), 0, ibUpload.Get(), 0, ibSize);
        D3D12_RESOURCE_BARRIER barriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(vbDefault.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(ibDefault.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
        };
        commandList->ResourceBarrier(2, barriers);
        if (FAILED(commandList->Close())) return false;
        ID3D12CommandList* lists[] = { commandList };
        queue->ExecuteCommandLists(1, lists);
        const UINT64 fenceValue = ++inOutFenceValue;
        if (FAILED(queue->Signal(fence, fenceValue))) return false;
        WaitForFence(fence, fenceEvent, fenceValue);
        outMesh.VertexBuffer = vbDefault;
        outMesh.IndexBuffer = ibDefault;
        outMesh.VBV.BufferLocation = vbDefault->GetGPUVirtualAddress();
        outMesh.VBV.SizeInBytes = vbSize;
        outMesh.VBV.StrideInBytes = sizeof(VertexPC);
        outMesh.IBV.BufferLocation = ibDefault->GetGPUVirtualAddress();
        outMesh.IBV.SizeInBytes = ibSize;
        outMesh.IBV.Format = DXGI_FORMAT_R16_UINT;
        outMesh.IndexCount = static_cast<uint32_t>(indices.size());
        if (label)
            Logger::Log(LogLevel::Info, std::string(label), "[Mesh]");
        return true;
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
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCubeData(vertices, indices);
    const bool ok = CreateUploadMesh(device, vertices, indices, outMesh);
    if (ok)
        Logger::Log(LogLevel::Info, "CreateCubeMesh: cube mesh created (UPLOAD heap only).", "[Mesh]");
    return ok;
}

bool CreateSphereMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices, uint32_t stacks)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillSphereData(vertices, indices, slices, stacks);
    return CreateUploadMesh(device, vertices, indices, outMesh);
}

bool CreatePlaneMesh(ID3D12Device* device, MeshData& outMesh)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillPlaneData(vertices, indices);
    return CreateUploadMesh(device, vertices, indices, outMesh);
}

bool CreateCylinderMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCylinderData(vertices, indices, slices);
    return CreateUploadMesh(device, vertices, indices, outMesh);
}

bool CreateCapsuleMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices, uint32_t hemiStacks)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCapsuleData(vertices, indices, slices, hemiStacks);
    return CreateUploadMesh(device, vertices, indices, outMesh);
}

bool CreateTorusMesh(ID3D12Device* device, MeshData& outMesh, uint32_t majorSegments, uint32_t minorSegments)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillTorusData(vertices, indices, majorSegments, minorSegments);
    return CreateUploadMesh(device, vertices, indices, outMesh);
}

bool CreateConeMesh(ID3D12Device* device, MeshData& outMesh, uint32_t slices)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillConeData(vertices, indices, slices);
    return CreateUploadMesh(device, vertices, indices, outMesh);
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
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCubeData(vertices, indices);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateCubeMeshDefaultHeap: cube mesh created (DEFAULT heap).");
}

bool CreateSphereMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices,
    uint32_t stacks)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillSphereData(vertices, indices, slices, stacks);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateSphereMeshDefaultHeap: sphere mesh created (DEFAULT heap).");
}

bool CreatePlaneMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillPlaneData(vertices, indices);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreatePlaneMeshDefaultHeap: plane mesh created (DEFAULT heap).");
}

bool CreateCylinderMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCylinderData(vertices, indices, slices);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateCylinderMeshDefaultHeap: cylinder mesh created (DEFAULT heap).");
}

bool CreateCapsuleMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices,
    uint32_t hemiStacks)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillCapsuleData(vertices, indices, slices, hemiStacks);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateCapsuleMeshDefaultHeap: capsule mesh created (DEFAULT heap).");
}

bool CreateTorusMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t majorSegments,
    uint32_t minorSegments)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillTorusData(vertices, indices, majorSegments, minorSegments);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateTorusMeshDefaultHeap: torus mesh created (DEFAULT heap).");
}

bool CreateConeMeshDefaultHeap(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12CommandQueue* queue,
    ID3D12Fence* fence,
    HANDLE fenceEvent,
    UINT64& inOutFenceValue,
    MeshData& outMesh,
    uint32_t slices)
{
    std::vector<VertexPC> vertices;
    std::vector<uint16_t> indices;
    FillConeData(vertices, indices, slices);
    return CreateDefaultMesh(device, commandList, commandAllocator, queue, fence, fenceEvent, inOutFenceValue, vertices, indices, outMesh, "CreateConeMeshDefaultHeap: cone mesh created (DEFAULT heap).");
}
