#pragma once

#include <d3d12.h>
#include <wrl/client.h>

class ImGui_ImplDX12_DescriptorHeap
{
public:
    // Initializes the descriptor heap with specified type and count
    bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors);

    // Allocates the next available CPU descriptor handle
    D3D12_CPU_DESCRIPTOR_HANDLE Allocate();

    // Returns the GPU handle to the start of the heap (used in descriptor tables)
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandleStart() const;

    // Returns the raw heap pointer (for setting descriptor heaps)
    ID3D12DescriptorHeap* GetHeap() const;

    // Resets allocation counter for reuse in the next frame
    void Reset();

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap = nullptr;  // The descriptor heap
    ID3D12Device* device = nullptr;                               // Weak reference to the device
    UINT descriptorSize = 0;                                      // Size of each descriptor
    UINT totalDescriptors = 0;                                    // Maximum descriptors in heap
    UINT allocatedCount = 0;                                      // Current allocation index
};
