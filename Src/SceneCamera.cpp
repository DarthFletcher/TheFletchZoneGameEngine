#include "SceneCamera.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    static XMVECTOR NormalizeOrFallback(XMVECTOR v, XMVECTOR fallback)
    {
        if (XMVectorGetX(XMVector3LengthSq(v)) < 1e-6f)
            return fallback;
        return XMVector3Normalize(v);
    }

    static void SyncYawPitchFromView(XMVECTOR eye, XMVECTOR target, float& yaw, float& pitch)
    {
        const XMVECTOR forward = NormalizeOrFallback(target - eye, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const float fx = XMVectorGetX(forward);
        const float fy = XMVectorGetY(forward);
        const float fz = XMVectorGetZ(forward);
        yaw = std::atan2(fx, fz);
        pitch = std::asin((std::clamp)(fy, -1.0f, 1.0f));
    }

    static void BuildBasis(float yaw, float pitch, XMVECTOR& outForward, XMVECTOR& outRight, XMVECTOR& outUp)
    {
        const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        outForward = XMVector3Normalize(XMVectorSet(
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw),
            0.0f));
        outRight = NormalizeOrFallback(XMVector3Cross(worldUp, outForward), XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
        outUp = NormalizeOrFallback(XMVector3Cross(outForward, outRight), worldUp);
    }
}

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

DirectX::XMFLOAT3 SceneCamera::GetPosition() const
{
    XMFLOAT3 p{};
    XMStoreFloat3(&p, eye_);
    return p;
}

DirectX::XMFLOAT3 SceneCamera::GetForward() const
{
    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);
    XMFLOAT3 f{};
    XMStoreFloat3(&f, forward);
    return f;
}

DirectX::XMFLOAT3 SceneCamera::GetRight() const
{
    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);
    XMFLOAT3 r{};
    XMStoreFloat3(&r, right);
    return r;
}

DirectX::XMFLOAT3 SceneCamera::GetUp() const
{
    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);
    XMFLOAT3 u{};
    XMStoreFloat3(&u, up);
    return u;
}

void SceneCamera::FocusOn(const XMFLOAT3& position, float distance)
{
    distance_ = (std::clamp)(distance, 0.25f, 500.0f);

    const XMVECTOR newTarget = XMVectorSet(position.x, position.y, position.z, 1.0f);
    XMVECTOR forward = NormalizeOrFallback(target_ - eye_, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

    target_ = newTarget;
    eye_ = target_ - forward * distance_;
    SyncYawPitchFromView(eye_, target_, yaw_, pitch_);
}

void SceneCamera::UpdateEditorNavigation(float deltaTime, bool allowInput)
{
    if (!allowInput)
    {
        freelooking_ = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!rmbDown)
    {
        freelooking_ = false;
    }
    else if (!freelooking_)
    {
        SyncYawPitchFromView(eye_, target_, yaw_, pitch_);
        freelooking_ = true;
    }

    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);

    const float wheel = io.MouseWheel;
    if (wheel != 0.0f)
    {
        const float wheelZoomSpeed = 2.0f;
        const XMVECTOR zoomOffset = forward * (wheel * wheelZoomSpeed);
        eye_ += zoomOffset;
        target_ += zoomOffset;
    }

    if (!rmbDown)
        return;

    const float lookSpeed = 0.0035f;
    yaw_ += io.MouseDelta.x * lookSpeed;
    pitch_ -= io.MouseDelta.y * lookSpeed;

    const float pitchLimit = XM_PIDIV2 - 0.05f;
    pitch_ = (std::clamp)(pitch_, -pitchLimit, pitchLimit);

    BuildBasis(yaw_, pitch_, forward, right, up);

    float speed = 8.0f * (std::max)(deltaTime, 0.0001f);
    if (io.KeyShift)
        speed *= 2.5f;
    if (io.KeyAlt)
        speed *= 0.35f;

    XMVECTOR move = XMVectorZero();
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up * speed;
    if (ImGui::IsKeyDown(ImGuiKey_E)) move += up * speed;

    eye_ += move;
    target_ = eye_ + forward * distance_;
    up_ = up;
}

void SceneCamera::UpdateOrbit(float deltaX, float deltaY, float wheelDelta, bool orbit, bool pan, bool precision)
{
    const float orbitSpeed = precision ? 0.0035f : 0.01f;
    const float panSpeed = precision ? 0.0035f : 0.01f;

    if (orbit)
    {
        yaw_ += deltaX * orbitSpeed;
        pitch_ += deltaY * orbitSpeed;
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
