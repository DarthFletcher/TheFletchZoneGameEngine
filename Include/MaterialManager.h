#pragma once

#include "TextureManager.h"

#include <DirectXMath.h>
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
    Texture* normal = nullptr;
    Texture* metallicMap = nullptr;
    Texture* roughnessMap = nullptr;
    DirectX::XMFLOAT3 baseColor{ 0.78f, 0.80f, 0.84f };
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::vector<MaterialParameter> parameters;

    void SetFloat(const std::string& name, float value);
    void SetTexture(const std::string& name, Texture* tex);
    void SetBaseColor(const DirectX::XMFLOAT3& color);
};

class MaterialManager
{
public:
    static MaterialManager& GetInstance();

    Material* CreateMaterial(const std::string& name);
    Material* DuplicateMaterial(const Material& source, const std::string& newName = "");
    bool RenameMaterialByIndex(int index, const std::string& requestedName, std::string& outRenamedName);
    bool DeleteMaterialByIndex(int index);
    Material* GetMaterial(const std::string& name);
    Material* GetMaterialByIndex(int index);
    const std::string* GetMaterialNameByIndex(int index) const;
    int GetMaterialCount() const;
    Texture* LoadAlbedoTexture(Material* material, const std::string& texturePath);
    Texture* LoadNormalTexture(Material* material, const std::string& texturePath);
    Texture* LoadMetallicTexture(Material* material, const std::string& texturePath);
    Texture* LoadRoughnessTexture(Material* material, const std::string& texturePath);
    void QueueAlbedoTextureLoad(Material* material, const std::string& texturePath);
    void QueueNormalTextureLoad(Material* material, const std::string& texturePath);
    void ProcessPendingTextureLoads();
    void SetAlbedoTexture(Material* material, Texture* texture);
    void SetNormalTexture(Material* material, Texture* texture);
    void SetMetallicTexture(Material* material, Texture* texture);
    void SetRoughnessTexture(Material* material, Texture* texture);
    void Shutdown();

private:
    struct PendingTextureLoad
    {
        Material* material = nullptr;
        std::string parameterName;
        std::string texturePath;
    };

    std::string MakeUniqueMaterialName(const std::string& baseName) const;

    std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials;
    std::vector<std::string> m_MaterialOrder;
    std::vector<PendingTextureLoad> m_PendingTextureLoads;
};
