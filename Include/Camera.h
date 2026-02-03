#pragma once

#include "CameraData.h"

#include <DirectXMath.h>

class Camera
{
public:
    void SetPerspective(float fovY, float aspect, float nearZ, float farZ);
    void SetLookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

    CameraData BuildDataLH() const;

private:
    DirectX::XMFLOAT3 position_{ 0.0f, 0.0f, -5.0f };
    DirectX::XMFLOAT3 target_{ 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 up_{ 0.0f, 1.0f, 0.0f };

    float fovY_ = DirectX::XMConvertToRadians(60.0f);
    float aspect_ = 1.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;
};
