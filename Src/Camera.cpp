#include "Camera.h"

#include <algorithm>

using namespace DirectX;

void Camera::SetPerspective(float fovY, float aspect, float nearZ, float farZ)
{
    fovY_ = fovY;
    aspect_ = aspect;
    nearZ_ = nearZ;
    farZ_ = farZ;
}

void Camera::SetLookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
    position_ = pos;
    target_ = target;
    up_ = up;
}

CameraData Camera::BuildDataLH() const
{
    CameraData out{};

    out.position = position_;
    out.fovY = fovY_;
    out.aspect = aspect_;
    out.nearZ = nearZ_;
    out.farZ = farZ_;

    const XMVECTOR eye = XMLoadFloat3(&position_);
    const XMVECTOR at = XMLoadFloat3(&target_);
    const XMVECTOR up = XMLoadFloat3(&up_);

    const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY_, (std::max)(aspect_, 0.0001f), nearZ_, farZ_);

    XMStoreFloat4x4(&out.view, view);
    XMStoreFloat4x4(&out.proj, proj);

    return out;
}
