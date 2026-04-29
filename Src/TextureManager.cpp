#include "TextureManager.h"

#include "Graphics.h"
#include "Logger.h"

#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "stb_image.h"

using Microsoft::WRL::ComPtr;

namespace
{
    struct BuiltInSkyPalette
    {
        float topR = 0.10f;
        float topG = 0.22f;
        float topB = 0.46f;
        float horizonR = 0.78f;
        float horizonG = 0.58f;
        float horizonB = 0.30f;
        float bottomR = 0.03f;
        float bottomG = 0.05f;
        float bottomB = 0.10f;
        float glowCenterU = 0.72f;
        float glowStrengthR = 0.18f;
        float glowStrengthG = 0.12f;
        float glowStrengthB = 0.08f;
    };

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

    BuiltInSkyPalette GetBuiltInSkyPalette(const std::string& presetId)
    {
        if (_stricmp(presetId.c_str(), "Day") == 0)
        {
            return { 0.16f, 0.42f, 0.78f, 0.72f, 0.86f, 0.98f, 0.55f, 0.68f, 0.85f, 0.58f, 0.08f, 0.06f, 0.04f };
        }
        if (_stricmp(presetId.c_str(), "Night") == 0)
        {
            return { 0.01f, 0.03f, 0.10f, 0.10f, 0.12f, 0.22f, 0.01f, 0.01f, 0.04f, 0.80f, 0.05f, 0.06f, 0.10f };
        }
        if (_stricmp(presetId.c_str(), "Void") == 0)
        {
            return { 0.03f, 0.00f, 0.04f, 0.20f, 0.03f, 0.14f, 0.01f, 0.00f, 0.02f, 0.66f, 0.16f, 0.02f, 0.12f };
        }

        return { 0.10f, 0.22f, 0.46f, 0.78f, 0.58f, 0.30f, 0.03f, 0.05f, 0.10f, 0.72f, 0.18f, 0.12f, 0.08f };
    }
}

TextureManager& TextureManager::GetInstance()
{
    static TextureManager instance;
    return instance;
}

