#pragma once

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Scene.h"

class IEditorCommand
{
public:
    virtual ~IEditorCommand() = default;
    virtual bool Execute() = 0;
    virtual bool Undo() = 0;
    virtual const char* GetLabel() const = 0;
};

class EditorCommandManager
{
public:
    bool ExecuteCommand(std::unique_ptr<IEditorCommand> command);
    bool UndoCommand();
    bool RedoCommand();
    bool CanUndo() const;
    bool CanRedo() const;
    const char* GetUndoLabel() const;
    const char* GetRedoLabel() const;
    void Clear();

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_UndoStack;
    std::vector<std::unique_ptr<IEditorCommand>> m_RedoStack;
};

class RenameSceneInstanceCommand : public IEditorCommand
{
public:
    RenameSceneInstanceCommand(uint32_t instanceId, std::string oldName, std::string newName);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    uint32_t m_InstanceId = 0;
    std::string m_OldName;
    std::string m_NewName;
};

struct PrefabLinkState
{
    uint32_t instanceId = 0;
    std::string prefabSourcePath;
};

class CreateEmptyParentCommand : public IEditorCommand
{
public:
    explicit CreateEmptyParentCommand(std::vector<uint32_t> selectionIds);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    std::vector<uint32_t> m_SelectionIds;
    uint32_t m_CreatedParentId = 0;
    uint32_t m_PreviousActiveSelectionId = 0;
    std::vector<uint32_t> m_PreviousSelectionIds;
    std::vector<ParentTransformState> m_PreviousStates;
};

class UnpackPrefabCommand : public IEditorCommand
{
public:
    explicit UnpackPrefabCommand(std::vector<uint32_t> selectionIds);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    std::vector<uint32_t> m_SelectionIds;
    std::vector<PrefabLinkState> m_PreviousStates;
};

class DuplicateSelectionCommand : public IEditorCommand
{
public:
    explicit DuplicateSelectionCommand(std::vector<uint32_t> selectionIds);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    std::vector<uint32_t> m_SourceIds;
    std::vector<uint32_t> m_CreatedIds;
    uint32_t m_PreviousActiveSelectionId = 0;
    std::vector<uint32_t> m_PreviousSelectionIds;
};

class ReparentCommand : public IEditorCommand
{
public:
    ReparentCommand(uint32_t childInstanceId, uint32_t newParentInstanceId, bool keepWorldTransform);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    uint32_t m_ChildInstanceId = 0;
    uint32_t m_NewParentInstanceId = 0;
    bool m_KeepWorldTransform = true;
    uint32_t m_PreviousActiveSelectionId = 0;
    std::vector<uint32_t> m_PreviousSelectionIds;
    std::vector<ParentTransformState> m_PreviousStates;
};

class DeleteSelectionCommand : public IEditorCommand
{
public:
    explicit DeleteSelectionCommand(std::vector<uint32_t> selectionIds);

    bool Execute() override;
    bool Undo() override;
    const char* GetLabel() const override;

private:
    std::vector<uint32_t> m_TargetIds;
    std::string m_SerializedData;
    uint32_t m_PreviousActiveSelectionId = 0;
    std::vector<uint32_t> m_PreviousSelectionIds;
};
