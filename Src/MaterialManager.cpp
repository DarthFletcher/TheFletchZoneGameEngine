#include "MaterialManager.h"
#include "Logger.h"
#include "Scene.h"

#include <algorithm>
#include <format>

namespace
{
    MaterialParameter* FindParameter(Material& material, const std::string& name, MaterialParamType type)
    {
        for (auto& parameter : material.parameters)
        {
            if (parameter.name == name && parameter.type == type)
                return &parameter;
        }

        return nullptr;
    }
}

std::string MaterialManager::MakeUniqueMaterialName(const std::string& baseName) const
{
    const std::string seed = baseName.empty() ? "Material" : baseName;
    if (!m_Materials.contains(seed))
        return seed;

    for (int suffix = 1;; ++suffix)
    {
        const std::string candidate = std::format("{}_{}", seed, suffix);
        if (!m_Materials.contains(candidate))
            return candidate;
    }
}

void Material::SetFloat(const std::string& paramName, float value)
{
    if (MaterialParameter* parameter = FindParameter(*this, paramName, MaterialParamType::Float))
    {
        parameter->floatValue = value;
    }
    else
    {
        MaterialParameter newParameter{};
        newParameter.name = paramName;
        newParameter.type = MaterialParamType::Float;
        newParameter.floatValue = value;
        parameters.push_back(std::move(newParameter));
    }

    if (paramName == "metallic")
        metallic = value;
    else if (paramName == "roughness")
        roughness = value;
    else if (paramName == "baseColorR")
        baseColor.x = value;
    else if (paramName == "baseColorG")
        baseColor.y = value;
    else if (paramName == "baseColorB")
        baseColor.z = value;
}

void Material::SetBaseColor(const DirectX::XMFLOAT3& color)
{
    SetFloat("baseColorR", color.x);
    SetFloat("baseColorG", color.y);
    SetFloat("baseColorB", color.z);
}

void Material::SetTexture(const std::string& paramName, Texture* tex)
{
    if (MaterialParameter* parameter = FindParameter(*this, paramName, MaterialParamType::Texture))
    {
        parameter->textureValue = tex;
    }
    else
    {
        MaterialParameter newParameter{};
        newParameter.name = paramName;
        newParameter.type = MaterialParamType::Texture;
        newParameter.textureValue = tex;
        parameters.push_back(std::move(newParameter));
    }

    if (paramName == "albedo")
        albedo = tex;
    else if (paramName == "normal")
        normal = tex;
    else if (paramName == "metallicMap")
        metallicMap = tex;
    else if (paramName == "roughnessMap")
        roughnessMap = tex;
}

MaterialManager& MaterialManager::GetInstance()
{
    static MaterialManager instance;
    return instance;
}

Material* MaterialManager::CreateMaterial(const std::string& name)
{
    if (name.empty())
    {
        Logger::Log(LogLevel::Error, "CreateMaterial called with empty name.", "Material");
        return nullptr;
    }

    if (auto it = m_Materials.find(name); it != m_Materials.end())
        return it->second.get();

    auto material = std::make_unique<Material>();
    material->name = name;
    material->SetTexture("albedo", nullptr);
    material->SetTexture("normal", nullptr);
    material->SetTexture("metallicMap", nullptr);
    material->SetTexture("roughnessMap", nullptr);
    material->SetBaseColor(material->baseColor);
    material->SetFloat("metallic", material->metallic);
    material->SetFloat("roughness", material->roughness);

    Material* outMaterial = material.get();
    m_Materials.emplace(name, std::move(material));
    m_MaterialOrder.push_back(name);

    Logger::Log(LogLevel::Info, std::format("Created material: {}", name), "Material");
    return outMaterial;
}

