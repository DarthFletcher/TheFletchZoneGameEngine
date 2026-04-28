#include "EditorCommands.h"

#include "EditorCommands.h"

#include "Scene.h"

#include <algorithm>
#include <utility>

namespace
{
    static SceneInstance* FindMutableInstanceById(uint32_t instanceId)
    {
        for (UINT index = 0; index < Scene::GetInstanceCount(); ++index)
        {
            SceneInstance* instance = Scene::GetInstance(index);
            if (instance && instance->instanceId == instanceId)
                return instance;
        }
        return nullptr;
    }

    static void AppendInstanceAndDescendants(uint32_t instanceId, std::vector<uint32_t>& outIds)
    {
        if (instanceId == 0)
            return;
        if (std::find(outIds.begin(), outIds.end(), instanceId) == outIds.end())
            outIds.push_back(instanceId);

        for (uint32_t childId : Scene::GetChildInstanceIds(instanceId))
            AppendInstanceAndDescendants(childId, outIds);
    }
}

bool EditorCommandManager::ExecuteCommand(std::unique_ptr<IEditorCommand> command)
{
    if (!command)
        return false;
    if (!command->Execute())
        return false;
    m_UndoStack.push_back(std::move(command));
    m_RedoStack.clear();
    return true;
}

bool EditorCommandManager::UndoCommand()
{
    if (m_UndoStack.empty())
        return false;

    std::unique_ptr<IEditorCommand> command = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();
    if (!command->Undo())
    {
        m_UndoStack.push_back(std::move(command));
        return false;
    }
    m_RedoStack.push_back(std::move(command));
    return true;
}

bool EditorCommandManager::RedoCommand()
{
    if (m_RedoStack.empty())
        return false;

    std::unique_ptr<IEditorCommand> command = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();
    if (!command->Execute())
    {
        m_RedoStack.push_back(std::move(command));
        return false;
    }
    m_UndoStack.push_back(std::move(command));
    return true;
}

bool EditorCommandManager::CanUndo() const
{
    return !m_UndoStack.empty();
}

bool EditorCommandManager::CanRedo() const
{
    return !m_RedoStack.empty();
}

const char* EditorCommandManager::GetUndoLabel() const
{
    if (m_UndoStack.empty())
        return "Undo";
    return m_UndoStack.back()->GetLabel();
}

const char* EditorCommandManager::GetRedoLabel() const
{
    if (m_RedoStack.empty())
        return "Redo";
    return m_RedoStack.back()->GetLabel();
}

void EditorCommandManager::Clear()
{
    m_UndoStack.clear();
    m_RedoStack.clear();
}

RenameSceneInstanceCommand::RenameSceneInstanceCommand(uint32_t instanceId, std::string oldName, std::string newName)
    : m_InstanceId(instanceId)
    , m_OldName(std::move(oldName))
    , m_NewName(std::move(newName))
{
}

bool RenameSceneInstanceCommand::Execute()
{
    if (m_InstanceId == 0 || m_NewName.empty() || m_OldName == m_NewName)
        return false;
    return Scene::RenameInstance(m_InstanceId, m_NewName);
}

bool RenameSceneInstanceCommand::Undo()
{
    if (m_InstanceId == 0 || m_OldName.empty())
        return false;
    return Scene::RenameInstance(m_InstanceId, m_OldName);
}

const char* RenameSceneInstanceCommand::GetLabel() const
{
    return "Rename Object";
}

CreateEmptyParentCommand::CreateEmptyParentCommand(std::vector<uint32_t> selectionIds)
    : m_SelectionIds(std::move(selectionIds))
{
}

bool CreateEmptyParentCommand::Execute()
{
    if (m_SelectionIds.empty())
        return false;

    m_PreviousActiveSelectionId = Scene::GetSelectedInstanceId();
    m_PreviousSelectionIds = Scene::GetSelectedInstanceIds();
    Scene::CaptureParentTransformState(m_SelectionIds, m_PreviousStates);
    if (m_PreviousStates.empty())
        return false;

    DirectX::XMFLOAT3 center{};
    for (const ParentTransformState& state : m_PreviousStates)
    {
        center.x += state.worldMatrix._41;
        center.y += state.worldMatrix._42;
        center.z += state.worldMatrix._43;
    }
    const float invCount = 1.0f / static_cast<float>(m_PreviousStates.size());
    center.x *= invCount;
    center.y *= invCount;
    center.z *= invCount;

    Scene::CreateEmpty(center);
    SceneInstance* parent = Scene::GetSelectedInstance();
    if (!parent)
        return false;

    m_CreatedParentId = parent->instanceId;
    Scene::RenameInstance(m_CreatedParentId, m_SelectionIds.size() > 1u ? "Group" : "Parent");

    for (uint32_t childId : m_SelectionIds)
    {
        if (childId == 0 || childId == m_CreatedParentId)
            continue;
        if (!Scene::SetParentInstance(childId, m_CreatedParentId, true))
            return false;
    }

    return true;
}

bool CreateEmptyParentCommand::Undo()
{
    if (m_CreatedParentId == 0 || m_PreviousStates.empty())
        return false;

    if (!Scene::RestoreParentTransformState(m_PreviousStates))
        return false;
    if (!Scene::DeleteInstanceById(m_CreatedParentId))
        return false;
    Scene::RestoreSelectionState(m_PreviousActiveSelectionId, m_PreviousSelectionIds);
    m_CreatedParentId = 0;
    return true;
}

const char* CreateEmptyParentCommand::GetLabel() const
{
    return "Create Empty Parent";
}

