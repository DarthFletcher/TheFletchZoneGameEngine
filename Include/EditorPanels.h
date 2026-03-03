#pragma once

#include <functional>

struct EditorPanel
{
    const char* name;
    bool open;
    std::function<void()> draw;
};

namespace EditorPanels
{
    EditorPanel& Scene();
    EditorPanel& Hierarchy();
    EditorPanel& Inspector();
    EditorPanel& Assets();
    EditorPanel& DebugOverlay();

    EditorPanel& Diagnostics();
    EditorPanel& LogViewer();
    EditorPanel& Instancing();

    void DrawAll();
}
