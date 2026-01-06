#include "ImGui_ImplDX12_DescriptorHeap.h"
#include "Logger.h"
#include <format>

// Initialize the descriptor heap for ImGui usage (CBV/SRV/UAV heap)
bool ImGui_ImplDX12_DescriptorHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors)
{
    this->device = device;
    this->totalDescriptors = numDescriptors;
    this->allocatedCount = 0;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to create descriptor heap: HRESULT = 0x{:08X}", hr));
        return false;
    }

    descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    Logger::Log(LogLevel::Info, std::format("✅ ImGui Descriptor Heap created with {} descriptors.", numDescriptors));
    return true;
}

// Allocate a descriptor from the heap
D3D12_CPU_DESCRIPTOR_HANDLE ImGui_ImplDX12_DescriptorHeap::Allocate()
{
    if (allocatedCount >= totalDescriptors)
    {
        Logger::Log(LogLevel::Error, std::format("❌ Descriptor heap exhausted: {}/{}", allocatedCount, totalDescriptors));
        return {0}; // Invalid handle (null)
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = {
        heap->GetCPUDescriptorHandleForHeapStart().ptr + allocatedCount * descriptorSize
    };
    allocatedCount++;

    Logger::Log(LogLevel::Verbose, std::format("🆕 Allocated descriptor {} / {}", allocatedCount, totalDescriptors));
    return handle;
}

// Get the GPU handle to start of heap (for root descriptor tables)
D3D12_GPU_DESCRIPTOR_HANDLE ImGui_ImplDX12_DescriptorHeap::GetGpuHandleStart() const
{
    return heap->GetGPUDescriptorHandleForHeapStart();
}

// Get raw heap pointer
ID3D12DescriptorHeap* ImGui_ImplDX12_DescriptorHeap::GetHeap() const
{
    return heap.Get();
}

// Reset heap allocation count (call this before each frame's allocations)
void ImGui_ImplDX12_DescriptorHeap::Reset()
{
    Logger::Log(LogLevel::Verbose, std::format("🔄 Resetting descriptor heap ({} used / {} total)", allocatedCount, totalDescriptors));
    allocatedCount = 0;
}
