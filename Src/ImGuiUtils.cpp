#include "imgui.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <format>
#include "ImGuiUtils.h"
#include "Graphics.h"
#include <wrl/client.h>
#include <wincodec.h> // WIC image decoding
#include <d3d12.h>
#include <DirectXTex.h> // Add this include for DirectX::ScratchImage
#include <windows.h>
#include <Utils.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "imgui_impl_dx12_custom.h" // for ImGui_ImplDX12_GetBackendData fallbacks

using Microsoft::WRL::ComPtr;

// 🔧 Ensure ImGui style alpha is within valid range [0.0, 1.0]
void SanitizeImGuiStyleAlpha()
{
    if (!ImGui::GetCurrentContext())
    {
        Logger::Log(LogLevel::Warning, "⚠️ SanitizeImGuiStyleAlpha() called with NULL ImGui context.");
        return;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    if (std::isnan(style.Alpha) || style.Alpha < 0.0f || style.Alpha > 1.0f)
    {
        Logger::Log(LogLevel::Warning,
            std::format("⚠️ style.Alpha was invalid ({:.3f}), clamping to [0.0, 1.0]", style.Alpha));
        style.Alpha = std::clamp(style.Alpha, 0.0f, 1.0f);
    }
}

// 🖼️ Load a texture from file and return it as an ImTextureID
static ImTextureID LoadTextureFromFile(const char* filename)
{
    if (!filename) {
        Logger::Log(LogLevel::Error, "❌ Null filename passed to LoadTextureFromFile.");
        return 0;
    }

    Logger::Log(LogLevel::Info, std::format("📁 Loading texture from file: {}", filename));

    // Replace previous path conversion usage
    DirectX::ScratchImage image;
    const std::wstring widePath = Utils::Utf8ToWide(filename); // UTF-8 → UTF-16 safe
    HRESULT hr = DirectX::LoadFromWICFile(widePath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, image);
    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to load image from file. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    
    const DirectX::Image* img = image.GetImage(0, 0, 0);

    // Describe the texture
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = img->width;
    texDesc.Height = static_cast<UINT>(img->height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = img->format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // Create texture resource
    ComPtr<ID3D12Resource> texture;
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    hr = Graphics::GetInstance().GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&texture)
    );

    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, std::format("❌ Failed to create texture resource. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }

    // Upload image to GPU
    ComPtr<ID3D12Resource> uploadBuffer;
    UINT64 uploadBufferSize = 0;
    Graphics::GetInstance().GetDevice()->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    hr = Graphics::GetInstance().GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE,
        &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&uploadBuffer)
    );

    if (FAILED(hr)) {
        Logger::Log(LogLevel::Error, "❌ Failed to create upload buffer.");
        return 0;
    }

    // Copy data into upload buffer
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = img->pixels;
    subresourceData.RowPitch = img->rowPitch;
    subresourceData.SlicePitch = img->slicePitch;
    
    // Record copy
    ID3D12GraphicsCommandList* cmdList = Graphics::GetInstance().GetCommandList().Get();
    UpdateSubresources(cmdList, texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresourceData);

    // Transition to pixel shader resource
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = Graphics::AllocateSRV();
    Graphics::GetInstance().GetDevice()->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    Logger::Log(LogLevel::Info, "✅ Splash texture loaded and uploaded to GPU.");

    return static_cast<ImTextureID>(cpuHandle.ptr);
}

// Public ImGuiUtils functions
ImTextureID ImGuiUtils::LoadTextureFromFile(const char* filename)
{
    return ::LoadTextureFromFile(filename);
}

// Load image from file into CPU memory
unsigned char* ImGuiUtils::LoadImageFromFile(
    const char* filename,
    int* outWidth,
    int* outHeight,
    int* outChannels)
{
    if (!filename)
        return nullptr;

    return stbi_load(filename, outWidth, outHeight, outChannels, 4);
    // force RGBA = 4 channels
}

// Free image data loaded with LoadImageFromFile
void ImGuiUtils::FreeCPUImage(unsigned char* data)
{
    if (data)
        stbi_image_free(data);
}

// Load image from file into CPU memory (wrapper with logging)
unsigned char* ImGuiUtils::LoadImageCPU(const char* filename, int& width, int& height, int& channels)
{
    // stb_image always outputs 4 channels (RGBA)
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);

    if (!data)
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ LoadImageCPU failed: {} (STB returned null)", filename));
        return nullptr;
    }

    channels = 4;
    Logger::Log(LogLevel::Info,
        std::format("📥 Loaded image '{}': {}x{} ({} channels)", filename, width, height, channels));

    return data;
}

// Free image data loaded with LoadImageCPU
void ImGuiUtils::FreeImageCPU(unsigned char* data)
{
    if (data)
        stbi_image_free(data);
}

