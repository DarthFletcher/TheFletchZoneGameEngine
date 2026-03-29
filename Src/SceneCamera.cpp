#include "SceneCamera.h"
#include "Engine.h"
#include "Scene.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

extern Engine* g_engineInstance;

using namespace DirectX;

namespace
{
    static XMVECTOR NormalizeOrFallback(XMVECTOR v, XMVECTOR fallback)
    {
        if (XMVectorGetX(XMVector3LengthSq(v)) < 1e-6f)
            return fallback;
        return XMVector3Normalize(v);
    }

    static void SyncYawPitchFromView(XMVECTOR eye, XMVECTOR target, float& yaw, float& pitch);

    static bool TryGetPrimaryGamepad(const Input*& outInput, int& outControllerId)
    {
        if (!g_engineInstance)
            return false;
        if (!g_engineInstance->GetEditorState().enableGamepadCamera)
            return false;

        const Input& input = g_engineInstance->GetInput();
        for (int controllerId = 0; controllerId < XUSER_MAX_COUNT; ++controllerId)
        {
            if (input.IsGamepadConnected(controllerId))
            {
                outInput = &input;
                outControllerId = controllerId;
                return true;
            }
        }

        return false;
    }

    static void ApplyLookAtState(XMVECTOR eye, XMVECTOR target, XMVECTOR up, XMVECTOR& ioEye, XMVECTOR& ioTarget, XMVECTOR& ioUp, XMVECTOR& ioOrbitPivot, float& ioYaw, float& ioPitch, float& ioDistance)
    {
        ioEye = eye;
        ioTarget = target;
        ioUp = NormalizeOrFallback(up, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        ioOrbitPivot = target;
        ioDistance = (std::max)(0.25f, XMVectorGetX(XMVector3Length(target - eye)));
        SyncYawPitchFromView(ioEye, ioTarget, ioYaw, ioPitch);
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

SceneCamera::SceneCamera()
{
    ResetToDefaultView();
}

void SceneCamera::ResetToDefaultView()
{
    projectionMode_ = ProjectionMode::Perspective;
    viewPreset_ = ViewPreset::View3D;
    const XMVECTOR target = XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f);
    orbitPivot_ = target;
    yaw_ = XM_PIDIV4;
    pitch_ = XMConvertToRadians(-25.0f);
    distance_ = 10.0f;

    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);
    up_ = up;
    target_ = target;
    eye_ = target_ - forward * distance_;
    freelooking_ = false;
    orbitInteracting_ = false;
}

void SceneCamera::SetFrontView()
{
    projectionMode_ = ProjectionMode::Orthographic;
    viewPreset_ = ViewPreset::View3D;
    const XMVECTOR target = orbitPivot_;
    const XMVECTOR eye = target + XMVectorSet(0.0f, 0.0f, -distance_, 0.0f);
    ApplyLookAtState(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), eye_, target_, up_, orbitPivot_, yaw_, pitch_, distance_);
    freelooking_ = false;
    orbitInteracting_ = false;
}

void SceneCamera::SetRightView()
{
    projectionMode_ = ProjectionMode::Orthographic;
    viewPreset_ = ViewPreset::View3D;
    const XMVECTOR target = orbitPivot_;
    const XMVECTOR eye = target + XMVectorSet(distance_, 0.0f, 0.0f, 0.0f);
    ApplyLookAtState(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), eye_, target_, up_, orbitPivot_, yaw_, pitch_, distance_);
    freelooking_ = false;
    orbitInteracting_ = false;
}

void SceneCamera::SetTopView()
{
    projectionMode_ = ProjectionMode::Orthographic;
    viewPreset_ = ViewPreset::View3D;
    const XMVECTOR target = orbitPivot_;
    const XMVECTOR eye = target + XMVectorSet(0.0f, distance_, 0.0f, 0.0f);
    ApplyLookAtState(eye, target, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), eye_, target_, up_, orbitPivot_, yaw_, pitch_, distance_);
    freelooking_ = false;
    orbitInteracting_ = false;
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

