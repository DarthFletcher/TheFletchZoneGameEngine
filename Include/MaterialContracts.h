#pragma once

#include <DirectXMath.h>
#include <cstddef>

struct alignas(256) MaterialCB
{
    DirectX::XMFLOAT4 materialBaseColor = { 0.78f, 0.80f, 0.84f, 1.0f };
    DirectX::XMFLOAT4 materialScalars = { 0.0f, 1.0f, 0.0f, 0.0f }; // metallic, roughness, flipNormalGreen, reserved
    DirectX::XMFLOAT4 materialTextureFlags = { 0.0f, 0.0f, 0.0f, 0.0f }; // albedo, normal, metallic, roughness
    DirectX::XMFLOAT4 materialReserved[13]{};

    void SetBaseColor(const DirectX::XMFLOAT3& color)
    {
        materialBaseColor = { color.x, color.y, color.z, 1.0f };
    }

    void SetMetallic(float value)
    {
        materialScalars.x = value;
    }

    void SetRoughness(float value)
    {
        materialScalars.y = value;
    }

    void SetFlipNormalGreen(bool enabled)
    {
        materialScalars.z = enabled ? 1.0f : 0.0f;
    }

    void SetUseAlbedoTexture(bool enabled)
    {
        materialTextureFlags.x = enabled ? 1.0f : 0.0f;
    }

    void SetUseNormalTexture(bool enabled)
    {
        materialTextureFlags.y = enabled ? 1.0f : 0.0f;
    }

    void SetUseMetallicTexture(bool enabled)
    {
        materialTextureFlags.z = enabled ? 1.0f : 0.0f;
    }

    void SetUseRoughnessTexture(bool enabled)
    {
        materialTextureFlags.w = enabled ? 1.0f : 0.0f;
    }
};

static_assert(sizeof(MaterialCB) == 256, "MaterialCB must be exactly 256 bytes");
