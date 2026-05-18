#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <array>

class Input {
public:
    Input();

    bool Initialize(HWND hWnd);
    void Update();
    void Shutdown();

    // ✅ Keyboard Input
    bool IsKeyPressed(unsigned char key) const;
    bool IsKeyJustPressed(unsigned char key) const;
    bool IsKeyJustReleased(unsigned char key) const;

    // ✅ Mouse Input
    void GetMousePosition(int& x, int& y) const;
    int GetMouseWheelDelta() const;
    bool IsMouseButtonDown(int button) const;
    bool IsMouseButtonJustPressed(int button) const;
    bool IsMouseButtonJustReleased(int button) const;

    // ✅ Gamepad Input (XInput)
    bool IsGamepadConnected(int controllerId) const;
    bool IsGamepadButtonPressed(int controllerId, WORD button) const;
    bool IsGamepadButtonJustPressed(int controllerId, WORD button) const;
    bool IsGamepadButtonJustReleased(int controllerId, WORD button) const;
    float GetGamepadLeftStickX(int controllerId) const;
    float GetGamepadLeftStickY(int controllerId) const;
    float GetGamepadRightStickX(int controllerId) const;
    float GetGamepadRightStickY(int controllerId) const;
    float GetGamepadLeftTrigger(int controllerId) const;
    float GetGamepadRightTrigger(int controllerId) const;

    // ✅ Controller Rumble
    void SetGamepadVibration(int controllerId, float leftMotor, float rightMotor);
    void StopGamepadVibration(int controllerId);

    // ✅ Deadzone Adjustment
    void SetGamepadDeadzone(int deadzone);
    int GetGamepadDeadzone() const;

    // ✅ Touch Input (Windows touch screens)
    bool IsTouchActive() const;
    void GetTouchPosition(int& x, int& y) const;

    // ✅ Windows Message Handling
    LRESULT ProcessWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND hWnd = nullptr;

    // Keyboard
    bool keys[256]{ false };
    bool prevKeys[256]{ false };

    // Mouse
    int mouseX = 0;
    int mouseY = 0;
    int mouseWheelDelta = 0;
    bool mouseButtons[5]{ false };

    // Gamepad
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> gamepadStates{};
    std::array<XINPUT_STATE, XUSER_MAX_COUNT> prevGamepadStates{};
    std::array<bool, XUSER_MAX_COUNT> gamepadConnected{ false };
    int gamepadDeadzone = 7849; // Default Deadzone

    // Rumble
    std::array<bool, XUSER_MAX_COUNT> rumbleActive{ false };

    // Touch
    bool touchActive = false;
    int touchX = 0;
    int touchY = 0;

    // Helper
    float NormalizeStickValue(SHORT value, SHORT deadzone) const;
};
