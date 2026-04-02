#include "Game.h"

#include "Engine.h"
#include "Graphics.h"
#include "Scene.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

extern Engine* g_engineInstance;

bool Game::Initialize() {
    return true;
}

void Game::Update(float deltaTime) {
    if (!g_engineInstance || Engine::GetState() != Engine::State::Playing)
        return;

    Graphics& graphics = Graphics::GetInstance();
    if (!graphics.IsGameViewportInputAllowed())
        return;

    SceneInstance* mainCamera = Scene::GetMainCameraInstanceMutable();
    if (!mainCamera || !mainCamera->camera.enabled)
        return;

    const bool lookActive = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (lookActive)
    {
        ImGuiIO& io = ImGui::GetIO();
        constexpr float kMouseLookSensitivity = 0.0025f;
        mainCamera->rotation.y += io.MouseDelta.x * kMouseLookSensitivity;
        mainCamera->rotation.x -= io.MouseDelta.y * kMouseLookSensitivity;
        mainCamera->rotation.x = (std::clamp)(mainCamera->rotation.x, DirectX::XMConvertToRadians(-89.0f), DirectX::XMConvertToRadians(89.0f));
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool moveForward = ImGui::IsKeyDown(ImGuiKey_W) || g_engineInstance->GetInput().IsKeyPressed('W');
    const bool moveBackward = ImGui::IsKeyDown(ImGuiKey_S) || g_engineInstance->GetInput().IsKeyPressed('S');
    const bool moveLeft = ImGui::IsKeyDown(ImGuiKey_A) || g_engineInstance->GetInput().IsKeyPressed('A');
    const bool moveRight = ImGui::IsKeyDown(ImGuiKey_D) || g_engineInstance->GetInput().IsKeyPressed('D');
    const bool moveUp = ImGui::IsKeyDown(ImGuiKey_E) || g_engineInstance->GetInput().IsKeyPressed('E');
    const bool moveDown = ImGui::IsKeyDown(ImGuiKey_Q) || g_engineInstance->GetInput().IsKeyPressed('Q');
    const bool speedBoost = io.KeyShift || g_engineInstance->GetInput().IsKeyPressed(VK_SHIFT);

    float moveSpeed = 5.0f;
    if (speedBoost)
        moveSpeed *= 3.0f;

    const float yaw = mainCamera->rotation.y;
    const DirectX::XMFLOAT3 forward = {
        std::sinf(yaw),
        0.0f,
        std::cosf(yaw)
    };
    const DirectX::XMFLOAT3 right = {
        std::cosf(yaw),
        0.0f,
        -std::sinf(yaw)
    };

    DirectX::XMFLOAT3 moveDelta{ 0.0f, 0.0f, 0.0f };

    if (moveForward)
    {
        moveDelta.x += forward.x;
        moveDelta.y += forward.y;
        moveDelta.z += forward.z;
    }
    if (moveBackward)
    {
        moveDelta.x -= forward.x;
        moveDelta.y -= forward.y;
        moveDelta.z -= forward.z;
    }
    if (moveLeft)
    {
        moveDelta.x -= right.x;
        moveDelta.y -= right.y;
        moveDelta.z -= right.z;
    }
    if (moveRight)
    {
        moveDelta.x += right.x;
        moveDelta.y += right.y;
        moveDelta.z += right.z;
    }
    if (moveUp)
        moveDelta.y += 1.0f;
    if (moveDown)
        moveDelta.y -= 1.0f;

    if (moveDelta.x == 0.0f && moveDelta.y == 0.0f && moveDelta.z == 0.0f)
        return;

    DirectX::XMVECTOR move = DirectX::XMLoadFloat3(&moveDelta);
    move = DirectX::XMVector3Normalize(move);
    move = DirectX::XMVectorScale(move, moveSpeed * deltaTime);
    DirectX::XMStoreFloat3(&moveDelta, move);

    mainCamera->position.x += moveDelta.x;
    mainCamera->position.y += moveDelta.y;
    mainCamera->position.z += moveDelta.z;
}

void Game::Shutdown() {
}