Texture* TextureManager::CreateTextureFromPixels(const std::string& key, const void* pixels, int width, int height)
{
    if (key.empty() || !pixels || width <= 0 || height <= 0)
        return nullptr;

    if (auto it = m_Textures.find(key); it != m_Textures.end())
        return &it->second->texture;

    auto& gfx = Graphics::GetInstance();
    gfx.AssertNotInRender("TextureManager::CreateTextureFromPixels");

    ID3D12Device* device = gfx.GetDevice();
    ID3D12CommandQueue* queue = gfx.GetCommandQueue();
    ID3D12DescriptorHeap* srvHeap = gfx.GetImGuiSrvHeap();
    if (!device || !queue || !srvHeap)
    {
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
        Logger::Log(LogLevel::Error,
            std::format("Failed to create GPU texture for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
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
        Logger::Log(LogLevel::Error,
            std::format("Failed to create upload texture for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
    if (FAILED(hr) || !uploadAllocator)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload allocator for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12GraphicsCommandList> uploadList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList));
    if (FAILED(hr) || !uploadList)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload command list for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
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
        Logger::Log(LogLevel::Error,
            std::format("Failed to close texture upload list for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ID3D12CommandList* lists[] = { uploadList.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> uploadFence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
    if (FAILED(hr) || !uploadFence)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    HANDLE uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!uploadFenceEvent)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create texture upload fence event for '{}'.", key),
            "Texture");
        return nullptr;
    }

    constexpr UINT64 uploadFenceValue = 1;
    hr = queue->Signal(uploadFence.Get(), uploadFenceValue);
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("Failed to signal texture upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    if (uploadFence->GetCompletedValue() < uploadFenceValue)
    {
        hr = uploadFence->SetEventOnCompletion(uploadFenceValue, uploadFenceEvent);
        if (FAILED(hr))
        {
            CloseHandle(uploadFenceEvent);
            Logger::Log(LogLevel::Error,
                std::format("Failed to wait for texture upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
                "Texture");
            return nullptr;
        }

        WaitForSingleObject(uploadFenceEvent, INFINITE);
    }

    CloseHandle(uploadFenceEvent);

    const D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics::AllocateSRV();
    if (srvCpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to allocate SRV descriptor for '{}'.", key),
            "Texture");
        return nullptr;
    }

    UINT srvIndex = 0;
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = ComputeGpuHandle(srvHeap, device, srvCpu, &srvIndex);
    if (srvGpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to compute GPU SRV handle for '{}'.", key),
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
    outTexture->sourcePath = key;
    m_Textures.emplace(key, std::move(record));
    return outTexture;
}

Texture* TextureManager::CreateTextureCubeFromPixels(const std::string& key, const std::array<const void*, 6>& facePixels, int width, int height)
{
    if (key.empty() || width <= 0 || height <= 0)
        return nullptr;

    for (const void* facePixelsPtr : facePixels)
    {
        if (!facePixelsPtr)
            return nullptr;
    }

    if (auto it = m_Textures.find(key); it != m_Textures.end())
        return &it->second->texture;

    auto& gfx = Graphics::GetInstance();
    gfx.AssertNotInRender("TextureManager::CreateTextureCubeFromPixels");

    ID3D12Device* device = gfx.GetDevice();
    ID3D12CommandQueue* queue = gfx.GetCommandQueue();
    ID3D12DescriptorHeap* srvHeap = gfx.GetImGuiSrvHeap();
    if (!device || !queue || !srvHeap)
    {
        Logger::Log(LogLevel::Error, "TextureManager requires a valid device, command queue, and SRV heap.", "Texture");
        return nullptr;
    }

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.DepthOrArraySize = 6;
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
        Logger::Log(LogLevel::Error,
            std::format("Failed to create GPU cubemap for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 6, 0, nullptr, nullptr, nullptr, &uploadSize);

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
        Logger::Log(LogLevel::Error,
            std::format("Failed to create upload cubemap for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
    if (FAILED(hr) || !uploadAllocator)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create cubemap upload allocator for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ComPtr<ID3D12GraphicsCommandList> uploadList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList));
    if (FAILED(hr) || !uploadList)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create cubemap upload command list for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    std::array<D3D12_SUBRESOURCE_DATA, 6> subresources{};
    for (size_t faceIndex = 0; faceIndex < subresources.size(); ++faceIndex)
    {
        subresources[faceIndex].pData = facePixels[faceIndex];
        subresources[faceIndex].RowPitch = static_cast<LONG_PTR>(width) * 4;
        subresources[faceIndex].SlicePitch = static_cast<LONG_PTR>(width) * static_cast<LONG_PTR>(height) * 4;
    }

    UpdateSubresources(uploadList.Get(), textureResource.Get(), uploadResource.Get(), 0, 0, static_cast<UINT>(subresources.size()), subresources.data());

    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        textureResource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    uploadList->ResourceBarrier(1, &barrier);

    hr = uploadList->Close();
    if (FAILED(hr))
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to close cubemap upload list for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    ID3D12CommandList* lists[] = { uploadList.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> uploadFence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
    if (FAILED(hr) || !uploadFence)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create cubemap upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    HANDLE uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!uploadFenceEvent)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to create cubemap upload fence event for '{}'.", key),
            "Texture");
        return nullptr;
    }

    constexpr UINT64 uploadFenceValue = 1;
    hr = queue->Signal(uploadFence.Get(), uploadFenceValue);
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("Failed to signal cubemap upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
            "Texture");
        return nullptr;
    }

    if (uploadFence->GetCompletedValue() < uploadFenceValue)
    {
        hr = uploadFence->SetEventOnCompletion(uploadFenceValue, uploadFenceEvent);
        if (FAILED(hr))
        {
            CloseHandle(uploadFenceEvent);
            Logger::Log(LogLevel::Error,
                std::format("Failed to wait for cubemap upload fence for '{}' HR=0x{:08X}", key, static_cast<UINT>(hr)),
                "Texture");
            return nullptr;
        }

        WaitForSingleObject(uploadFenceEvent, INFINITE);
    }

    CloseHandle(uploadFenceEvent);

    const D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics::AllocateSRV();
    if (srvCpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to allocate cubemap SRV descriptor for '{}'.", key),
            "Texture");
        return nullptr;
    }

    UINT srvIndex = 0;
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = ComputeGpuHandle(srvHeap, device, srvCpu, &srvIndex);
    if (srvGpu.ptr == 0)
    {
        Logger::Log(LogLevel::Error,
            std::format("Failed to compute GPU cubemap SRV handle for '{}'.", key),
            "Texture");
        return nullptr;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCpu);

    auto record = std::make_unique<TextureRecord>();
    record->resource = textureResource;
    record->texture.resource = record->resource.Get();
    record->texture.srvCPU = srvCpu;
    record->texture.srvGPU = srvGpu;
    record->texture.width = width;
    record->texture.height = height;
    record->texture.isCubemap = true;
    record->texture.sourcePath = key;

    Texture* outTexture = &record->texture;
    m_Textures.emplace(key, std::move(record));
    return outTexture;
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
    unsigned char* pixels = stbi_load(normalizedPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        const char* reason = stbi_failure_reason();
        Logger::Log(LogLevel::Error,
            std::format("Failed to decode texture '{}': {}", normalizedPath, reason ? reason : "unknown stb_image error"),
            "Texture");
        return nullptr;
    }

    Texture* texture = CreateTextureFromPixels(normalizedPath, pixels, width, height);
    stbi_image_free(pixels);
    if (!texture)
        return nullptr;

    const UINT increment = Graphics::GetInstance().GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = Graphics::GetInstance().GetImGuiSrvHeap()->GetCPUDescriptorHandleForHeapStart();
    const UINT srvIndex = static_cast<UINT>((texture->srvCPU.ptr - cpuStart.ptr) / static_cast<SIZE_T>(increment));

    Logger::Log(LogLevel::Info,
        std::format("Loaded texture\npath={}\nsize={}x{}\nsrvIndex={}", normalizedPath, width, height, srvIndex),
        "Texture");

    return texture;
}

Texture* TextureManager::LoadCubemap(const std::array<std::string, 6>& facePaths)
{
    bool hasAnyFace = false;
    for (const std::string& facePath : facePaths)
    {
        if (!facePath.empty())
        {
            hasAnyFace = true;
            break;
        }
    }
    if (!hasAnyFace)
        return nullptr;

    std::array<std::string, 6> normalizedPaths{};
    for (size_t faceIndex = 0; faceIndex < facePaths.size(); ++faceIndex)
    {
        if (facePaths[faceIndex].empty())
            return nullptr;
        normalizedPaths[faceIndex] = NormalizeTexturePath(facePaths[faceIndex]);
    }

    std::string key = "__cubemap__/";
    for (size_t faceIndex = 0; faceIndex < normalizedPaths.size(); ++faceIndex)
    {
        if (faceIndex > 0)
            key += "|";
        key += normalizedPaths[faceIndex];
    }

    if (auto it = m_Textures.find(key); it != m_Textures.end())
        return &it->second->texture;

    std::array<std::vector<unsigned char>, 6> facePixelStorage;
    std::array<const void*, 6> facePixels{};
    int width = 0;
    int height = 0;

    for (size_t faceIndex = 0; faceIndex < normalizedPaths.size(); ++faceIndex)
    {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(normalizedPaths[faceIndex].c_str(), &faceWidth, &faceHeight, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            const char* reason = stbi_failure_reason();
            Logger::Log(LogLevel::Error,
                std::format("Failed to decode cubemap face '{}': {}", normalizedPaths[faceIndex], reason ? reason : "unknown stb_image error"),
                "Texture");
            return nullptr;
        }

        if (faceIndex == 0)
        {
            width = faceWidth;
            height = faceHeight;
        }
        else if (faceWidth != width || faceHeight != height)
        {
            stbi_image_free(pixels);
            Logger::Log(LogLevel::Error,
                std::format("Cubemap faces must share the same dimensions. '{}' was {}x{} instead of {}x{}.", normalizedPaths[faceIndex], faceWidth, faceHeight, width, height),
                "Texture");
            return nullptr;
        }

        facePixelStorage[faceIndex].assign(pixels, pixels + (static_cast<size_t>(faceWidth) * static_cast<size_t>(faceHeight) * 4u));
        stbi_image_free(pixels);
        facePixels[faceIndex] = facePixelStorage[faceIndex].data();
    }

    Texture* texture = CreateTextureCubeFromPixels(key, facePixels, width, height);
    if (!texture)
        return nullptr;

    Logger::Log(LogLevel::Info,
        std::format("Loaded cubemap\nsize={}x{}\nkey={}", width, height, key),
        "Texture");
    return texture;
}

Texture* TextureManager::GetWhiteTexture()
{
    static constexpr unsigned char kWhitePixel[4] = { 255, 255, 255, 255 };
    return CreateTextureFromPixels("__builtin__/white", kWhitePixel, 1, 1);
}

Texture* TextureManager::GetDefaultNormalTexture()
{
    static constexpr unsigned char kDefaultNormalPixel[4] = { 128, 128, 255, 255 };
    return CreateTextureFromPixels("__builtin__/default_normal", kDefaultNormalPixel, 1, 1);
}

Texture* TextureManager::GetDefaultSkyTexture()
{
    return GetBuiltInSkyTexture("Sunset");
}

Texture* TextureManager::GetBuiltInSkyTexture(const std::string& presetId)
{
    constexpr int kWidth = 256;
    constexpr int kHeight = 128;
    const std::string normalizedPreset = presetId.empty() ? "Sunset" : presetId;
    const std::string key = std::format("__builtin__/sky/{}", normalizedPreset);
    if (auto it = m_Textures.find(key); it != m_Textures.end())
        return &it->second->texture;

    const BuiltInSkyPalette palette = GetBuiltInSkyPalette(normalizedPreset);
    std::vector<unsigned char> pixels(static_cast<size_t>(kWidth * kHeight * 4));

    for (int y = 0; y < kHeight; ++y)
    {
        const float t = static_cast<float>(y) / static_cast<float>(kHeight - 1);
        const float horizonBlend = std::pow(1.0f - std::fabs(t * 2.0f - 1.0f), 1.4f);

        const bool upperHemisphere = t < 0.5f;
        const float hemiT = upperHemisphere ? (t / 0.5f) : ((t - 0.5f) / 0.5f);

        const float baseR = upperHemisphere ? (palette.topR + (palette.horizonR - palette.topR) * hemiT) : (palette.horizonR + (palette.bottomR - palette.horizonR) * hemiT);
        const float baseG = upperHemisphere ? (palette.topG + (palette.horizonG - palette.topG) * hemiT) : (palette.horizonG + (palette.bottomG - palette.horizonG) * hemiT);
        const float baseB = upperHemisphere ? (palette.topB + (palette.horizonB - palette.topB) * hemiT) : (palette.horizonB + (palette.bottomB - palette.horizonB) * hemiT);

        for (int x = 0; x < kWidth; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(kWidth - 1);
            const float sunGlow = std::exp(-std::pow((u - palette.glowCenterU) * 10.0f, 2.0f)) * horizonBlend;
            const size_t index = static_cast<size_t>((y * kWidth + x) * 4);
            pixels[index + 0] = static_cast<unsigned char>(std::clamp((baseR + sunGlow * palette.glowStrengthR) * 255.0f, 0.0f, 255.0f));
            pixels[index + 1] = static_cast<unsigned char>(std::clamp((baseG + sunGlow * palette.glowStrengthG) * 255.0f, 0.0f, 255.0f));
            pixels[index + 2] = static_cast<unsigned char>(std::clamp((baseB + sunGlow * palette.glowStrengthB) * 255.0f, 0.0f, 255.0f));
            pixels[index + 3] = 255;
        }
    }

    return CreateTextureFromPixels(key, pixels.data(), kWidth, kHeight);
}

void TextureManager::Shutdown()
{
    m_Textures.clear();
}