Material* MaterialManager::DuplicateMaterial(const Material& source, const std::string& newName)
{
    const std::string copyName = MakeUniqueMaterialName(
        newName.empty() ? std::format("{}_Copy", source.name.empty() ? "Material" : source.name) : newName);

    auto material = std::make_unique<Material>(source);
    material->name = copyName;

    Material* outMaterial = material.get();
    m_Materials.emplace(copyName, std::move(material));
    m_MaterialOrder.push_back(copyName);

    Logger::Log(LogLevel::Info, std::format("Duplicated material: {} -> {}", source.name, copyName), "Material");
    return outMaterial;
}

bool MaterialManager::RenameMaterialByIndex(int index, const std::string& requestedName, std::string& outRenamedName)
{
    outRenamedName.clear();
    if (index < 0 || index >= static_cast<int>(m_MaterialOrder.size()))
        return false;

    std::string sanitized = requestedName;
    sanitized.erase(sanitized.begin(), std::find_if(sanitized.begin(), sanitized.end(), [](unsigned char c) { return !std::isspace(c); }));
    sanitized.erase(std::find_if(sanitized.rbegin(), sanitized.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), sanitized.end());
    if (sanitized.empty())
        return false;

    const std::string oldName = m_MaterialOrder[index];
    auto it = m_Materials.find(oldName);
    if (it == m_Materials.end() || !it->second)
        return false;

    const std::string newName = (sanitized == oldName) ? oldName : MakeUniqueMaterialName(sanitized);
    if (newName == oldName)
    {
        outRenamedName = oldName;
        return true;
    }

    std::unique_ptr<Material> material = std::move(it->second);
    m_Materials.erase(it);
    material->name = newName;
    m_Materials.emplace(newName, std::move(material));
    m_MaterialOrder[index] = newName;
    outRenamedName = newName;

    Logger::Log(LogLevel::Info, std::format("Renamed material: {} -> {}", oldName, newName), "Material");
    return true;
}

bool MaterialManager::DeleteMaterialByIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_MaterialOrder.size()))
        return false;
    if (m_MaterialOrder.size() <= 1)
    {
        Logger::Log(LogLevel::Warning, "Cannot delete the last material asset.", "Material");
        return false;
    }

    const std::string removedName = m_MaterialOrder[index];
    auto it = m_Materials.find(removedName);
    if (it == m_Materials.end())
        return false;

    Material* removedMaterial = it->second.get();
    m_PendingTextureLoads.erase(
        std::remove_if(
            m_PendingTextureLoads.begin(),
            m_PendingTextureLoads.end(),
            [removedMaterial](const PendingTextureLoad& pendingLoad)
            {
                return pendingLoad.material == removedMaterial;
            }),
        m_PendingTextureLoads.end());

    for (UINT sceneIndex = 0; sceneIndex < Scene::GetInstanceCount(); ++sceneIndex)
    {
        if (SceneInstance* instance = Scene::GetInstance(sceneIndex))
        {
            if (instance->materialIndex == index)
                instance->materialIndex = 0;
            else if (instance->materialIndex > index)
                --instance->materialIndex;
        }
    }

    m_Materials.erase(it);
    m_MaterialOrder.erase(m_MaterialOrder.begin() + index);
    Scene::RebuildRenderInstancesFromSceneData();
    Scene::MarkInstancesDirty();

    Logger::Log(LogLevel::Info, std::format("Deleted material: {}", removedName), "Material");
    return true;
}

Material* MaterialManager::GetMaterial(const std::string& name)
{
    if (auto it = m_Materials.find(name); it != m_Materials.end())
        return it->second.get();

    return nullptr;
}

Material* MaterialManager::GetMaterialByIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_MaterialOrder.size()))
        return nullptr;
    return GetMaterial(m_MaterialOrder[index]);
}

const std::string* MaterialManager::GetMaterialNameByIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_MaterialOrder.size()))
        return nullptr;
    return &m_MaterialOrder[index];
}

int MaterialManager::GetMaterialCount() const
{
    return static_cast<int>(m_MaterialOrder.size());
}

Texture* MaterialManager::LoadAlbedoTexture(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "LoadAlbedoTexture called with null material.", "Material");
        return nullptr;
    }

    Texture* texture = TextureManager::GetInstance().LoadTexture(texturePath);
    if (!texture)
        return nullptr;

    material->SetTexture("albedo", texture);
    return texture;
}

