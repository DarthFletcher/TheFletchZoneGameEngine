#pragma once

#include "DX12Common.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

struct Texture
{
    ID3D12Resource* resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCPU = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGPU = {};
    int width = 0;
    int height = 0;
    bool isCubemap = false;
    std::string sourcePath;
};

class TextureManager
{
public:
    static TextureManager& GetInstance();

    Texture* LoadTexture(const std::string& path);
    Texture* LoadCubemap(const std::array<std::string, 6>& facePaths);
    Texture* GetWhiteTexture();
    Texture* GetDefaultNormalTexture();
    Texture* GetDefaultSkyTexture();
    Texture* GetBuiltInSkyTexture(const std::string& presetId);
    void Shutdown();

private:
    struct TextureRecord
    {
        Texture texture = {};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };

    Texture* CreateTextureFromPixels(const std::string& key, const void* pixels, int width, int height);
    Texture* CreateTextureCubeFromPixels(const std::string& key, const std::array<const void*, 6>& facePixels, int width, int height);

    std::unordered_map<std::string, std::unique_ptr<TextureRecord>> m_Textures;
};
