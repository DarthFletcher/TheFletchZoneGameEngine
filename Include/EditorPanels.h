#pragma once

#include <functional>
#include <string>

struct EditorPanel
{
    const char* name;
    bool open;
    std::function<void()> draw;
};

namespace EditorPanels
{
    EditorPanel& Scene();
    EditorPanel& Game();
    EditorPanel& Hierarchy();
    EditorPanel& Inspector();
    EditorPanel& Assets();
    EditorPanel& DebugOverlay();

    EditorPanel& Diagnostics();
    EditorPanel& LogViewer();
    EditorPanel& Instancing();
    EditorPanel& MaterialPreview();
    EditorPanel& BlackFlame();
    EditorPanel& PromptHelper();
    EditorPanel& PrefabWorkflow();

    void DrawAll();
    void DrawPrefabOptionsMenu();
    void RevealAssetInBrowser(const std::string& assetId);
    bool ExecuteUndoCommand();
    bool ExecuteRedoCommand();
    bool CanUndoCommand();
    bool CanRedoCommand();
    bool ExecuteFocusSelectionCommand();
    bool ExecuteRenameSelectionCommand();
    bool ExecuteCreateEmptyParentCommand();
    bool ExecuteSaveSelectionAsPrefabCommand();
    bool ExecuteUnpackPrefabCommand();
    bool CanUnpackPrefabSelection();
    bool ExecuteDuplicateSelectionCommand();
    bool ExecuteDeleteSelectionCommand();
}
