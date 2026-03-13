#pragma once

#include "DX12Common.h"

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
};

class TextureManager
{
public:
    static TextureManager& GetInstance();

    Texture* LoadTexture(const std::string& path);
    void Shutdown();

private:
    struct TextureRecord
    {
        Texture texture = {};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };

    std::unordered_map<std::string, std::unique_ptr<TextureRecord>> m_Textures;
};
