#pragma once

#include <DirectXMath.h>
#include "CameraData.h"
#include "EditorState.h"

class SceneCamera
{
public:
    enum class ProjectionMode
    {
        Perspective,
        Orthographic
    };

    enum class ViewPreset
    {
        View3D,
        View2D
    };

    SceneCamera();

    DirectX::XMMATRIX GetView() const;
    DirectX::XMMATRIX GetProj(float aspect) const;
    DirectX::XMMATRIX GetOrthoProj(float aspect) const;
    CameraData GetCameraData(float aspect, bool ortho) const;
    CameraData GetCameraData(float aspect) const;

    void SetEye(float x, float y, float z) { eye_ = DirectX::XMVectorSet(x, y, z, 1.0f); }
    void SetTarget(float x, float y, float z) { target_ = DirectX::XMVectorSet(x, y, z, 1.0f); }
    void SetUp(float x, float y, float z) { up_ = DirectX::XMVectorSet(x, y, z, 0.0f); }

    void SetFovYRadians(float fovY) { fovY_ = fovY; }
    void SetNearFar(float nearZ, float farZ) { nearZ_ = nearZ; farZ_ = farZ; }
    void SetOrthoHeight(float h) { orthoHeight_ = h; }

    void SetProjectionMode(ProjectionMode mode) { projectionMode_ = mode; }
    ProjectionMode GetProjectionMode() const { return projectionMode_; }
    void ToggleProjectionMode() { projectionMode_ = (projectionMode_ == ProjectionMode::Perspective) ? ProjectionMode::Orthographic : ProjectionMode::Perspective; }
    void SetNavigationTuning(bool invertX, bool invertY, bool smoothLook, float lookSmoothing, float flyLookSpeed, float orbitLookSpeed, float flyMoveSpeed,
        float gamepadStickDeadzone, float gamepadLookSensitivity, float gamepadMoveSensitivity, float gamepadZoomSensitivity);

    void SetViewPreset(ViewPreset preset);
    ViewPreset GetViewPreset() const { return viewPreset_; }

    void ResetToDefaultView();
    void SetFrontView();
    void SetRightView();
    void SetTopView();

    void FocusOn(const DirectX::XMFLOAT3& position, float distance = 5.0f);
    void FocusOnPoint(const DirectX::XMFLOAT3& position, float distance = 5.0f) { FocusOn(position, distance); }
    void UpdateEditorNavigation(float deltaTime, bool allowInput);
    void UpdateEditorNavigation(float deltaTime, bool allowInput, CameraNavMode navMode);

    // Orbit camera controls (Scene view)
    void UpdateOrbit(float deltaX, float deltaY, float wheelDelta, bool orbit, bool pan, bool precision);
    DirectX::XMVECTOR GetEye() const { return eye_; }

    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetDistance() const { return distance_; }
    DirectX::XMVECTOR GetTarget() const { return target_; }
    float GetOrthoHeight() const { return orthoHeight_; }
    bool IsFreelooking() const { return freelooking_; }
    DirectX::XMFLOAT3 GetPosition() const;
    DirectX::XMFLOAT3 GetForward() const;
    DirectX::XMFLOAT3 GetRight() const;
    DirectX::XMFLOAT3 GetUp() const;
    bool Is2DMode() const { return viewPreset_ == ViewPreset::View2D; }

private:
    // Backing vectors used for rendering.
    DirectX::XMVECTOR eye_ = DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 1.0f);
    DirectX::XMVECTOR target_ = DirectX::XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f);
    DirectX::XMVECTOR up_ = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMVECTOR orbitPivot_ = DirectX::XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f);

    float controlTickAccumulator_ = 0.0f;
    DirectX::XMFLOAT2 pendingMouseDelta_{ 0.0f, 0.0f };
    float pendingMouseWheel_ = 0.0f;

    // Orbit state
    float yaw_ = DirectX::XM_PIDIV4;
    float pitch_ = DirectX::XMConvertToRadians(-25.0f);
    float distance_ = 10.0f;
    bool freelooking_ = false;
    ProjectionMode projectionMode_ = ProjectionMode::Perspective;
    ViewPreset viewPreset_ = ViewPreset::View3D;
    bool invertLookX_ = false;
    bool invertLookY_ = false;
    bool smoothLook_ = true;
    float lookSmoothing_ = 0.22f;
    float flyLookSpeed_ = 0.0018f;
    float orbitLookSpeed_ = 0.0055f;
    float flyMoveSpeed_ = 8.0f;
    float gamepadStickDeadzone_ = 0.18f;
    float gamepadLookSensitivity_ = 1.0f;
    float gamepadMoveSensitivity_ = 1.0f;
    float gamepadZoomSensitivity_ = 1.0f;
    bool orbitInteracting_ = false;
    bool gamepadFocusPressed_ = false;
    DirectX::XMFLOAT2 smoothedLookDelta_{ 0.0f, 0.0f };

    float fovY_ = DirectX::XMConvertToRadians(60.0f);
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;
    float orthoHeight_ = 10.0f;
};
