#pragma once

#include "TextureManager.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class MaterialParamType
{
    Float,
    Texture
};

struct MaterialParameter
{
    std::string name;
    MaterialParamType type = MaterialParamType::Float;
    float floatValue = 0.0f;
    Texture* textureValue = nullptr;
};

struct Material
{
    std::string name;
    Texture* albedo = nullptr;
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::vector<MaterialParameter> parameters;

    void SetFloat(const std::string& name, float value);
    void SetTexture(const std::string& name, Texture* tex);
};

class MaterialManager
{
public:
    static MaterialManager& GetInstance();

    Material* CreateMaterial(const std::string& name);
    Material* GetMaterial(const std::string& name);
    Texture* LoadAlbedoTexture(Material* material, const std::string& texturePath);
    void Shutdown();

private:
    std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials;
};
