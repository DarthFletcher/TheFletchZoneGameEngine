#include "SceneCamera.h"

#include <algorithm>

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

XMMATRIX SceneCamera::GetOrthoProj(float aspect) const
{
    if (aspect <= 0.0f)
        aspect = 1.0f;

    const float h = (std::max)(0.001f, orthoHeight_);
    const float w = h * aspect;
    return XMMatrixOrthographicLH(w, h, nearZ_, farZ_);
}

void SceneCamera::UpdateOrbit(float deltaX, float deltaY, float wheelDelta, bool orbit, bool pan, bool precision)
{
    const float orbitSpeed = precision ? 0.0035f : 0.01f;
    const float panSpeed = precision ? 0.0035f : 0.01f;

    if (orbit)
    {
        yaw_ += deltaX * orbitSpeed;
        pitch_ += deltaY * orbitSpeed;

        // Clamp pitch to avoid gimbal flip.
        pitch_ = (std::clamp)(pitch_, -1.45f, 1.45f);
    }

    // Mouse wheel zoom (WM_MOUSEWHEEL is typically 120 per notch)
    if (wheelDelta != 0)
    {
        const float zoomSpeed = precision ? 0.0015f : 0.005f;

        if (orbit)
        {
            // Perspective/orbit zoom.
            distance_ *= (1.0f - (wheelDelta / 120.0f) * zoomSpeed);
            distance_ = (std::clamp)(distance_, 0.25f, 500.0f);
        }
        else
        {
            // Ortho-style zoom (used by 2D mode; also sensible when orbit disabled).
            orthoHeight_ *= (1.0f - (wheelDelta / 120.0f) * zoomSpeed);
            orthoHeight_ = (std::clamp)(orthoHeight_, 0.05f, 5000.0f);
        }
    }

    // Pan in view plane.
    if (pan)
    {
        const XMVECTOR forward = XMVector3Normalize(target_ - eye_);
        const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up_, forward));
        const XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

        // Scale pan by distance so it feels consistent.
        const float scaled = distance_ * panSpeed;
        const XMVECTOR panOffset = (right * (-deltaX * scaled)) + (up * (deltaY * scaled));

        target_ += panOffset;
        eye_ += panOffset;
    }

    // Recompute eye from orbit state if orbiting or zooming (perspective/orbit).
    if (orbit)
    {
        const float cp = cosf(pitch_);
        const float sp = sinf(pitch_);
        const float cy = cosf(yaw_);
        const float sy = sinf(yaw_);

        // LH: Y up, Z forward.
        const XMVECTOR offset = XMVectorSet(cp * sy, sp, cp * cy, 0.0f) * distance_;
        eye_ = target_ - offset;
    }
}
