#include "SceneCamera.h"

using namespace DirectX;

XMMATRIX SceneCamera::GetView() const
{
    return XMMatrixLookAtLH(eye_, target_, up_);
}

XMMATRIX SceneCamera::GetProj(float aspect) const
{
    if (aspect <= 0.0f)
        aspect = 1.0f;
    return XMMatrixPerspectiveFovLH(fovY_, aspect, nearZ_, farZ_);
}