Texture* MaterialManager::LoadNormalTexture(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "LoadNormalTexture called with null material.", "Material");
        return nullptr;
    }

    Texture* texture = TextureManager::GetInstance().LoadTexture(texturePath);
    if (!texture)
        return nullptr;

    material->SetTexture("normal", texture);
    return texture;
}

Texture* MaterialManager::LoadMetallicTexture(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "LoadMetallicTexture called with null material.", "Material");
        return nullptr;
    }

    Texture* texture = TextureManager::GetInstance().LoadTexture(texturePath);
    if (!texture)
        return nullptr;

    material->SetTexture("metallicMap", texture);
    return texture;
}

Texture* MaterialManager::LoadRoughnessTexture(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "LoadRoughnessTexture called with null material.", "Material");
        return nullptr;
    }

    Texture* texture = TextureManager::GetInstance().LoadTexture(texturePath);
    if (!texture)
        return nullptr;

    material->SetTexture("roughnessMap", texture);
    return texture;
}

void MaterialManager::QueueAlbedoTextureLoad(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "QueueAlbedoTextureLoad called with null material.", "Material");
        return;
    }

    if (texturePath.empty())
    {
        Logger::Log(LogLevel::Error, "QueueAlbedoTextureLoad called with empty path.", "Material");
        return;
    }

    for (auto& pendingLoad : m_PendingTextureLoads)
    {
        if (pendingLoad.material == material && pendingLoad.parameterName == "albedo")
        {
            pendingLoad.texturePath = texturePath;
            return;
        }
    }

    m_PendingTextureLoads.push_back({ material, "albedo", texturePath });
}

void MaterialManager::QueueNormalTextureLoad(Material* material, const std::string& texturePath)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "QueueNormalTextureLoad called with null material.", "Material");
        return;
    }

    if (texturePath.empty())
    {
        Logger::Log(LogLevel::Error, "QueueNormalTextureLoad called with empty path.", "Material");
        return;
    }

    for (auto& pendingLoad : m_PendingTextureLoads)
    {
        if (pendingLoad.material == material && pendingLoad.parameterName == "normal")
        {
            pendingLoad.texturePath = texturePath;
            return;
        }
    }

    m_PendingTextureLoads.push_back({ material, "normal", texturePath });
}

void MaterialManager::ProcessPendingTextureLoads()
{
    if (m_PendingTextureLoads.empty())
        return;

    std::vector<PendingTextureLoad> pendingLoads = std::move(m_PendingTextureLoads);
    m_PendingTextureLoads.clear();

    for (const PendingTextureLoad& pendingLoad : pendingLoads)
    {
        if (!pendingLoad.material)
            continue;

        Texture* texture = TextureManager::GetInstance().LoadTexture(pendingLoad.texturePath);
        if (texture)
            pendingLoad.material->SetTexture(pendingLoad.parameterName, texture);
    }
}

void MaterialManager::SetAlbedoTexture(Material* material, Texture* texture)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "SetAlbedoTexture called with null material.", "Material");
        return;
    }

    material->SetTexture("albedo", texture);
}

void MaterialManager::SetNormalTexture(Material* material, Texture* texture)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "SetNormalTexture called with null material.", "Material");
        return;
    }

    material->SetTexture("normal", texture);
}

void MaterialManager::SetMetallicTexture(Material* material, Texture* texture)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "SetMetallicTexture called with null material.", "Material");
        return;
    }

    material->SetTexture("metallicMap", texture);
}

void MaterialManager::SetRoughnessTexture(Material* material, Texture* texture)
{
    if (!material)
    {
        Logger::Log(LogLevel::Error, "SetRoughnessTexture called with null material.", "Material");
        return;
    }

    material->SetTexture("roughnessMap", texture);
}

void MaterialManager::Shutdown()
{
    m_PendingTextureLoads.clear();
    m_MaterialOrder.clear();
    m_Materials.clear();
}
