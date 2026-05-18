#pragma once

#include <windows.h>
#include "Graphics.h"
#include "Input.h"
#include "Timer.h"
#include "Game.h"
#include "Logger.h"
#include "HintMacros.h"
#include "EditorState.h"
#include "RuntimeWorld.h"

static constexpr int NUM_BACK_BUFFERS = 3;

class Engine {
public:
    enum class State
    {
        Editing,
        Playing,
        Paused
    };

    static bool IsMouseCapturedByUI();
    static bool IsKeyboardCapturedByUI();

    static State GetState();
    static void SetState(State s);
    static bool StartPlayMode();
    static bool TogglePauseMode();
    static bool StopPlayMode();

    static void NewScene();
    static void SaveScene();
    static void LoadScene();

    bool Initialize(HINSTANCE hInstance, int nCmdShow); // ✅ Initialize engine with instance and show command
    void Run();                                         // ✅ Main game loop
    void Shutdown();                                    // ✅ Clean up resources

    EditorState& GetEditorState() { return editorState; }
    const EditorState& GetEditorState() const { return editorState; }
    Input& GetInput() { return input; }
    const Input& GetInput() const { return input; }
    Game& GetGame() { return game; }
    const Game& GetGame() const { return game; }
    RuntimeWorld& GetRuntimeWorld() { return runtimeWorld; }
    const RuntimeWorld& GetRuntimeWorld() const { return runtimeWorld; }

private:
    // Initialization
    bool InitializeWindow(HINSTANCE hInstance, int nCmdShow); // ✅ Setup window
    void RegisterWindowClass(HINSTANCE hInstance);            // ✅ Register window class

    // Main Loop Helpers
    void ProcessInput();          // ✅ Poll input
    void Update();                // ✅ Update game logic
    void Render(HWND hWnd);       // ✅ Render frame

    // Message Handling
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam); // ✅ Window message handler

    // Engine Components
    Graphics& graphics = Graphics::GetInstance();
    Input input;
    Timer timer;
    Game game;
    RuntimeWorld runtimeWorld;

    EditorState editorState;

    void GameLoop(HWND hWnd);

    // Window Handles and States
    HWND hWnd = nullptr;       // ✅ Main application window handle
    bool isRunning = true;     // ✅ Engine loop control flag
};