CameraData SceneCamera::GetCameraData(float aspect, bool ortho) const
{
    CameraData data{};
    data.position = GetPosition();
    data.fovY = fovY_;
    data.aspect = (aspect > 0.0f) ? aspect : 1.0f;
    data.nearZ = nearZ_;
    data.farZ = farZ_;

    const XMMATRIX view = GetView();
    const XMMATRIX proj = ortho ? GetOrthoProj(data.aspect) : GetProj(data.aspect);

    XMStoreFloat4x4(&data.view, view);
    XMStoreFloat4x4(&data.proj, proj);
    return data;
}

CameraData SceneCamera::GetCameraData(float aspect) const
{
    return GetCameraData(aspect, projectionMode_ == ProjectionMode::Orthographic);
}

void SceneCamera::SetViewPreset(ViewPreset preset)
{
    if (viewPreset_ == preset)
        return;

    viewPreset_ = preset;
    freelooking_ = false;
    orbitInteracting_ = false;

    if (viewPreset_ == ViewPreset::View2D)
    {
        projectionMode_ = ProjectionMode::Orthographic;
        pitch_ = 0.0f;
        yaw_ = 0.0f;
        up_ = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        XMFLOAT3 targetPos{};
        XMStoreFloat3(&targetPos, orbitPivot_);
        targetPos.z = 0.0f;
        orbitPivot_ = XMVectorSet(targetPos.x, targetPos.y, targetPos.z, 1.0f);
        target_ = orbitPivot_;
        distance_ = (std::max)(distance_, 10.0f);
        eye_ = orbitPivot_ - XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) * distance_;
    }
    else
    {
        projectionMode_ = ProjectionMode::Perspective;
        if (fabsf(pitch_) < 1e-4f)
            pitch_ = -0.35f;
        if (distance_ < 0.25f)
            distance_ = 5.0f;

        XMVECTOR forward{}, right{}, up{};
        BuildBasis(yaw_, pitch_, forward, right, up);
        up_ = up;
        target_ = orbitPivot_;
        eye_ = target_ - forward * distance_;
    }
}

