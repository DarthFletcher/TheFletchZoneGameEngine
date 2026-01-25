#pragma once

#include <DirectXMath.h>

class SceneCamera
{
public:
    SceneCamera() = default;

    DirectX::XMMATRIX GetView() const;
    DirectX::XMMATRIX GetProj(float aspect) const;

    void SetEye(float x, float y, float z) { eye_ = DirectX::XMVectorSet(x, y, z, 1.0f); }
    void SetTarget(float x, float y, float z) { target_ = DirectX::XMVectorSet(x, y, z, 1.0f); }
    void SetUp(float x, float y, float z) { up_ = DirectX::XMVectorSet(x, y, z, 0.0f); }

    void SetFovYRadians(float fovY) { fovY_ = fovY; }
    void SetNearFar(float nearZ, float farZ) { nearZ_ = nearZ; farZ_ = farZ; }

    // Orbit camera controls (Scene view)
    void UpdateOrbit(float deltaX, float deltaY, float wheelDelta, bool orbit, bool pan, bool precision);
    DirectX::XMVECTOR GetEye() const { return eye_; }

    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetDistance() const { return distance_; }
    DirectX::XMVECTOR GetTarget() const { return target_; }

private:
    // Backing vectors used for rendering.
    DirectX::XMVECTOR eye_ = DirectX::XMVectorSet(0.0f, 2.5f, -4.5f, 1.0f);
    DirectX::XMVECTOR target_ = DirectX::XMVectorZero();
    DirectX::XMVECTOR up_ = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // Orbit state
    float yaw_ = 0.0f;
    float pitch_ = 0.35f;
    float distance_ = 5.0f;

    float fovY_ = DirectX::XMConvertToRadians(60.0f);
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;
};
