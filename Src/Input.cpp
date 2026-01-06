#include "Input.h"
#include <iostream>
#include <algorithm>

Input::Input()
    : hWnd(nullptr), mouseX(0), mouseY(0), mouseWheelDelta(0), gamepadDeadzone(7849) {
    ZeroMemory(keys, sizeof(keys));
    ZeroMemory(prevKeys, sizeof(prevKeys));
    ZeroMemory(mouseButtons, sizeof(mouseButtons));
    gamepadConnected.fill(false);
    gamepadStates.fill({});
    rumbleActive.fill(false);
}

bool Input::Initialize(HWND hWnd) {
    if (!hWnd) {
        std::cerr << "❌ ERROR: Invalid HWND in Input::Initialize." << std::endl;
        return false;
    }

    this->hWnd = hWnd;
    ShowCursor(TRUE);
    return true;
}

void Input::Update() {
    // ✅ Keyboard
    memcpy(prevKeys, keys, sizeof(keys));
    for (int i = 0; i < 256; ++i) {
        keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    // ✅ Mouse Position
    POINT point;
    GetCursorPos(&point);
    ScreenToClient(hWnd, &point);
    mouseX = point.x;
    mouseY = point.y;

    // ✅ Mouse Wheel Reset
    mouseWheelDelta = 0;

    // ✅ Gamepad Update
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(XINPUT_STATE));

        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            gamepadStates[i] = state;
            gamepadConnected[i] = true;
        }
        else {
            gamepadConnected[i] = false;
            StopGamepadVibration(i); // Stop rumble if disconnected
        }
    }
}

void Input::Shutdown() {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        StopGamepadVibration(i); // Stop rumble on shutdown
    }
    ShowCursor(TRUE);
}

// ======================
// ✅ Keyboard Handling
// ======================

bool Input::IsKeyPressed(unsigned char key) const {
    return keys[key];
}

bool Input::IsKeyJustPressed(unsigned char key) const {
    return keys[key] && !prevKeys[key];
}

bool Input::IsKeyJustReleased(unsigned char key) const {
    return !keys[key] && prevKeys[key];
}

// ======================
// ✅ Mouse Handling
// ======================

void Input::GetMousePosition(int& x, int& y) const {
    x = mouseX;
    y = mouseY;
}

int Input::GetMouseWheelDelta() const {
    return mouseWheelDelta;
}

bool Input::IsMouseButtonDown(int button) const {
    return (GetAsyncKeyState(button) & 0x8000) != 0;
}

bool Input::IsMouseButtonJustPressed(int button) const {
    return IsMouseButtonDown(button) && !mouseButtons[button];
}

bool Input::IsMouseButtonJustReleased(int button) const {
    return !IsMouseButtonDown(button) && mouseButtons[button];
}

// ======================
// ✅ Gamepad Handling
// ======================

bool Input::IsGamepadConnected(int controllerId) const {
    if (controllerId < 0 || controllerId >= XUSER_MAX_COUNT) return false;
    return gamepadConnected[controllerId];
}

bool Input::IsGamepadButtonPressed(int controllerId, WORD button) const {
    if (!IsGamepadConnected(controllerId)) return false;
    return (gamepadStates[controllerId].Gamepad.wButtons & button) != 0;
}

float Input::GetGamepadLeftStickX(int controllerId) const {
    return NormalizeStickValue(gamepadStates[controllerId].Gamepad.sThumbLX, gamepadDeadzone);
}

float Input::GetGamepadLeftStickY(int controllerId) const {
    return NormalizeStickValue(gamepadStates[controllerId].Gamepad.sThumbLY, gamepadDeadzone);
}

float Input::GetGamepadRightStickX(int controllerId) const {
    return NormalizeStickValue(gamepadStates[controllerId].Gamepad.sThumbRX, gamepadDeadzone);
}

float Input::GetGamepadRightStickY(int controllerId) const {
    return NormalizeStickValue(gamepadStates[controllerId].Gamepad.sThumbRY, gamepadDeadzone);
}

float Input::GetGamepadLeftTrigger(int controllerId) const {
    return gamepadStates[controllerId].Gamepad.bLeftTrigger / 255.0f;
}

float Input::GetGamepadRightTrigger(int controllerId) const {
    return gamepadStates[controllerId].Gamepad.bRightTrigger / 255.0f;
}

float Input::NormalizeStickValue(SHORT value, SHORT deadzone) const {
    if (abs(value) < deadzone) return 0.0f;
    return static_cast<float>(value) / 32767.0f;
}

// ======================
// ✅ Controller Rumble
// ======================

void Input::SetGamepadVibration(int controllerId, float leftMotor, float rightMotor) {
    if (!IsGamepadConnected(controllerId)) return;

    XINPUT_VIBRATION vibration;
    ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

    vibration.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);

    XInputSetState(controllerId, &vibration);
    rumbleActive[controllerId] = true;
}

void Input::StopGamepadVibration(int controllerId) {
    if (!IsGamepadConnected(controllerId) || !rumbleActive[controllerId]) return;

    XINPUT_VIBRATION vibration = { 0 };
    XInputSetState(controllerId, &vibration);
    rumbleActive[controllerId] = false;
}

// ======================
// ✅ Deadzone Adjustment
// ======================

void Input::SetGamepadDeadzone(int deadzone) {
    gamepadDeadzone = std::clamp(deadzone, 0, 32767);
}

int Input::GetGamepadDeadzone() const {
    return gamepadDeadzone;
}

// ======================
// ✅ Touch Input
// ======================

bool Input::IsTouchActive() const {
    return touchActive;
}

void Input::GetTouchPosition(int& x, int& y) const {
    x = touchX;
    y = touchY;
}

// ======================
// ✅ Windows Message Handling
// ======================

LRESULT Input::ProcessWindowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEWHEEL:
        mouseWheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        break;
    case WM_TOUCH: {
        UINT cInputs = LOWORD(wParam);
        TOUCHINPUT* pInputs = new TOUCHINPUT[cInputs];
        if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs, sizeof(TOUCHINPUT))) {
            touchX = pInputs[0].x / 100; // Scaling touch position
            touchY = pInputs[0].y / 100;
            touchActive = true;
        }
        delete[] pInputs;
        CloseTouchInputHandle((HTOUCHINPUT)lParam);
        break;
    }
    case WM_KILLFOCUS:
        ZeroMemory(keys, sizeof(keys));
        touchActive = false;
        break;
    default:
        break;
    }
    return 0;
}