// ============================================================================
// Create a GPU texture from CPU pixel data (ONE-SHOT DX12 UPLOAD)
// Returns ImTextureID for use in ImGui
// ============================================================================
ImTextureID ImGuiUtils::CreateTextureFromMemory(
    unsigned char* pixels,
    int width,
    int height,
    int channels)
{
    // ==========================================================
    // 0. Validate CPU data
    // ==========================================================
    if (!pixels || width <= 0 || height <= 0)
    {
        Logger::Log(LogLevel::Error,
            "❌ CreateTextureFromMemory() - Invalid CPU pixel data.");
        return 0;
    }

    // ==========================================================
    // 1. DX12 device/queue/heap validation with robust fallbacks
    // ==========================================================
    ID3D12Device* device = ImGui_ImplDX12_GetDevice();
    ID3D12CommandQueue* queue = ImGui_ImplDX12_GetCommandQueue();
    ID3D12DescriptorHeap* srvHeap = ImGui_ImplDX12_GetSrvHeap();

    // Prefer engine objects when available
    if (Graphics::GetInstance().GetDevice())
    {
        device = Graphics::GetInstance().GetDevice();
        queue = Graphics::GetInstance().GetCommandQueue();
        if (Graphics::GetInstance().GetImGuiSrvHeap())
            srvHeap = Graphics::GetInstance().GetImGuiSrvHeap();
    }

    if (!device || !queue || !srvHeap)
    {
        Logger::Log(LogLevel::Error,
            "❌ CreateTextureFromMemory() - Missing device/queue/heap.");
        return 0;
    }

    auto& gfx = Graphics::GetInstance();

    Logger::Log(LogLevel::Info,
        std::format("🔧 CreateTextureFromMemory() - Uploading {}x{} texture...", width, height));

    // ==========================================================
    // 2. Describe texture
    // ==========================================================
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = (UINT)width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    // ==========================================================
    // 2.5 Allocate SRV handles up-front from the same allocator ImGui uses
    // (ensures CPU+GPU handles are consistent and prevents descriptor index drift)
    // ==========================================================
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};

    ImGui_ImplDX12_InitInfo* initInfo = ImGui_ImplDX12_GetInitInfo();
    if (initInfo && initInfo->SrvDescriptorAllocFn)
    {
        initInfo->SrvDescriptorAllocFn(initInfo, &cpuHandle, &gpuHandle);
    }
    else
    {
        cpuHandle = Graphics::AllocateSRV();
        if (cpuHandle.ptr != 0)
        {
            const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = srvHeap->GetCPUDescriptorHandleForHeapStart();
            const UINT index = (UINT)((cpuHandle.ptr - cpuStart.ptr) / (SIZE_T)inc);
            const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = srvHeap->GetGPUDescriptorHandleForHeapStart();
            gpuHandle = { gpuStart.ptr + (UINT64)index * (UINT64)inc };
        }
    }

    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0)
    {
        Logger::Log(LogLevel::Error, "❌ CreateTextureFromMemory() - Failed to allocate SRV descriptor pair.");
        return 0;
    }

    // Diagnostic: the font atlas uses slot 0. If we ever get the same TexID, we're sampling the font texture.
    ImTextureID fontTex = 0;
    if (ImGui::GetCurrentContext() && ImGui::GetIO().Fonts)
        fontTex = ImGui::GetIO().Fonts->TexID;

    if (fontTex && (ImTextureID)gpuHandle.ptr == fontTex)
    {
        Logger::Log(LogLevel::Error, std::format(
            "❌ CreateTextureFromMemory() - SRV allocator returned FONT TexID (0x{:016X}). Rejecting allocation.",
            (uint64_t)gpuHandle.ptr));
        return 0;
    }

    Logger::Log(LogLevel::Info, std::format(
        "🧷 CreateTextureFromMemory() - Reserved SRV: CPU=0x{:016X} GPU=0x{:016X} (fontTex=0x{:016X})",
        (uint64_t)cpuHandle.ptr, (uint64_t)gpuHandle.ptr, (uint64_t)fontTex));

    // ==========================================================
    // 3. Allocate GPU texture + upload buffer
    // ==========================================================
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> uploadBuffer;

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));

    if (FAILED(hr) || !texture)
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed CreateCommittedResource (GPU). HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }

    // Give the resource a useful debug name (helps D3D12 debug layer diagnostics)
    texture->SetName(std::format(L"ImGuiUtilsTexture {}x{}", width, height).c_str());

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));

    if (FAILED(hr) || !uploadBuffer)
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed CreateCommittedResource (UPLOAD). HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }

    uploadBuffer->SetName(std::format(L"ImGuiUtilsTextureUpload {}x{}", width, height).c_str());

    // ==========================================================
    // 4. Record copy using a TEMP command allocator/list.
    // Avoid resetting the engine's per-frame allocator/list mid-frame.
    // ==========================================================
    ComPtr<ID3D12CommandAllocator> uploadAlloc;
    ComPtr<ID3D12GraphicsCommandList> uploadList;

    // Use a local fence to wait for this one-shot upload (do not depend on engine fences during boot).
    ComPtr<ID3D12Fence> uploadFence;
    HANDLE uploadFenceEvent = nullptr;
    UINT64 uploadFenceValue = 0;

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
    if (FAILED(hr) || !uploadFence)
    {
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to create upload fence. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!uploadFenceEvent)
    {
        Logger::Log(LogLevel::Error, "❌ CreateTextureFromMemory() - Failed to create upload fence event.");
        return 0;
    }

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc));
    if (FAILED(hr) || !uploadAlloc)
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to create upload command allocator. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    uploadAlloc->SetName(L"ImGuiUtilsTextureUploadAlloc");

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc.Get(), nullptr, IID_PPV_ARGS(&uploadList));
    if (FAILED(hr) || !uploadList)
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to create upload command list. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    uploadList->SetName(L"ImGuiUtilsTextureUploadList");

    // D3D12 command lists are created in the recording state; close + Reset to start from a clean known state.
    // This avoids edge cases where a caller assumes the list starts closed.
    (void)uploadList->Close();
    hr = uploadAlloc->Reset();
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to reset upload command allocator. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    hr = uploadList->Reset(uploadAlloc.Get(), nullptr);
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to reset upload command list. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }

    // Ensure the correct SRV heap is bound while recording (same heap that ImGui uses).
    {
        ID3D12DescriptorHeap* heaps[] = { srvHeap };
        uploadList->SetDescriptorHeaps(1, heaps);
    }

    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = pixels;
    subresource.RowPitch = (LONG_PTR)width * 4;
    subresource.SlicePitch = (LONG_PTR)width * (LONG_PTR)height * 4;

    UpdateSubresources(uploadList.Get(), texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subresource);

    CD3DX12_RESOURCE_BARRIER toPsResource = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    uploadList->ResourceBarrier(1, &toPsResource);

    DX_CHECK(uploadList->Close());
    {
        ID3D12CommandList* lists[] = { uploadList.Get() };
        queue->ExecuteCommandLists(1, lists);
    }

    // Signal + wait for upload completion.
    uploadFenceValue++;
    hr = queue->Signal(uploadFence.Get(), uploadFenceValue);
    if (FAILED(hr))
    {
        CloseHandle(uploadFenceEvent);
        Logger::Log(LogLevel::Error,
            std::format("❌ CreateTextureFromMemory() - Failed to signal upload fence. HRESULT=0x{:X}", (UINT)hr));
        return 0;
    }
    if (uploadFence->GetCompletedValue() < uploadFenceValue)
    {
        hr = uploadFence->SetEventOnCompletion(uploadFenceValue, uploadFenceEvent);
        if (FAILED(hr))
        {
            CloseHandle(uploadFenceEvent);
            Logger::Log(LogLevel::Error,
                std::format("❌ CreateTextureFromMemory() - SetEventOnCompletion failed. HRESULT=0x{:X}", (UINT)hr));
            return 0;
        }
        WaitForSingleObject(uploadFenceEvent, INFINITE);
    }
    CloseHandle(uploadFenceEvent);

    Logger::Log(LogLevel::Info, "🚀 CreateTextureFromMemory() - GPU upload complete.");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    Logger::Log(LogLevel::Info, std::format(
        "🧾 CreateTextureFromMemory() - SRV created | format={} cpu=0x{:016X} gpu=0x{:016X}",
        (int)srvDesc.Format, (uint64_t)cpuHandle.ptr, (uint64_t)gpuHandle.ptr));

    // ==========================================================
    // 6. Persist resources so the SRV stays valid
    // ==========================================================
    static std::vector<ComPtr<ID3D12Resource>> s_Textures;
    static std::vector<ComPtr<ID3D12Resource>> s_Uploads;
    static std::vector<ComPtr<ID3D12CommandAllocator>> s_UploadAllocs;
    static std::vector<ComPtr<ID3D12GraphicsCommandList>> s_UploadLists;
    s_Textures.push_back(texture);
    s_Uploads.push_back(uploadBuffer);
    s_UploadAllocs.push_back(uploadAlloc);
    s_UploadLists.push_back(uploadList);

    // ==========================================================
    // 7. Return ImTextureID as GPU descriptor handle (DX12 backend expects GPU handle)
    // ==========================================================
    return (ImTextureID)gpuHandle.ptr;
}


// Release all loaded textures (if tracking implemented)
void ImGuiUtils::ReleaseAllTextures()
{
	// Currently no texture tracking implemented
}
