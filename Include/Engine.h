#pragma once

#include <windows.h>
#include "Graphics.h"
#include "Input.h"
#include "Timer.h"
#include "Game.h"
#include "Logger.h"
#include "HintMacros.h"
#include "EditorState.h"

static constexpr int NUM_BACK_BUFFERS = 3;

class Engine {
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow); // ✅ Initialize engine with instance and show command
    void Run();                                         // ✅ Main game loop
    void Shutdown();                                    // ✅ Clean up resources

    EditorState& GetEditorState() { return editorState; }
    const EditorState& GetEditorState() const { return editorState; }

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

    EditorState editorState;

    void GameLoop(HWND hWnd);

    // Window Handles and States
    HWND hWnd = nullptr;       // ✅ Main application window handle
    bool isRunning = true;     // ✅ Engine loop control flag
};
