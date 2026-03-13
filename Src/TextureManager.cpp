#include "TextureManager.h"

#include "Graphics.h"
#include "Logger.h"

#include <filesystem>
#include <format>
#include <string>

#include "stb_image.h"

using Microsoft::WRL::ComPtr;

namespace
{
    std::string NormalizeTexturePath(const std::string& path)
    {
        std::filesystem::path fsPath(path);
        std::error_code ec;

        if (fsPath.is_relative())
            fsPath = std::filesystem::absolute(fsPath, ec);

        if (!ec)
            fsPath = fsPath.lexically_normal();

        return fsPath.string();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ComputeGpuHandle(
        ID3D12DescriptorHeap* heap,
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        UINT* outIndex)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        if (!heap || !device || cpuHandle.ptr == 0)
            return gpuHandle;

        const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = heap->GetGPUDescriptorHandleForHeapStart();
        const UINT index = static_cast<UINT>((cpuHandle.ptr - cpuStart.ptr) / static_cast<SIZE_T>(increment));

        gpuHandle.ptr = gpuStart.ptr + static_cast<UINT64>(index) * static_cast<UINT64>(increment);
        if (outIndex)
            *outIndex = index;

        return gpuHandle;
    }
}

TextureManager& TextureManager::GetInstance()
{
    static TextureManager instance;
    return instance;
}

Texture* TextureManager::LoadTexture(const std::string& path)
{
    if (path.empty())
    {
        Logger::Log(LogLevel::Error, "LoadTexture called with empty path.", "Texture");
        return nullptr;
    }

    const std::string normalizedPath = NormalizeTexturePath(path);
    if (auto it = m_Textures.find(normalizedPath); it != m_Textures.end())
        return &it->second->texture;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        Logger::Log(LogLevel::Error,
            std::format("Failed to decode texture '{}': {}", normalizedPath, reason ? reason : "unknown stb_image error"),
            "Texture");
        return nullptr;
    }

    auto& gfx = Graphics::GetInstance();
    gfx.AssertNotInRender("TextureManager::LoadTexture");

    ID3D12Device* device = gfx.GetDevice();
    ID3D12CommandQueue* queue = gfx.GetCommandQueue();
    ID3D12DescriptorHeap* srvHeap = gfx.GetImGuiSrvHeap();
    if (!device || !queue || !srvHeap)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error, "TextureManager requires a valid device, command queue, and SRV heap.", "Texture");
        return nullptr;
    }

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> textureResource;
    const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&textureResource));
    if (FAILED(hr) || !textureResource)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create GPU texture for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    ComPtr<ID3D12Resource> uploadResource;
    const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadResource));
    if (FAILED(hr) || !uploadResource)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create upload texture for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
    if (FAILED(hr) || !uploadAllocator)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload allocator for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12GraphicsCommandList> uploadList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList));
    if (FAILED(hr) || !uploadList)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload command list for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = pixels;
    subresource.RowPitch = static_cast<LONG_PTR>(width) * 4;
    subresource.SlicePitch = static_cast<LONG_PTR>(width) * static_cast<LONG_PTR>(height) * 4;

    UpdateSubresources(uploadList.Get(), textureResource.Get(), uploadResource.Get(), 0, 0, 1, &subresource);

    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        textureResource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    uploadList->ResourceBarrier(1, &barrier);

    hr = uploadList->Close();
    if (FAILED(hr))
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to close texture upload list for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ID3D12CommandList* lists[] = { uploadList.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> uploadFence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
    if (FAILED(hr) || !uploadFence)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload fence for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    HANDLE uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!uploadFenceEvent)
    {
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload fence event for '{}'.", normalizedPath),
            "Texture");
        return nullptr;
    }

    constexpr UINT64 uploadFenceValue = 1;
    hr = queue->Signal(uploadFence.Get(), uploadFenceValue);
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        stbi_image_free(pixels);
        Logger::Log(LogLevel::Error,
            std::format("Failed to signal texture upload fence for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    if (uploadFence->GetCompletedValue() < uploadFenceValue)
    {
        hr = uploadFence->SetEventOnCompletion(uploadFenceValue, uploadFenceEvent);
        if (FAILED(hr))
        {
            CloseHandle(uploadFenceEvent);
            stbi_image_free(pixels);
            Logger::Log(LogLevel::Error,
                std::format("Failed to wait for texture upload fence for '{}' HR=0x{:08X}", normalizedPath, static_cast<UINT>(hr)),
                "Texture");
            return nullptr;
        }

        WaitForSingleObject(uploadFenceEvent, INFINITE);
    }

    CloseHandle(uploadFenceEvent);
    stbi_image_free(pixels);

    const D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics::AllocateSRV();
    if (srvCpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to allocate SRV descriptor for '{}'.", normalizedPath),
            "Texture");
        return nullptr;
    }

    UINT srvIndex = 0;
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = ComputeGpuHandle(srvHeap, device, srvCpu, &srvIndex);
    if (srvGpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to compute GPU SRV handle for '{}'.", normalizedPath),
            "Texture");
        return nullptr;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCpu);

    auto record = std::make_unique<TextureRecord>();
    record->resource = textureResource;
    record->texture.resource = record->resource.Get();
    record->texture.srvCPU = srvCpu;
    record->texture.srvGPU = srvGpu;
    record->texture.width = width;
    record->texture.height = height;

    Texture* outTexture = &record->texture;
    m_Textures.emplace(normalizedPath, std::move(record));

    Logger::Log(LogLevel::Info,
        std::format("Loaded texture\npath={}\nsize={}x{}\nsrvIndex={}", normalizedPath, width, height, srvIndex),
        "Texture");

    return outTexture;
}

void TextureManager::Shutdown()
{
    m_Textures.clear();
}