UnpackPrefabCommand::UnpackPrefabCommand(std::vector<uint32_t> selectionIds)
    : m_SelectionIds(std::move(selectionIds))
{
}

bool UnpackPrefabCommand::Execute()
{
    if (m_SelectionIds.empty())
        return false;

    m_PreviousStates.clear();
    std::vector<uint32_t> expandedIds;
    for (uint32_t instanceId : m_SelectionIds)
        AppendInstanceAndDescendants(instanceId, expandedIds);

    bool changed = false;
    for (uint32_t instanceId : expandedIds)
    {
        SceneInstance* instance = FindMutableInstanceById(instanceId);
        if (!instance || instance->prefabSourcePath.empty())
            continue;

        m_PreviousStates.push_back({ instanceId, instance->prefabSourcePath });
        instance->prefabSourcePath.clear();
        changed = true;
    }

    if (!changed)
        return false;

    Scene::RebuildRenderInstancesFromSceneData();
    Scene::MarkInstancesDirty();
    return true;
}

bool UnpackPrefabCommand::Undo()
{
    if (m_PreviousStates.empty())
        return false;

    bool restoredAny = false;
    for (const PrefabLinkState& state : m_PreviousStates)
    {
        SceneInstance* instance = FindMutableInstanceById(state.instanceId);
        if (!instance)
            continue;
        instance->prefabSourcePath = state.prefabSourcePath;
        restoredAny = true;
    }

    if (!restoredAny)
        return false;

    Scene::RebuildRenderInstancesFromSceneData();
    Scene::MarkInstancesDirty();
    return true;
}

const char* UnpackPrefabCommand::GetLabel() const
{
    return "Unpack Prefab";
}

DuplicateSelectionCommand::DuplicateSelectionCommand(std::vector<uint32_t> selectionIds)
    : m_SourceIds(std::move(selectionIds))
{
}

bool DuplicateSelectionCommand::Execute()
{
    if (m_SourceIds.empty())
        return false;

    m_PreviousActiveSelectionId = Scene::GetSelectedInstanceId();
    m_PreviousSelectionIds = Scene::GetSelectedInstanceIds();
    m_CreatedIds.clear();
    return Scene::DuplicateInstances(m_SourceIds, m_CreatedIds);
}

bool DuplicateSelectionCommand::Undo()
{
    if (m_CreatedIds.empty())
        return false;
    if (!Scene::DeleteInstances(m_CreatedIds))
        return false;
    Scene::RestoreSelectionState(m_PreviousActiveSelectionId, m_PreviousSelectionIds);
    return true;
}

const char* DuplicateSelectionCommand::GetLabel() const
{
    return "Duplicate Selection";
}

DeleteSelectionCommand::DeleteSelectionCommand(std::vector<uint32_t> selectionIds)
    : m_TargetIds(std::move(selectionIds))
{
}

bool DeleteSelectionCommand::Execute()
{
    if (m_TargetIds.empty())
        return false;

    m_PreviousActiveSelectionId = Scene::GetSelectedInstanceId();
    m_PreviousSelectionIds = Scene::GetSelectedInstanceIds();

    std::vector<uint32_t> expandedIds;
    for (uint32_t instanceId : m_TargetIds)
        AppendInstanceAndDescendants(instanceId, expandedIds);

    if (!Scene::SerializeInstances(expandedIds, m_SerializedData))
        return false;
    return Scene::DeleteInstances(expandedIds);
}

bool DeleteSelectionCommand::Undo()
{
    if (m_SerializedData.empty())
        return false;

    std::vector<uint32_t> restoredIds;
    if (!Scene::DeserializeInstances(m_SerializedData, restoredIds))
        return false;
    Scene::RestoreSelectionState(m_PreviousActiveSelectionId, m_PreviousSelectionIds);
    return true;
}

const char* DeleteSelectionCommand::GetLabel() const
{
    return "Delete Selection";
}

ReparentCommand::ReparentCommand(uint32_t childInstanceId, uint32_t newParentInstanceId, bool keepWorldTransform)
    : m_ChildInstanceId(childInstanceId)
    , m_NewParentInstanceId(newParentInstanceId)
    , m_KeepWorldTransform(keepWorldTransform)
{
}

bool ReparentCommand::Execute()
{
    if (m_ChildInstanceId == 0)
        return false;
    if (!Scene::CanParentInstance(m_ChildInstanceId, m_NewParentInstanceId))
        return false;

    m_PreviousActiveSelectionId = Scene::GetSelectedInstanceId();
    m_PreviousSelectionIds = Scene::GetSelectedInstanceIds();
    m_PreviousStates.clear();
    Scene::CaptureParentTransformState({ m_ChildInstanceId }, m_PreviousStates);
    if (m_PreviousStates.empty())
        return false;

    if (m_KeepWorldTransform)
    {
        std::string reason;
        if (!Scene::CanPreserveWorldTransformOnReparent(m_ChildInstanceId, m_NewParentInstanceId, &reason))
            return false;
    }

    return Scene::SetParentInstance(m_ChildInstanceId, m_NewParentInstanceId, m_KeepWorldTransform);
}

bool ReparentCommand::Undo()
{
    if (m_PreviousStates.empty())
        return false;
    if (!Scene::RestoreParentTransformState(m_PreviousStates))
        return false;
    Scene::RestoreSelectionState(m_PreviousActiveSelectionId, m_PreviousSelectionIds);
    return true;
}

const char* ReparentCommand::GetLabel() const
{
    return m_KeepWorldTransform ? "Reparent (Keep World)" : "Reparent (Keep Local)";
}
