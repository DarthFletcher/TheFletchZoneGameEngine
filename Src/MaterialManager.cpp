#include "MaterialManager.h"

#include "Logger.h"

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
    material->SetFloat("metallic", material->metallic);
    material->SetFloat("roughness", material->roughness);

    Material* outMaterial = material.get();
    m_Materials.emplace(name, std::move(material));

    Logger::Log(LogLevel::Info, std::format("Created material: {}", name), "Material");
    return outMaterial;
}

Material* MaterialManager::GetMaterial(const std::string& name)
{
    if (auto it = m_Materials.find(name); it != m_Materials.end())
        return it->second.get();

    return nullptr;
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

void MaterialManager::Shutdown()
{
    m_Materials.clear();
}