void SceneCamera::SetNavigationTuning(bool invertX, bool invertY, bool smoothLook, float lookSmoothing, float flyLookSpeed, float orbitLookSpeed, float flyMoveSpeed)
{
    invertLookX_ = invertX;
    invertLookY_ = invertY;
    smoothLook_ = smoothLook;
    lookSmoothing_ = (std::clamp)(lookSmoothing, 0.0f, 0.95f);
    flyLookSpeed_ = (std::max)(flyLookSpeed, 0.0001f);
    orbitLookSpeed_ = (std::max)(orbitLookSpeed, 0.0001f);
    flyMoveSpeed_ = (std::max)(flyMoveSpeed, 0.1f);
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

    orbitPivot_ = XMVectorSet(position.x, position.y, position.z, 1.0f);
    XMVECTOR forward = NormalizeOrFallback(target_ - eye_, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

    target_ = orbitPivot_;
    eye_ = target_ - forward * distance_;
    SyncYawPitchFromView(eye_, target_, yaw_, pitch_);
}

void SceneCamera::UpdateEditorNavigation(float deltaTime, bool allowInput)
{
    UpdateEditorNavigation(deltaTime, allowInput, CameraNavMode::TFZ_RMB);
}

void SceneCamera::UpdateEditorNavigation(float deltaTime, bool allowInput, CameraNavMode navMode)
{
    if (!allowInput)
    {
        freelooking_ = false;
        orbitInteracting_ = false;
        gamepadFocusPressed_ = false;
        smoothedLookDelta_ = { 0.0f, 0.0f };
        controlTickAccumulator_ = 0.0f;
        pendingMouseDelta_ = { 0.0f, 0.0f };
        pendingMouseWheel_ = 0.0f;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    pendingMouseDelta_.x += io.MouseDelta.x;
    pendingMouseDelta_.y += io.MouseDelta.y;
    pendingMouseWheel_ += io.MouseWheel;

    const float clampedDeltaTime = (std::clamp)(deltaTime, 0.0f, 0.1f);
    controlTickAccumulator_ = (std::min)(controlTickAccumulator_ + clampedDeltaTime, kEditorControlTickSeconds * 4.0f);
    if (controlTickAccumulator_ < kEditorControlTickSeconds)
        return;

    const float tickDeltaTime = controlTickAccumulator_;
    const float tickMouseDeltaX = pendingMouseDelta_.x;
    const float tickMouseDeltaY = pendingMouseDelta_.y;
    const float tickMouseWheel = pendingMouseWheel_;
    controlTickAccumulator_ = 0.0f;
    pendingMouseDelta_ = { 0.0f, 0.0f };
    pendingMouseWheel_ = 0.0f;

    const Input* gamepadInput = nullptr;
    int controllerId = -1;
    const bool hasGamepad = TryGetPrimaryGamepad(gamepadInput, controllerId);
    const float gamepadLeftX = hasGamepad ? gamepadInput->GetGamepadLeftStickX(controllerId) : 0.0f;
    const float gamepadLeftY = hasGamepad ? gamepadInput->GetGamepadLeftStickY(controllerId) : 0.0f;
    const float gamepadRightX = hasGamepad ? gamepadInput->GetGamepadRightStickX(controllerId) : 0.0f;
    const float gamepadRightY = hasGamepad ? gamepadInput->GetGamepadRightStickY(controllerId) : 0.0f;
    const float gamepadZoom = hasGamepad ? (gamepadInput->GetGamepadRightTrigger(controllerId) - gamepadInput->GetGamepadLeftTrigger(controllerId)) : 0.0f;
    const bool gamepadSlow = hasGamepad && gamepadInput->IsGamepadButtonPressed(controllerId, XINPUT_GAMEPAD_LEFT_SHOULDER);
    const bool gamepadFast = hasGamepad && gamepadInput->IsGamepadButtonPressed(controllerId, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    const bool gamepadFocus = hasGamepad && gamepadInput->IsGamepadButtonPressed(controllerId, XINPUT_GAMEPAD_A);
    const bool gamepadLookActive = fabsf(gamepadRightX) > 0.001f || fabsf(gamepadRightY) > 0.001f;
    const bool gamepadMoveActive = fabsf(gamepadLeftX) > 0.001f || fabsf(gamepadLeftY) > 0.001f;
    const bool gamepadZoomActive = fabsf(gamepadZoom) > 0.001f;

    if (gamepadFocus && !gamepadFocusPressed_)
    {
        DirectX::XMFLOAT3 focusPoint{};
        if (Scene::TryGetSelectionCenter(focusPoint))
            FocusOnPoint(focusPoint, (std::max)(distance_, 5.0f));
    }
    gamepadFocusPressed_ = gamepadFocus;

    if (viewPreset_ == ViewPreset::View2D)
    {
        freelooking_ = false;
        orbitInteracting_ = false;
        smoothedLookDelta_ = { 0.0f, 0.0f };

        if (tickMouseWheel != 0.0f || gamepadZoomActive)
        {
            const float zoomInput = tickMouseWheel + gamepadZoom * (tickDeltaTime * 6.0f);
            const float zoomSpeed = (io.KeyShift || gamepadFast) ? 0.08f : 0.12f;
            orthoHeight_ *= (1.0f - zoomInput * zoomSpeed);
            orthoHeight_ = (std::clamp)(orthoHeight_, 0.05f, 5000.0f);
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || gamepadMoveActive)
        {
            float panScale = orthoHeight_ * (io.KeyAlt ? 0.0012f : 0.0025f);
            if (gamepadFast)
                panScale *= 2.0f;
            if (gamepadSlow)
                panScale *= 0.35f;

            const float panDeltaX = tickMouseDeltaX + gamepadLeftX * (220.0f * tickDeltaTime);
            const float panDeltaY = tickMouseDeltaY + (-gamepadLeftY * (220.0f * tickDeltaTime));
            const XMVECTOR panOffset = XMVectorSet(-panDeltaX * panScale, panDeltaY * panScale, 0.0f, 0.0f);
            eye_ += panOffset;
            orbitPivot_ += panOffset;
            target_ = orbitPivot_;
        }

        return;
    }

    const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool alt = io.KeyAlt;
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const bool orbitMode = (navMode == CameraNavMode::Unity_AltMouse || navMode == CameraNavMode::Blender_MMB || navMode == CameraNavMode::Laptop_Friendly);

    bool orbit = false;
    bool pan = false;
    bool dolly = false;
    bool fly = false;

    switch (navMode)
    {
    case CameraNavMode::Unity_AltMouse:
        orbit = alt && lmbDown;
        pan = alt && mmbDown;
        dolly = alt && rmbDown;
        break;

    case CameraNavMode::Blender_MMB:
        orbit = mmbDown && !shift && !ctrl;
        pan = mmbDown && shift;
        dolly = mmbDown && ctrl;
        break;

    case CameraNavMode::Laptop_Friendly:
        orbit = alt && lmbDown;
        pan = shift && lmbDown;
        dolly = alt && rmbDown;
        break;

    case CameraNavMode::TFZ_RMB:
    default:
        fly = rmbDown && !ctrl;
        pan = mmbDown;
        dolly = rmbDown && ctrl;
        break;
    }

    const bool controllerOrbit = orbitMode && gamepadLookActive;
    const bool controllerPan = orbitMode && gamepadMoveActive;
    const bool controllerFly = !orbitMode && (gamepadLookActive || gamepadMoveActive || gamepadZoomActive);
    orbit = orbit || controllerOrbit;
    pan = pan || controllerPan;
    dolly = dolly || (orbitMode && gamepadZoomActive);
    fly = fly || controllerFly;

    const bool orbitInteraction = orbitMode && (orbit || pan || dolly);
    const bool lookActive = fly || orbit;
    if (!lookActive)
    {
        freelooking_ = false;
        smoothedLookDelta_ = { 0.0f, 0.0f };
    }
    else if (fly && !freelooking_)
    {
        SyncYawPitchFromView(eye_, target_, yaw_, pitch_);
        freelooking_ = true;
    }

    if (!orbitInteraction)
    {
        orbitInteracting_ = false;
    }
    else if (!orbitInteracting_)
    {
        orbitPivot_ = target_;
        distance_ = (std::max)(0.25f, XMVectorGetX(XMVector3Length(target_ - eye_)));
        SyncYawPitchFromView(eye_, target_, yaw_, pitch_);
        orbitInteracting_ = true;
    }

    float lookDeltaX = tickMouseDeltaX + gamepadRightX * (220.0f * tickDeltaTime);
    float lookDeltaY = tickMouseDeltaY + (-gamepadRightY * (220.0f * tickDeltaTime));
    if (smoothLook_ && lookActive)
    {
        const float rawAlpha = 1.0f - lookSmoothing_;
        smoothedLookDelta_.x = smoothedLookDelta_.x * lookSmoothing_ + lookDeltaX * rawAlpha;
        smoothedLookDelta_.y = smoothedLookDelta_.y * lookSmoothing_ + lookDeltaY * rawAlpha;
        lookDeltaX = smoothedLookDelta_.x;
        lookDeltaY = smoothedLookDelta_.y;
    }

    if (invertLookX_)
        lookDeltaX = -lookDeltaX;
    if (invertLookY_)
        lookDeltaY = -lookDeltaY;

    if (fabsf(lookDeltaX) < 0.001f) lookDeltaX = 0.0f;
    if (fabsf(lookDeltaY) < 0.001f) lookDeltaY = 0.0f;

    XMVECTOR forward{}, right{}, up{};
    BuildBasis(yaw_, pitch_, forward, right, up);
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    if (tickMouseWheel != 0.0f || (gamepadZoomActive && !orbitMode))
    {
        if (projectionMode_ == ProjectionMode::Orthographic)
        {
            const float zoomInput = tickMouseWheel + gamepadZoom * (tickDeltaTime * 6.0f);
            const float zoomSpeed = (shift || gamepadFast) ? 0.08f : 0.12f;
            orthoHeight_ *= (1.0f - zoomInput * zoomSpeed);
            orthoHeight_ = (std::clamp)(orthoHeight_, 0.05f, 5000.0f);
        }
        else if (orbitMode)
        {
            const float zoomInput = tickMouseWheel;
            if (zoomInput != 0.0f)
            {
                const float zoomFactor = 1.0f - zoomInput * (shift ? 0.06f : 0.10f);
                distance_ *= zoomFactor;
                distance_ = (std::clamp)(distance_, 0.25f, 500.0f);
                eye_ = orbitPivot_ - forward * distance_;
                target_ = orbitPivot_;
            }
        }
        else if (!dolly)
        {
            float wheelZoomSpeed = (shift || gamepadFast) ? 1.0f : 2.0f;
            if (gamepadSlow)
                wheelZoomSpeed *= 0.35f;
            const XMVECTOR zoomOffset = forward * ((tickMouseWheel + gamepadZoom * (tickDeltaTime * 3.0f)) * wheelZoomSpeed);
            eye_ += zoomOffset;
            target_ += zoomOffset;
        }
    }

    if (dolly)
    {
        if (orbitMode)
        {
            const float zoomInput = tickMouseDeltaY - gamepadZoom * (tickDeltaTime * 120.0f);
            const float zoomFactor = 1.0f + zoomInput * ((shift || gamepadFast) ? 0.01f : 0.02f);
            distance_ *= zoomFactor;
            distance_ = (std::clamp)(distance_, 0.25f, 500.0f);
            eye_ = orbitPivot_ - forward * distance_;
            target_ = orbitPivot_;
        }
        else
        {
            float dollySpeed = ((shift || gamepadFast) ? 0.02f : 0.035f) * (std::max)(distance_, 0.25f);
            if (gamepadSlow)
                dollySpeed *= 0.35f;
            const XMVECTOR dollyOffset = forward * ((-tickMouseDeltaY + gamepadZoom * (tickDeltaTime * 120.0f)) * dollySpeed);
            eye_ += dollyOffset;
            target_ += dollyOffset;
        }
    }

    if (orbit)
    {
        yaw_ += lookDeltaX * orbitLookSpeed_;
        pitch_ -= lookDeltaY * orbitLookSpeed_;
        const float pitchLimit = XM_PIDIV2 - 0.05f;
        pitch_ = (std::clamp)(pitch_, -pitchLimit, pitchLimit);

        BuildBasis(yaw_, pitch_, forward, right, up);
        eye_ = orbitPivot_ - forward * distance_;
        target_ = orbitPivot_;
        up_ = up;
    }

    if (pan)
    {
        BuildBasis(yaw_, pitch_, forward, right, up);
        float panScale = (projectionMode_ == ProjectionMode::Orthographic)
            ? orthoHeight_ * (shift ? 0.0012f : 0.0025f)
            : distance_ * (shift ? 0.0035f : 0.0060f);
        if (gamepadFast)
            panScale *= 2.0f;
        if (gamepadSlow)
            panScale *= 0.35f;
        const float panDeltaX = tickMouseDeltaX + gamepadLeftX * (220.0f * tickDeltaTime);
        const float panDeltaY = tickMouseDeltaY + (-gamepadLeftY * (220.0f * tickDeltaTime));
        const XMVECTOR panOffset = (right * (-panDeltaX * panScale)) + (up * (panDeltaY * panScale));
        eye_ += panOffset;
        if (orbitMode)
        {
            orbitPivot_ += panOffset;
            target_ = orbitPivot_;
        }
        else
        {
            target_ += panOffset;
        }
    }

    if (fly)
    {
        yaw_ += lookDeltaX * flyLookSpeed_;
        pitch_ -= lookDeltaY * flyLookSpeed_;
        const float pitchLimit = XM_PIDIV2 - 0.05f;
        pitch_ = (std::clamp)(pitch_, -pitchLimit, pitchLimit);

        BuildBasis(yaw_, pitch_, forward, right, up);

        float speed = flyMoveSpeed_ * (std::max)(tickDeltaTime, 0.0001f);
        if (shift || gamepadFast)
            speed *= 2.0f;
        if (alt || gamepadSlow)
            speed *= 0.35f;

        XMVECTOR move = XMVectorZero();
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= worldUp * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move += worldUp * speed;
        move += forward * (gamepadLeftY * speed);
        move += right * (gamepadLeftX * speed);

        eye_ += move;
        const float flyTargetDistance = (std::max)(distance_, 5.0f);
        target_ = eye_ + forward * flyTargetDistance;
        distance_ = flyTargetDistance;
        up_ = up;
    }

    if (!fly && !orbit && !orbitMode)
    {
        const XMVECTOR viewDir = NormalizeOrFallback(target_ - eye_, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        distance_ = (std::max)(0.25f, XMVectorGetX(XMVector3Length(target_ - eye_)));
        target_ = eye_ + viewDir * distance_;
    }
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
