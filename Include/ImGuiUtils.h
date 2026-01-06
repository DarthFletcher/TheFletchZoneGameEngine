#pragma once
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

// Structure to hold loaded texture information
struct LoadedTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
    ImTextureID TextureID;
};

// Public ImGuiUtils functions
namespace ImGuiUtils {
// ----------------------------------------------------------
// GPU texture loader from file
// ----------------------------------------------------------
    ImTextureID LoadTextureFromFile(const char* filename);
    void ReleaseAllTextures();

// ----------------------------------------------------------
// CPU image loader (returns raw pixel data)
// ----------------------------------------------------------
    unsigned char* LoadImageCPU(const char* filename, int& width, int& height, int& channels);
    void FreeImageCPU(unsigned char* data);

// ----------------------------------------------------------
// CPU-only image loaders (no GPU interaction)
// ----------------------------------------------------------
    unsigned char* LoadImageFromFile(
        const char* filename,
        int* outWidth,
        int* outHeight,
        int* outChannels);

    void FreeCPUImage(unsigned char* data);

// ----------------------------------------------------------
// GPU texture creator from CPU memory
// ----------------------------------------------------------
    ImTextureID CreateTextureFromMemory(
        unsigned char* pixels,
        int width,
        int height,
        int channels);
}

// 🔧 Ensure ImGui style alpha is within valid range [0.0, 1.0]
void SanitizeImGuiStyleAlpha();