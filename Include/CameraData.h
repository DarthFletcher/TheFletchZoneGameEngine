#pragma once

#include <DirectXMath.h>

// Phase 3C: engine-owned camera state (POD, no logic).
// This is the authoritative data passed into `SceneRenderContext`.
struct CameraData
{
    DirectX::XMFLOAT4X4 view{};
    DirectX::XMFLOAT4X4 proj{};

    DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
    float pad0 = 0.0f;

    float fovY = DirectX::XMConvertToRadians(60.0f);
    float aspect = 1.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;
};
