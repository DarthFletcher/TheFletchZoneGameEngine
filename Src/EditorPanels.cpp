#include "EditorPanels.h"

#include "imgui.h"
#include "EditorCommon.h"
#include "EditorCommands.h"
#include "Graphics.h"
#include "Logger.h"
#include "EditorState.h"
#include "Engine.h"
#include "Scene.h"
#include "CameraData.h"
#include "MaterialManager.h"
#include "TextureManager.h"
#include "EditorGizmo.h"
#include "UI.h"
#include "EditorIcons.h"
#include "ImGuiUtils.h"

#include <format>
#include <array>
#include <string>
#include <chrono>
#include <cstring>
#include <cfloat>
#include <cctype>
#include <filesystem>
#include <functional>
#include <unordered_map>

extern Engine* g_engineInstance;

namespace EditorPanels
{
    enum class AssetCategory
    {
        All,
        Primitives,
        Prefabs,
        Scenes,
        Textures,
        Materials,
    };

    static constexpr size_t kMaxUndoSnapshots = 64;
    static constexpr const char* kSceneCreateCubePayload = "SCENE_CREATE_CUBE";
    static constexpr const char* kSceneCreateSpherePayload = "SCENE_CREATE_SPHERE";
    static constexpr const char* kSceneCreatePlanePayload = "SCENE_CREATE_PLANE";
    static constexpr const char* kSceneCreateCylinderPayload = "SCENE_CREATE_CYLINDER";
    static constexpr const char* kSceneCreateCapsulePayload = "SCENE_CREATE_CAPSULE";
    static constexpr const char* kSceneCreateTorusPayload = "SCENE_CREATE_TORUS";
    static constexpr const char* kSceneCreateConePayload = "SCENE_CREATE_CONE";
    static constexpr const char* kSceneCreatePrefabPayload = "SCENE_CREATE_PREFAB";
    static constexpr const char* kSceneCreateEmptyPayload = "SCENE_CREATE_EMPTY";
    static constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
    static constexpr const char* kAssetMaterialPayload = "ASSET_MATERIAL";
    static constexpr const char* kAssetFolderPayload = "ASSET_FOLDER";
    static constexpr const char* kAssetScenePayload = "ASSET_SCENE";
    static EditorGizmo g_EditorGizmo;
    static bool g_WasGizmoDragging = false;
    static AssetCategory g_SelectedAssetCategory = AssetCategory::All;
    static std::string g_SelectedAssetId;
    static char g_AssetSearch[128] = {};
    static char g_FolderRenameBuffer[128] = {};
    static char g_SceneRenameBuffer[128] = {};
    static char g_AssetRenameBuffer[128] = {};
    static char g_MaterialRenameBuffer[128] = {};
    static char g_SceneInstanceRenameBuffer[128] = {};
    static std::filesystem::path g_AssetCurrentFolder = std::filesystem::path("Assets");
    static std::filesystem::path g_PendingFolderRenamePath;
    static std::filesystem::path g_PendingSceneRenamePath;
    static std::filesystem::path g_PendingAssetRenamePath;
    static std::filesystem::path g_PendingDeletePath;
    static bool g_PendingDeleteIsFolder = false;
    static const char* g_PendingDeleteKindLabel = "Asset";
    static const char* g_PendingAssetRenameKindLabel = "asset";
    static int g_PendingMaterialRenameIndex = -1;
    static int g_PendingMaterialDeleteIndex = -1;
    static std::string g_PendingAssetRevealId;
    static bool g_RequestOpenRenameFolderPopup = false;
    static bool g_RequestOpenRenameScenePopup = false;
    static bool g_RequestOpenRenameAssetPopup = false;
    static bool g_RequestOpenRenameMaterialPopup = false;
    static bool g_RequestOpenDeleteAssetPopup = false;
    static bool g_RequestOpenDeleteMaterialPopup = false;
    static uint32_t g_PendingReparentChildId = 0;
    static uint32_t g_PendingReparentParentId = 0;
    static std::string g_PendingReparentKeepWorldReason;
    static bool g_RequestOpenReparentPopup = false;
    static uint32_t g_PendingSceneInstanceRenameId = 0;
    static bool g_RequestOpenSceneInstanceRenamePopup = false;
    static std::unordered_map<std::string, ImTextureID> g_AssetThumbnailCache;
    static bool IsTextureAssetPath(const std::string& path);
    static int GetAssetTileColumnCount(float availableWidth, float tileWidth, float spacing);
    static ImTextureID GetAssetThumbnailTexture(const std::string& assetId, const std::string& fallbackTexturePath);
    static bool DrawAssetTile(const char* id, const char* title, const char* icon, const char* payloadType, const void* payloadData, size_t payloadSize, bool selected, ImTextureID thumbnailTexture);
    static void DrawAssetSectionHeader(const char* label);
    static std::vector<std::filesystem::path> EnumerateTextureAssets();
    static std::vector<std::filesystem::path> EnumerateSceneAssets();
    static std::vector<std::filesystem::path> EnumerateAssetFolders();
    static bool CreateAssetFolder(std::filesystem::path& outCreatedPath);
    static bool RenameAssetFolder(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath);
    static bool RenameSceneAsset(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath);
    static bool RenameAssetFile(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath);
    static bool DuplicateAssetFile(const std::filesystem::path& sourcePath, std::filesystem::path& outDuplicatedPath);
    static bool MoveAssetFileToFolder(const std::filesystem::path& sourcePath, const std::filesystem::path& targetFolder, std::filesystem::path& outMovedPath);
    static bool DeleteAssetPath(const std::filesystem::path& targetPath, bool isFolder);
    static bool CanDeleteAssetPath(const std::filesystem::path& targetPath, bool isFolder, std::string& outReason);
    static std::filesystem::path GetAssetRootPath();
    static void EnsureAssetCurrentFolderValid();
    static bool IsDirectChildOfFolder(const std::filesystem::path& path, const std::filesystem::path& folder);
    static bool IsPathWithinFolder(const std::filesystem::path& path, const std::filesystem::path& folder);
    static void QueueAssetSelection(const std::string& assetId);
    static void QueueMaterialSelection(int materialIndex, const std::string& materialName);
    static std::string FindFirstSelectableAssetIdInFolder(const std::filesystem::path& folder);
    static bool ProjectWorldToSceneScreen(const DirectX::XMFLOAT3& worldPos, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2& outScreen);
    static void DrawSceneCameraIcons(const CameraData& camera, ImDrawList* drawList, ImVec2 sceneMin, ImVec2 sceneSize);
    static void DrawGameInteractionTargetHighlight(ImDrawList* drawList, ImVec2 imageMin, ImVec2 imageSize);
    static void DrawSceneMaterialDropOverlay(ImDrawList* drawList, ImVec2 imageMin, ImVec2 imageSize);
    static void SetInstanceAsMainCamera(SceneInstance* selected);
    static const char* GetVaultTypeLabel(VaultType vaultType);
    static SceneInstance* FindSceneInstanceById(uint32_t instanceId);
    static bool CanAssignMaterialToInstance(const SceneInstance* instance);
    static bool ApplyMaterialToSceneInstance(EditorState& editor, SceneInstance* instance, int materialIndex);
    static std::filesystem::path BuildDefaultSelectionPrefabPath();
    static bool SaveCurrentSelectionAsPrefab(EditorState& editor);
    static bool CreateEmptyParentForSelection(EditorState& editor);
    static bool SelectionContainsPrefabInstance();

    static const char* GetAssetCategoryLabel(AssetCategory category)
    {
        switch (category)
        {
        case AssetCategory::Primitives: return "Primitives";
        case AssetCategory::Prefabs: return "Prefabs";
        case AssetCategory::Scenes: return "Scenes";
        case AssetCategory::Textures: return "Textures";
        case AssetCategory::Materials: return "Materials";
        case AssetCategory::All:
        default: return "All";
        }
    }

    static void PushUndoSnapshot(EditorState& editor);

    static SceneHistoryEntry MakeHistoryEntry()
    {
        SceneHistoryEntry entry{};
        entry.sceneSnapshot = Scene::SerializeToString();
        entry.activeSelectedInstanceId = Scene::GetSelectedInstanceId();
        entry.selectedInstanceIds = Scene::GetSelectedInstanceIds();
        return entry;
    }

    static std::string SanitizeAssetName(std::string name)
    {
        for (char& c : name)
        {
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-'))
                c = '_';
        }
        if (name.empty())
            name = "Prefab";
        return name;
    }

    static std::filesystem::path BuildDefaultSelectionPrefabPath()
    {
        SceneInstance* selected = Scene::GetSelectedInstance();
        std::string baseName = selected ? SanitizeAssetName(selected->name) : std::string("Prefab");
        if (Scene::GetSelectedInstanceIds().size() > 1u)
            baseName += "_Group";
        return std::filesystem::path("Assets") / "Prefabs" / (baseName + ".prefab.json");
    }

    static bool SaveCurrentSelectionAsPrefab(EditorState& editor)
    {
        if (!Scene::GetSelectedInstance())
            return false;

        const std::filesystem::path prefabPath = BuildDefaultSelectionPrefabPath();
        if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
        {
            Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
            return false;
        }

        PrefabWorkflow().open = true;
        return true;
    }

    static bool ExecuteEditorCommand(EditorState& editor, std::unique_ptr<IEditorCommand> command)
    {
        if (!editor.commandManager.ExecuteCommand(std::move(command)))
            return false;

        editor.undoEntryKinds.push_back(EditorUndoEntryKind::Command);
        editor.redoEntryKinds.clear();
        return true;
    }

    static bool DuplicateCurrentSelection(EditorState& editor)
    {
        std::vector<uint32_t> selectionIds = Scene::GetSelectedInstanceIds();
        if (selectionIds.empty())
        {
            if (const uint32_t activeId = Scene::GetSelectedInstanceId(); activeId != 0)
                selectionIds.push_back(activeId);
        }
        if (selectionIds.empty())
            return false;

        if (!ExecuteEditorCommand(editor, std::make_unique<DuplicateSelectionCommand>(selectionIds)))
            return false;
        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return true;
    }

    static bool DeleteCurrentSelection(EditorState& editor)
    {
        std::vector<uint32_t> selectionIds = Scene::GetSelectedInstanceIds();
        if (selectionIds.empty())
        {
            if (const uint32_t activeId = Scene::GetSelectedInstanceId(); activeId != 0)
                selectionIds.push_back(activeId);
        }
        if (selectionIds.empty())
            return false;

        if (!ExecuteEditorCommand(editor, std::make_unique<DeleteSelectionCommand>(selectionIds)))
            return false;
        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return true;
    }

    static bool CreateEmptyParentForSelection(EditorState& editor)
    {
        const std::vector<uint32_t> selectionIds = Scene::GetSelectedInstanceIds();
        if (selectionIds.empty())
            return false;

        if (!ExecuteEditorCommand(editor, std::make_unique<CreateEmptyParentCommand>(selectionIds)))
            return false;
        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return true;
    }

    static bool SelectionContainsPrefabInstance()
    {
        const std::vector<uint32_t>& selectionIds = Scene::GetSelectedInstanceIds();
        for (uint32_t instanceId : selectionIds)
        {
            if (SceneInstance* instance = FindSceneInstanceById(instanceId))
            {
                if (!instance->prefabSourcePath.empty())
                    return true;
            }
        }
        if (SceneInstance* selected = Scene::GetSelectedInstance())
            return !selected->prefabSourcePath.empty();
        return false;
    }

    static std::vector<std::filesystem::path> EnumeratePrefabAssets()
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path assetRoot = std::filesystem::path("Assets");
        if (!std::filesystem::exists(assetRoot))
            return result;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file())
                continue;
            const std::string filename = entry.path().filename().string();
            if (filename.size() < 12 || filename.find(".prefab.json") == std::string::npos)
                continue;
            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    static bool TryGetCurrentSelectionAnchor(const EditorState& editor, DirectX::XMFLOAT3& outAnchor);

    static void PushUndoSnapshot(EditorState& editor)
    {
        const SceneHistoryEntry snapshot = MakeHistoryEntry();
        if (!editor.undoSceneSnapshots.empty() && editor.undoSceneSnapshots.back().sceneSnapshot == snapshot.sceneSnapshot &&
            editor.undoSceneSnapshots.back().activeSelectedInstanceId == snapshot.activeSelectedInstanceId &&
            editor.undoSceneSnapshots.back().selectedInstanceIds == snapshot.selectedInstanceIds)
            return;

        editor.undoSceneSnapshots.push_back(snapshot);
        if (editor.undoSceneSnapshots.size() > kMaxUndoSnapshots)
        {
            editor.undoSceneSnapshots.erase(editor.undoSceneSnapshots.begin());
            auto oldestSnapshotIt = std::find(editor.undoEntryKinds.begin(), editor.undoEntryKinds.end(), EditorUndoEntryKind::Snapshot);
            if (oldestSnapshotIt != editor.undoEntryKinds.end())
                editor.undoEntryKinds.erase(oldestSnapshotIt);
        }
        editor.undoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
        editor.redoSceneSnapshots.clear();
        editor.redoEntryKinds.clear();
    }

    static bool DrawTickFloat(const char* label, float& value, float minValue, float maxValue, float step, const char* format = "%.2f")
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::SmallButton("-"))
        {
            value = (std::max)(minValue, value - step);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(96.0f);
        changed = ImGui::DragFloat("##value", &value, step, minValue, maxValue, format) || changed;
        value = std::clamp(value, minValue, maxValue);
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
        {
            value = (std::min)(maxValue, value + step);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    static const char* GetSceneDebugViewModeLabel(SceneDebugViewMode mode)
    {
        switch (mode)
        {
        case SceneDebugViewMode::Albedo: return "Albedo";
        case SceneDebugViewMode::Normals: return "Normals";
        case SceneDebugViewMode::Metallic: return "Metallic";
        case SceneDebugViewMode::Roughness: return "Roughness";
        case SceneDebugViewMode::LightingOnly: return "Lighting Only";
        case SceneDebugViewMode::AmbientOnly: return "Ambient Only";
        case SceneDebugViewMode::SpecularOnly: return "Specular Only";
        case SceneDebugViewMode::SelectionMask: return "Selection Mask";
        case SceneDebugViewMode::Depth: return "Depth";
        case SceneDebugViewMode::LinearDepth: return "Linear Depth";
        case SceneDebugViewMode::WorldPosition: return "World Position";
        case SceneDebugViewMode::UVs: return "UVs";
        case SceneDebugViewMode::LightDirection: return "Light Direction";
        case SceneDebugViewMode::Lit:
        default:
            return "Lit";
        }
    }

    static void DrawMaterialDebugViewControls(EditorState& editor, const char* idSuffix)
    {
        ImGui::PushID(idSuffix);
        ImGui::TextUnformatted("Material Debug View");
        const SceneDebugViewMode currentMode = editor.sceneDebugViewMode;
        if (ImGui::BeginCombo("##MaterialDebugView", GetSceneDebugViewModeLabel(currentMode)))
        {
            const SceneDebugViewMode modes[] = {
                SceneDebugViewMode::Lit,
                SceneDebugViewMode::Albedo,
                SceneDebugViewMode::Normals,
                SceneDebugViewMode::Metallic,
                SceneDebugViewMode::Roughness,
                SceneDebugViewMode::LightingOnly,
                SceneDebugViewMode::AmbientOnly,
                SceneDebugViewMode::SpecularOnly,
                SceneDebugViewMode::SelectionMask,
                SceneDebugViewMode::Depth,
                SceneDebugViewMode::LinearDepth,
                SceneDebugViewMode::WorldPosition,
                SceneDebugViewMode::UVs,
                SceneDebugViewMode::LightDirection,
            };

            for (SceneDebugViewMode mode : modes)
            {
                const bool selected = (editor.sceneDebugViewMode == mode);
                if (ImGui::Selectable(GetSceneDebugViewModeLabel(mode), selected))
                    editor.sceneDebugViewMode = mode;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##MaterialDebugView"))
            editor.sceneDebugViewMode = SceneDebugViewMode::Lit;
        ImGui::PopID();
    }

    static void DrawMaterialInspectorFields(
        Material* material,
        EditorState& editor,
        const char* idSuffix,
        const std::function<void(Material*)>& emitMaterialChanged)
    {
        if (!material)
            return;

        auto textureLabel = [](Texture* texture) -> std::string
        {
            if (!texture || texture->sourcePath.empty())
                return "<None>";
            return std::filesystem::path(texture->sourcePath).filename().string();
        };

        auto drawTextureSlot = [&](const char* comboLabel, Texture* currentTexture, const std::function<void(const std::string&)>& assignTexture, const std::function<void()>& clearTexture)
        {
            const std::string currentTextureLabel = textureLabel(currentTexture);
            const std::string comboId = std::format("{}##{}", comboLabel, idSuffix);
            if (ImGui::BeginCombo(comboId.c_str(), currentTextureLabel.c_str()))
            {
                static std::unordered_map<std::string, std::string> textureFilters;
                std::string& filterText = textureFilters[comboId];
                char filterBuffer[128] = {};
                strncpy_s(filterBuffer, filterText.c_str(), sizeof(filterBuffer) - 1u);
                const std::string filterId = std::format("##TextureFilter{}", idSuffix);
                if (ImGui::InputTextWithHint(filterId.c_str(), "Filter textures...", filterBuffer, IM_ARRAYSIZE(filterBuffer)))
                    filterText = filterBuffer;

                auto matchesTextureFilter = [&](const std::string& textureName)
                {
                    if (filterText.empty())
                        return true;
                    std::string lowerName = textureName;
                    std::string lowerFilter = filterText;
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    return lowerName.find(lowerFilter) != std::string::npos;
                };

                const bool noTextureSelected = (currentTexture == nullptr);
                if (matchesTextureFilter("<None>") && ImGui::Selectable("<None>", noTextureSelected))
                {
                    clearTexture();
                    emitMaterialChanged(material);
                }

                const std::filesystem::path textureRoot = std::filesystem::path("Assets") / "Textures";
                for (const auto& texturePath : EnumerateTextureAssets())
                {
                    const std::string texturePathString = texturePath.string();
                    const std::string textureLabelText = texturePath.lexically_relative(textureRoot).generic_string();
                    if (!matchesTextureFilter(textureLabelText))
                        continue;
                    const bool isCurrent = currentTexture && currentTexture->sourcePath == texturePathString;
                    if (ImGui::Selectable(textureLabelText.c_str(), isCurrent))
                    {
                        assignTexture(texturePathString);
                        emitMaterialChanged(material);
                    }
                }
                ImGui::EndCombo();
            }
        };

        auto drawTexturePreview = [&](const char* label, Texture* texture)
        {
            ImGui::TextUnformatted(label);
            if (texture && !texture->sourcePath.empty())
            {
                const ImTextureID preview = GetAssetThumbnailTexture(texture->sourcePath, texture->sourcePath);
                if (preview)
                {
                    ImGui::Image(preview, ImVec2(48.0f, 48.0f), ImVec2(0, 0), ImVec2(1, 1));
                    ImGui::SameLine();
                }
                ImGui::BeginGroup();
                ImGui::TextUnformatted(textureLabel(texture).c_str());
                ImGui::TextDisabled("%d x %d", texture->width, texture->height);
                ImGui::EndGroup();
            }
            else
            {
                ImGui::TextDisabled("<None>");
            }
        };

        DrawMaterialDebugViewControls(editor, idSuffix);
        drawTexturePreview("Albedo Preview", material->albedo);
        drawTextureSlot(
            "Albedo Texture",
            material->albedo,
            [&](const std::string& texturePath) { MaterialManager::GetInstance().LoadAlbedoTexture(material, texturePath); },
            [&]() { MaterialManager::GetInstance().SetAlbedoTexture(material, nullptr); });

        drawTexturePreview("Normal Preview", material->normal);
        drawTextureSlot(
            "Normal Texture",
            material->normal,
            [&](const std::string& texturePath) { MaterialManager::GetInstance().LoadNormalTexture(material, texturePath); },
            [&]() { MaterialManager::GetInstance().SetNormalTexture(material, nullptr); });

        drawTexturePreview("Metallic Preview", material->metallicMap);
        drawTextureSlot(
            "Metallic Texture",
            material->metallicMap,
            [&](const std::string& texturePath) { MaterialManager::GetInstance().LoadMetallicTexture(material, texturePath); },
            [&]() { MaterialManager::GetInstance().SetMetallicTexture(material, nullptr); });

        drawTexturePreview("Roughness Preview", material->roughnessMap);
        drawTextureSlot(
            "Roughness Texture",
            material->roughnessMap,
            [&](const std::string& texturePath) { MaterialManager::GetInstance().LoadRoughnessTexture(material, texturePath); },
            [&]() { MaterialManager::GetInstance().SetRoughnessTexture(material, nullptr); });

        ImGui::Text("Normal: %s", textureLabel(material->normal).c_str());
        ImGui::Text("Metallic Map: %s", textureLabel(material->metallicMap).c_str());
        ImGui::Text("Roughness Map: %s", textureLabel(material->roughnessMap).c_str());

        float baseColor[3] = { material->baseColor.x, material->baseColor.y, material->baseColor.z };
        if (ImGui::ColorEdit3("Base Color", baseColor))
        {
            material->SetBaseColor({ baseColor[0], baseColor[1], baseColor[2] });
            emitMaterialChanged(material);
        }

        float metallic = material->metallic;
        if (DrawTickFloat("Metallic", metallic, 0.0f, 1.0f, 0.05f))
        {
            material->SetFloat("metallic", metallic);
            emitMaterialChanged(material);
        }

        float roughness = material->roughness;
        if (DrawTickFloat("Roughness", roughness, 0.0f, 1.0f, 0.05f))
        {
            material->SetFloat("roughness", roughness);
            emitMaterialChanged(material);
        }

        bool flipNormalY = Graphics::GetInstance().GetFlipNormalGreenChannel();
        if (ImGui::Checkbox("Normal Flip Y", &flipNormalY))
        {
            Graphics::GetInstance().GetFlipNormalGreenChannel() = flipNormalY;
            emitMaterialChanged(material);
        }
    }

    static bool DrawTickFloat3(const char* label, DirectX::XMFLOAT3& value, float step, float minValue, float maxValue)
    {
        bool changed = false;
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        changed = ImGui::DragFloat3("##value", &value.x, step, minValue, maxValue, "%.2f") || changed;
        const char* axisLabels[3] = { "X", "Y", "Z" };
        float* axisValues[3] = { &value.x, &value.y, &value.z };
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis > 0)
                ImGui::SameLine();
            ImGui::TextUnformatted(axisLabels[axis]);
            ImGui::SameLine();
            const std::string minusId = std::format("-{}", axisLabels[axis]);
            if (ImGui::SmallButton(minusId.c_str()))
            {
                *axisValues[axis] = (std::max)(minValue, *axisValues[axis] - step);
                changed = true;
            }
            ImGui::SameLine();
            const std::string plusId = std::format("+{}", axisLabels[axis]);
            if (ImGui::SmallButton(plusId.c_str()))
            {
                *axisValues[axis] = (std::min)(maxValue, *axisValues[axis] + step);
                changed = true;
            }
        }
        ImGui::PopID();
        return changed;
    }

    static bool PerformUndo(EditorState& editor)
    {
        if (editor.undoEntryKinds.empty())
            return false;

        const EditorUndoEntryKind entryKind = editor.undoEntryKinds.back();
        editor.undoEntryKinds.pop_back();

        if (entryKind == EditorUndoEntryKind::Command)
        {
            if (!editor.commandManager.UndoCommand())
            {
                editor.undoEntryKinds.push_back(EditorUndoEntryKind::Command);
                return false;
            }
            editor.redoEntryKinds.push_back(EditorUndoEntryKind::Command);
            return true;
        }

        if (editor.undoSceneSnapshots.empty())
        {
            editor.undoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
            return false;
        }

        editor.redoSceneSnapshots.push_back(MakeHistoryEntry());
        const SceneHistoryEntry snapshot = editor.undoSceneSnapshots.back();
        editor.undoSceneSnapshots.pop_back();
        if (!Scene::LoadFromString(snapshot.sceneSnapshot))
        {
            editor.undoSceneSnapshots.push_back(snapshot);
            editor.redoSceneSnapshots.pop_back();
            editor.undoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
            return false;
        }
        Scene::RestoreSelectionState(snapshot.activeSelectedInstanceId, snapshot.selectedInstanceIds);
        editor.redoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
        return true;
    }

    static bool PerformRedo(EditorState& editor)
    {
        if (editor.redoEntryKinds.empty())
            return false;

        const EditorUndoEntryKind entryKind = editor.redoEntryKinds.back();
        editor.redoEntryKinds.pop_back();

        if (entryKind == EditorUndoEntryKind::Command)
        {
            if (!editor.commandManager.RedoCommand())
            {
                editor.redoEntryKinds.push_back(EditorUndoEntryKind::Command);
                return false;
            }
            editor.undoEntryKinds.push_back(EditorUndoEntryKind::Command);
            return true;
        }

        if (editor.redoSceneSnapshots.empty())
        {
            editor.redoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
            return false;
        }

        editor.undoSceneSnapshots.push_back(MakeHistoryEntry());
        const SceneHistoryEntry snapshot = editor.redoSceneSnapshots.back();
        editor.redoSceneSnapshots.pop_back();
        if (!Scene::LoadFromString(snapshot.sceneSnapshot))
        {
            editor.redoSceneSnapshots.push_back(snapshot);
            editor.undoSceneSnapshots.pop_back();
            editor.redoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
            return false;
        }
        Scene::RestoreSelectionState(snapshot.activeSelectedInstanceId, snapshot.selectedInstanceIds);
        editor.undoEntryKinds.push_back(EditorUndoEntryKind::Snapshot);
        return true;
    }

    struct EditorWorldRay
    {
        DirectX::XMFLOAT3 origin{};
        DirectX::XMFLOAT3 direction{};
    };

    static bool ScreenPointToSceneWorldRay(ImVec2 mousePos, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, EditorWorldRay& outRay)
    {
        using namespace DirectX;
        const float width = (std::max)(sceneSize.x, 1.0f);
        const float height = (std::max)(sceneSize.y, 1.0f);
        const float localX = mousePos.x - sceneMin.x;
        const float localY = mousePos.y - sceneMin.y;
        const float px = (2.0f * localX / width) - 1.0f;
        const float py = 1.0f - (2.0f * localY / height);

        const XMVECTOR nearClip = XMVectorSet(px, py, 0.0f, 1.0f);
        const XMVECTOR farClip = XMVectorSet(px, py, 1.0f, 1.0f);
        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMMATRIX invViewProj = XMMatrixInverse(nullptr, XMMatrixMultiply(view, proj));

        XMVECTOR nearWorld = XMVector4Transform(nearClip, invViewProj);
        XMVECTOR farWorld = XMVector4Transform(farClip, invViewProj);
        const float nearW = XMVectorGetW(nearWorld);
        const float farW = XMVectorGetW(farWorld);
        if (fabsf(nearW) < 1e-6f || fabsf(farW) < 1e-6f)
            return false;

        nearWorld = XMVectorScale(nearWorld, 1.0f / nearW);
        farWorld = XMVectorScale(farWorld, 1.0f / farW);
        XMStoreFloat3(&outRay.origin, nearWorld);
        XMStoreFloat3(&outRay.direction, XMVector3Normalize(XMVectorSubtract(farWorld, nearWorld)));
        return true;
    }

    static bool IntersectRayWithPlaneY(const EditorWorldRay& ray, float planeY, DirectX::XMFLOAT3& outHit)
    {
        constexpr float kEpsilon = 1e-6f;
        if (fabsf(ray.direction.y) < kEpsilon)
            return false;
        const float t = (planeY - ray.origin.y) / ray.direction.y;
        if (t < 0.0f)
            return false;
        outHit = { ray.origin.x + ray.direction.x * t, planeY, ray.origin.z + ray.direction.z * t };
        return true;
    }

    static DirectX::XMFLOAT3 ComputeSceneDropSpawnPosition(const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2 mousePos)
    {
        EditorWorldRay ray{};
        if (ScreenPointToSceneWorldRay(mousePos, camera, sceneMin, sceneSize, ray))
        {
            DirectX::XMFLOAT3 hit{};
            if (IntersectRayWithPlaneY(ray, 0.5f, hit))
                return hit;
            return { ray.origin.x + ray.direction.x * 5.0f, (std::max)(0.5f, ray.origin.y + ray.direction.y * 5.0f), ray.origin.z + ray.direction.z * 5.0f };
        }
        return camera.position;
    }

    static float ComputeWorldMatrixMaxBasisScale(const DirectX::XMFLOAT4X4& world)
    {
        const float sx = std::sqrt(world._11 * world._11 + world._12 * world._12 + world._13 * world._13);
        const float sy = std::sqrt(world._21 * world._21 + world._22 * world._22 + world._23 * world._23);
        const float sz = std::sqrt(world._31 * world._31 + world._32 * world._32 + world._33 * world._33);
        return (std::max)(sx, (std::max)(sy, sz));
    }

    static float ComputeSelectionFocusDistance(const EditorState& editor)
    {
        DirectX::XMFLOAT3 anchor{};
        if (!TryGetCurrentSelectionAnchor(editor, anchor))
            return 6.0f;

        float maxExtent = 0.0f;
        for (uint32_t id : Scene::GetSelectedInstanceIds())
        {
            DirectX::XMFLOAT4X4 world{};
            if (!Scene::TryGetInstanceWorldMatrix(id, world))
                continue;
            const DirectX::XMFLOAT3 center{ world._41, world._42, world._43 };
            const float dx = center.x - anchor.x;
            const float dy = center.y - anchor.y;
            const float dz = center.z - anchor.z;
            const float distanceToCenter = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float radius = 0.8660254f * ComputeWorldMatrixMaxBasisScale(world);
            maxExtent = (std::max)(maxExtent, distanceToCenter + radius);
        }

        if (maxExtent <= 0.0f)
            maxExtent = 1.0f;
        return (std::max)(4.5f, maxExtent * 3.0f);
    }

    static bool ProjectWorldToSceneViewport(
        const DirectX::XMFLOAT3& worldPos,
        const CameraData& camera,
        ImVec2 sceneMin,
        ImVec2 sceneSize,
        ImVec2& outScreen)
    {
        using namespace DirectX;

        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        const XMVECTOR clip = XMVector4Transform(pos, XMMatrixMultiply(view, proj));

        const float w = XMVectorGetW(clip);
        if (fabsf(w) < 1e-6f)
            return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        const float ndcZ = XMVectorGetZ(clip) / w;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return false;

        outScreen.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
        outScreen.y = sceneMin.y + (-ndcY * 0.5f + 0.5f) * sceneSize.y;
        return true;
    }

    static const char* ViewModeLabel(ViewMode m)
    {
        switch (m)
        {
        case ViewMode::Mode2D: return "2D";
        case ViewMode::Mode3D:
        default: return "3D";
        }
    }

    static std::chrono::steady_clock::time_point g_ScenePanelDiag_Last = std::chrono::steady_clock::now();
    static bool ScenePanelDiag_ShouldLog(std::chrono::milliseconds interval = std::chrono::milliseconds(1000))
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - g_ScenePanelDiag_Last >= interval)
        {
            g_ScenePanelDiag_Last = now;
            return true;
        }
        return false;
    }

    static bool IsSceneCameraNavigating(const EditorState& editor)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool lmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool alt = io.KeyAlt;
        const bool ctrl = io.KeyCtrl;
        const bool shift = io.KeyShift;

        switch (editor.cameraNavMode)
        {
        case CameraNavMode::Unity_AltMouse:
            return alt && (lmbDown || mmbDown || rmbDown);
        case CameraNavMode::Unity_WASDMouse:
            return rmbDown || (alt && (lmbDown || mmbDown || rmbDown));
        case CameraNavMode::Blender_MMB:
            return mmbDown && (!shift || shift || ctrl);
        case CameraNavMode::TFZ_RMB:
        default:
            return rmbDown || mmbDown;
        }
    }

    static bool TryGetCurrentSelectionAnchor(const EditorState& editor, DirectX::XMFLOAT3& outAnchor)
    {
        if (editor.gizmoPivotMode == GizmoPivotMode::Center && Scene::TryGetSelectionCenter(outAnchor))
            return true;

        if (SceneInstance* selected = Scene::GetSelectedInstance())
        {
            outAnchor = Scene::GetInstanceWorldPosition(selected->instanceId);
            return true;
        }

        return Scene::TryGetSelectionCenter(outAnchor);
    }

    static bool TryGetSceneInstanceScreenRect(const SceneInstance& instance, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2& outMin, ImVec2& outMax)
    {
        using namespace DirectX;

        const XMFLOAT3 localCorners[8] =
        {
            { -0.5f, -0.5f, -0.5f }, { +0.5f, -0.5f, -0.5f },
            { -0.5f, +0.5f, -0.5f }, { +0.5f, +0.5f, -0.5f },
            { -0.5f, -0.5f, +0.5f }, { +0.5f, -0.5f, +0.5f },
            { -0.5f, +0.5f, +0.5f }, { +0.5f, +0.5f, +0.5f },
        };

        DirectX::XMFLOAT4X4 worldF{};
        if (!Scene::TryGetInstanceWorldMatrix(instance.instanceId, worldF))
            return false;
        const XMMATRIX world = XMLoadFloat4x4(&worldF);

        bool anyProjected = false;
        outMin = ImVec2(FLT_MAX, FLT_MAX);
        outMax = ImVec2(-FLT_MAX, -FLT_MAX);

        for (const XMFLOAT3& local : localCorners)
        {
            const XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&local), world);
            XMFLOAT3 worldPt{};
            XMStoreFloat3(&worldPt, p);

            ImVec2 screenPt{};
            if (!ProjectWorldToSceneViewport(worldPt, camera, sceneMin, sceneSize, screenPt))
                continue;

            anyProjected = true;
            outMin.x = (std::min)(outMin.x, screenPt.x);
            outMin.y = (std::min)(outMin.y, screenPt.y);
            outMax.x = (std::max)(outMax.x, screenPt.x);
            outMax.y = (std::max)(outMax.y, screenPt.y);
        }

        return anyProjected;
    }

    static EditorPanel g_scene{
        "Scene",
        true,
        []()
        {
            auto& panel = Scene();
            if (!panel.open)
            {
                Graphics::GetInstance().SetSceneViewportCameraInputState(false, false, false);
                return;
            }

            auto& gfx = Graphics::GetInstance();
            auto& sceneCamera = gfx.GetSceneCamera();

            if (ImGui::Begin(panel.name, &panel.open))
            {
                // Header view mode dropdown (kept lightweight; uses existing menu routing rules).
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("View");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::BeginCombo("##SceneViewMode", ViewModeLabel(gfx.GetViewMode())))
                {
                    const bool sel3d = (gfx.GetViewMode() == ViewMode::Mode3D);
                    const bool sel2d = (gfx.GetViewMode() == ViewMode::Mode2D);
                    if (ImGui::Selectable("3D", sel3d))
                    {
                        gfx.SetViewMode(ViewMode::Mode3D);
                        sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View3D);
                    }
                    if (ImGui::Selectable("2D", sel2d))
                    {
                        gfx.SetViewMode(ViewMode::Mode2D);
                        sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View2D);
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                const bool projectionToggleEnabled = !sceneCamera.Is2DMode();
                if (!projectionToggleEnabled)
                    ImGui::BeginDisabled();
                if (ImGui::SmallButton(sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective ? "Persp##Proj" : "Ortho##Proj"))
                    sceneCamera.ToggleProjectionMode();
                if (!projectionToggleEnabled)
                    ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::SmallButton("Front##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetFrontView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Back##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetBackView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Right##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetRightView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Left##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetLeftView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Top##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetTopView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Bottom##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.SetBottomView();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Perspective##SceneView"))
                {
                    gfx.SetViewMode(ViewMode::Mode3D);
                    sceneCamera.ResetToDefaultView();
                }

                static char skyboxTextureBuffer[260] = {};
                static std::string skyboxTextureSource;
                static std::array<std::array<char, 260>, 6> skyboxCubemapBuffers{};
                static std::array<std::string, 6> skyboxCubemapSources;
                SceneSkyboxSettings skyboxSettings = Scene::GetSkyboxSettings();
                if (skyboxTextureSource != skyboxSettings.texturePath)
                {
                    skyboxTextureSource = skyboxSettings.texturePath;
                    strncpy_s(skyboxTextureBuffer, skyboxTextureSource.c_str(), _TRUNCATE);
                }
                for (size_t faceIndex = 0; faceIndex < skyboxCubemapSources.size(); ++faceIndex)
                {
                    if (skyboxCubemapSources[faceIndex] != skyboxSettings.cubemapFacePaths[faceIndex])
                    {
                        skyboxCubemapSources[faceIndex] = skyboxSettings.cubemapFacePaths[faceIndex];
                        strncpy_s(skyboxCubemapBuffers[faceIndex].data(), skyboxCubemapBuffers[faceIndex].size(), skyboxCubemapSources[faceIndex].c_str(), _TRUNCATE);
                    }
                }

                if (ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static constexpr const char* kCubemapFaceLabels[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
                    bool skyboxChanged = false;
                    static constexpr const char* kSkyboxBuiltInPresets[] = { "Day", "Sunset", "Night", "Void" };
                    skyboxChanged |= ImGui::Checkbox("Enabled##Skybox", &skyboxSettings.enabled);
                    const char* skyboxModeLabel = skyboxSettings.useCubemap ? "Cubemap" : "Equirect";
                    if (ImGui::BeginCombo("Mode##Skybox", skyboxModeLabel))
                    {
                        const bool usingEquirect = !skyboxSettings.useCubemap;
                        if (ImGui::Selectable("Equirect", usingEquirect))
                        {
                            skyboxSettings.useCubemap = false;
                            skyboxChanged = true;
                        }
                        if (usingEquirect)
                            ImGui::SetItemDefaultFocus();

                        const bool usingCubemap = skyboxSettings.useCubemap;
                        if (ImGui::Selectable("Cubemap", usingCubemap))
                        {
                            skyboxSettings.useCubemap = true;
                            skyboxChanged = true;
                        }
                        if (usingCubemap)
                            ImGui::SetItemDefaultFocus();
                        ImGui::EndCombo();
                    }
                    ImGui::Separator();

                    if (!skyboxSettings.useCubemap)
                    {
                        const char* builtInPresetLabel = skyboxSettings.builtInPreset.empty() ? "Sunset" : skyboxSettings.builtInPreset.c_str();
                        if (ImGui::BeginCombo("Built-In Preset##Skybox", builtInPresetLabel))
                        {
                            for (const char* presetLabel : kSkyboxBuiltInPresets)
                            {
                                const bool isCurrent = (_stricmp(builtInPresetLabel, presetLabel) == 0);
                                if (ImGui::Selectable(presetLabel, isCurrent))
                                {
                                    skyboxSettings.builtInPreset = presetLabel;
                                    skyboxChanged = true;
                                }
                                if (isCurrent)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        static bool s_SkyboxPresetExpanded = false;
                        ImGui::TextUnformatted("Built-In Presets");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(s_SkyboxPresetExpanded ? "Compact View##SkyboxPresets" : "Expanded View##SkyboxPresets"))
                            s_SkyboxPresetExpanded = !s_SkyboxPresetExpanded;

                        const ImVec2 presetThumbSize = s_SkyboxPresetExpanded ? ImVec2(144.0f, 72.0f) : ImVec2(96.0f, 48.0f);
                        const float presetSpacing = ImGui::GetStyle().ItemSpacing.x;
                        const float availableWidth = ImGui::GetContentRegionAvail().x;
                        const int presetColumns = (std::max)(1, static_cast<int>((availableWidth + presetSpacing) / (presetThumbSize.x + presetSpacing)));
                        ImDrawList* presetDrawList = ImGui::GetWindowDrawList();
                        if (ImGui::BeginTable("##SkyboxPresetTable", presetColumns, ImGuiTableFlags_SizingFixedFit))
                        {
                            for (size_t presetIndex = 0; presetIndex < _countof(kSkyboxBuiltInPresets); ++presetIndex)
                            {
                                ImGui::TableNextColumn();

                                const char* presetLabel = kSkyboxBuiltInPresets[presetIndex];
                                Texture* presetTexture = TextureManager::GetInstance().GetBuiltInSkyTexture(presetLabel);
                                const ImTextureID presetTextureId = (presetTexture && presetTexture->srvGPU.ptr != 0) ? (ImTextureID)presetTexture->srvGPU.ptr : 0;
                                const bool isCurrentPreset = (_stricmp(builtInPresetLabel, presetLabel) == 0) && skyboxSettings.texturePath.empty();

                                ImGui::PushID(presetLabel);
                                ImGui::BeginGroup();
                                const float groupStartX = ImGui::GetCursorPosX();
                                if (presetTextureId)
                                    ImGui::Image(presetTextureId, presetThumbSize, ImVec2(0, 0), ImVec2(1, 1));
                                else
                                    ImGui::Button("No Preview", presetThumbSize);

                                const ImVec2 thumbMin = ImGui::GetItemRectMin();
                                const ImVec2 thumbMax = ImGui::GetItemRectMax();
                                const bool presetClicked = ImGui::IsItemClicked();
                                presetDrawList->AddRect(
                                    thumbMin,
                                    thumbMax,
                                    isCurrentPreset ? IM_COL32(110, 190, 255, 255) : IM_COL32(90, 90, 90, 255),
                                    4.0f,
                                    0,
                                    isCurrentPreset ? 2.0f : 1.0f);
                                if (ImGui::IsItemHovered())
                                {
                                    const char* presetDescription = "Stylized built-in sky preset.";
                                    if (_stricmp(presetLabel, "Day") == 0)
                                        presetDescription = "Bright daytime gradient with a cool horizon.";
                                    else if (_stricmp(presetLabel, "Sunset") == 0)
                                        presetDescription = "Warm sunset gradient with a strong glow band.";
                                    else if (_stricmp(presetLabel, "Night") == 0)
                                        presetDescription = "Dark night sky with a subtle moonlit horizon.";
                                    else if (_stricmp(presetLabel, "Void") == 0)
                                        presetDescription = "Moody void sky with arcane purple-red tones.";

                                    ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 0.0f), ImVec2(320.0f, FLT_MAX));
                                    ImGui::BeginTooltip();
                                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 220.0f);
                                    ImGui::TextUnformatted(presetLabel);
                                    ImGui::Separator();
                                    ImGui::TextWrapped("%s", presetDescription);
                                    if (isCurrentPreset)
                                        ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), "Current built-in preset");
                                    ImGui::PopTextWrapPos();
                                    ImGui::EndTooltip();
                                }

                                const float labelWidth = ImGui::CalcTextSize(presetLabel).x;
                                ImGui::SetCursorPosX(groupStartX + (std::max)(0.0f, (presetThumbSize.x - labelWidth) * 0.5f));
                                ImGui::TextUnformatted(presetLabel);
                                ImGui::Dummy(ImVec2(presetThumbSize.x, 0.0f));
                                ImGui::EndGroup();
                                ImGui::PopID();

                                if (presetClicked)
                                {
                                    skyboxSettings.builtInPreset = presetLabel;
                                    skyboxSettings.texturePath.clear();
                                    skyboxTextureBuffer[0] = '\0';
                                    skyboxTextureSource.clear();
                                    skyboxChanged = true;
                                }
                            }
                            ImGui::EndTable();
                        }

                        ImGui::InputText("Texture##Skybox", skyboxTextureBuffer, IM_ARRAYSIZE(skyboxTextureBuffer), ImGuiInputTextFlags_ReadOnly);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Pick##Skybox"))
                            ImGui::OpenPopup("Skybox Texture Picker##Scene");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use Built-In##Skybox"))
                        {
                            skyboxTextureBuffer[0] = '\0';
                            skyboxTextureSource.clear();
                            skyboxSettings.texturePath.clear();
                            skyboxChanged = true;
                        }
                        if (!g_SelectedAssetId.empty() && IsTextureAssetPath(g_SelectedAssetId))
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Use Selected Asset##Skybox"))
                            {
                                strncpy_s(skyboxTextureBuffer, g_SelectedAssetId.c_str(), _TRUNCATE);
                                skyboxChanged = true;
                            }
                        }

                        if (ImGui::BeginPopup("Skybox Texture Picker##Scene"))
                        {
                            for (const std::filesystem::path& texturePath : EnumerateTextureAssets())
                            {
                                const std::string texturePathString = texturePath.string();
                                const bool isCurrent = (skyboxTextureSource == texturePathString);
                                if (ImGui::Selectable(texturePathString.c_str(), isCurrent))
                                {
                                    strncpy_s(skyboxTextureBuffer, texturePathString.c_str(), _TRUNCATE);
                                    skyboxChanged = true;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                            ImGui::EndPopup();
                        }
                    }
                    else
                    {
                        if (ImGui::Button("Clear All Faces##SkyboxCube"))
                        {
                            for (size_t faceIndex = 0; faceIndex < skyboxCubemapBuffers.size(); ++faceIndex)
                            {
                                std::array<char, 260>& buffer = skyboxCubemapBuffers[faceIndex];
                                buffer[0] = '\0';
                                skyboxCubemapSources[faceIndex].clear();
                                skyboxSettings.cubemapFacePaths[faceIndex].clear();
                            }
                            skyboxChanged = true;
                        }
                        for (size_t faceIndex = 0; faceIndex < skyboxCubemapBuffers.size(); ++faceIndex)
                        {
                            const std::string fieldLabel = std::format("{} Face##SkyboxCube", kCubemapFaceLabels[faceIndex]);
                            ImGui::InputText(fieldLabel.c_str(), skyboxCubemapBuffers[faceIndex].data(), static_cast<size_t>(skyboxCubemapBuffers[faceIndex].size()), ImGuiInputTextFlags_ReadOnly);
                            ImGui::SameLine();
                            const std::string pickLabel = std::format("Pick##SkyboxCube{}", faceIndex);
                            if (ImGui::SmallButton(pickLabel.c_str()))
                                ImGui::OpenPopup(std::format("Skybox Cubemap Picker##Scene{}", faceIndex).c_str());
                            ImGui::SameLine();
                            const std::string clearLabel = std::format("Clear##SkyboxCube{}", faceIndex);
                            if (ImGui::SmallButton(clearLabel.c_str()))
                            {
                                skyboxCubemapBuffers[faceIndex][0] = '\0';
                                skyboxCubemapSources[faceIndex].clear();
                                skyboxSettings.cubemapFacePaths[faceIndex].clear();
                                skyboxChanged = true;
                            }
                            if (!g_SelectedAssetId.empty() && IsTextureAssetPath(g_SelectedAssetId))
                            {
                                ImGui::SameLine();
                                const std::string selectedLabel = std::format("Use Selected##SkyboxCube{}", faceIndex);
                                if (ImGui::SmallButton(selectedLabel.c_str()))
                                {
                                    strncpy_s(skyboxCubemapBuffers[faceIndex].data(), skyboxCubemapBuffers[faceIndex].size(), g_SelectedAssetId.c_str(), _TRUNCATE);
                                    skyboxChanged = true;
                                }
                            }

                            const std::string popupId = std::format("Skybox Cubemap Picker##Scene{}", faceIndex);
                            if (ImGui::BeginPopup(popupId.c_str()))
                            {
                                for (const std::filesystem::path& texturePath : EnumerateTextureAssets())
                                {
                                    const std::string texturePathString = texturePath.string();
                                    const bool isCurrent = (skyboxCubemapSources[faceIndex] == texturePathString);
                                    if (ImGui::Selectable(texturePathString.c_str(), isCurrent))
                                    {
                                        strncpy_s(skyboxCubemapBuffers[faceIndex].data(), skyboxCubemapBuffers[faceIndex].size(), texturePathString.c_str(), _TRUNCATE);
                                        skyboxChanged = true;
                                        ImGui::CloseCurrentPopup();
                                    }
                                }
                                ImGui::EndPopup();
                            }
                        }
                    }
                    skyboxChanged |= ImGui::ColorEdit3("Tint Picker##Skybox", &skyboxSettings.tint.x, ImGuiColorEditFlags_Float);
                    skyboxChanged |= DrawTickFloat3("Tint", skyboxSettings.tint, 0.05f, 0.0f, 2.0f);
                    skyboxChanged |= DrawTickFloat("Intensity", skyboxSettings.intensity, 0.0f, 8.0f, 0.05f, "%.2f");
                    skyboxChanged |= DrawTickFloat("Exposure", skyboxSettings.exposure, -8.0f, 8.0f, 0.10f, "%.2f EV");
                    skyboxChanged |= DrawTickFloat("Rotation", skyboxSettings.rotationDegrees, -360.0f, 360.0f, 1.0f, "%.0f deg");
                    skyboxChanged |= DrawTickFloat("Vertical Rotation", skyboxSettings.verticalRotationDegrees, -89.0f, 89.0f, 1.0f, "%.0f deg");
                    if (!skyboxSettings.useCubemap)
                        skyboxChanged |= DrawTickFloat("Horizon Offset", skyboxSettings.horizonOffset, -0.5f, 0.5f, 0.01f, "%.2f");
                    if (ImGui::SmallButton("Reset Skybox Controls##Skybox"))
                    {
                        skyboxSettings.tint = { 1.0f, 1.0f, 1.0f };
                        skyboxSettings.intensity = 1.0f;
                        skyboxSettings.exposure = 0.0f;
                        skyboxSettings.rotationDegrees = 0.0f;
                        skyboxSettings.verticalRotationDegrees = 0.0f;
                        skyboxSettings.horizonOffset = 0.0f;
                        skyboxChanged = true;
                    }

                    if (skyboxChanged)
                    {
                        if (g_engineInstance)
                            PushUndoSnapshot(g_engineInstance->GetEditorState());
                        skyboxSettings.texturePath = skyboxTextureBuffer;
                        for (size_t faceIndex = 0; faceIndex < skyboxCubemapBuffers.size(); ++faceIndex)
                            skyboxSettings.cubemapFacePaths[faceIndex] = skyboxCubemapBuffers[faceIndex].data();
                        Scene::SetSkyboxSettings(skyboxSettings);
                        skyboxTextureSource = skyboxSettings.texturePath;
                        for (size_t faceIndex = 0; faceIndex < skyboxCubemapSources.size(); ++faceIndex)
                            skyboxCubemapSources[faceIndex] = skyboxSettings.cubemapFacePaths[faceIndex];
                    }

                    ImGui::TextDisabled(skyboxSettings.useCubemap
                        ? "Cubemap order: +X, -X, +Y, -Y, +Z, -Z. Vertical Rotation applies here too."
                        : "Uses a 2D equirect sky texture path. Empty path uses the built-in sky. Horizon Offset is equirect-only.");
                }

                ImVec2 size = ImGui::GetContentRegionAvail();

                // Quantize to integer pixels and stabilize against 1px docking/padding jitter.
                const int rawW = (int)std::floor(size.x);
                const int rawH = (int)std::floor(size.y);

                static int s_lastRequestedW = 0;
                static int s_lastRequestedH = 0;

                int reqW = (rawW > 1) ? rawW : 1;
                int reqH = (rawH > 1) ? rawH : 1;

                if (s_lastRequestedW != 0 && s_lastRequestedH != 0)
                {
                    if (std::abs(reqW - s_lastRequestedW) <= 1) reqW = s_lastRequestedW;
                    if (std::abs(reqH - s_lastRequestedH) <= 1) reqH = s_lastRequestedH;
                }

                const UINT w = (UINT)reqW;
                const UINT h = (UINT)reqH;

                if ((int)w != s_lastRequestedW || (int)h != s_lastRequestedH)
                {
                    s_lastRequestedW = (int)w;
                    s_lastRequestedH = (int)h;
                    gfx.RequestSceneRenderTargetResize(w, h, ResizeSource::User);
                }

                ImTextureID tex = gfx.GetSceneImGuiTextureID();
                const bool shouldLogSceneDiag = ScenePanelDiag_ShouldLog();
                if (shouldLogSceneDiag)
                {
                    Logger::Log(LogLevel::Debug, std::format(
                        "[ScenePanel] SceneRT TexID=0x{:X} ViewportSize=({:.1f},{:.1f}) RTSize=({:.0f},{:.0f})",
                        (UINT64)tex,
                        size.x, size.y,
                        gfx.GetSceneRenderTargetSize().x, gfx.GetSceneRenderTargetSize().y), "[Editor]");
                }
                if (tex)
                {
                    // Scene render target displayed in native orientation so overlay/gizmo projection matches it.
                    ImGui::Image(tex, size, ImVec2(0, 0), ImVec2(1, 1));

                    const bool hovered = ImGui::IsItemHovered();
                    const bool viewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

                    // Cache scene image rect for picking
                    const ImVec2 sceneMin = ImGui::GetItemRectMin();
                    const ImVec2 sceneMax = ImGui::GetItemRectMax();
                    ImDrawList* sceneDrawList = ImGui::GetWindowDrawList();
                    sceneDrawList->PushClipRect(sceneMin, sceneMax, true);

                    extern Engine* g_engineInstance;
                    EditorState& editor = g_engineInstance->GetEditorState();

                    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                    {
                        const ImVec2 rightDragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                        const float rightDragDistanceSq = rightDragDelta.x * rightDragDelta.x + rightDragDelta.y * rightDragDelta.y;
                        if (rightDragDistanceSq < 16.0f && !g_EditorGizmo.IsDragging())
                            ImGui::OpenPopup("Scene Context##SceneViewport");
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        CameraData dropCamera{};
                        DirectX::XMFLOAT3 spawnPos{ 0.0f, 0.5f, 0.0f };
                        if (Scene::TryGetLastRenderCameraData(dropCamera))
                            spawnPos = ComputeSceneDropSpawnPosition(dropCamera, sceneMin, size, ImGui::GetMousePos());

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMaterialPayload))
                        {
                            if (payload->DataSize == sizeof(int))
                            {
                                const int materialIndex = *static_cast<const int*>(payload->Data);
                                SceneInstance* dropTarget = nullptr;
                                const uint32_t hoveredInstanceId = Scene::GetHoveredInstanceId();
                                if (hoveredInstanceId != 0)
                                    dropTarget = FindSceneInstanceById(hoveredInstanceId);
                                if (!dropTarget)
                                    dropTarget = Scene::GetSelectedInstance();
                                if (ApplyMaterialToSceneInstance(editor, dropTarget, materialIndex))
                                    editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                        }

                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateEmptyPayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateEmpty({ spawnPos.x, 0.0f, spawnPos.z });
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateCubePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCube(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateSpherePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateSphere(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreatePlanePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            const DirectX::XMFLOAT3 planeSpawnPos{ spawnPos.x, 0.0f, spawnPos.z };
                            Scene::CreatePlane(planeSpawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateCylinderPayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCylinder(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateCapsulePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCapsule({ spawnPos.x, spawnPos.y + 0.25f, spawnPos.z });
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateTorusPayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateTorus(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreateConePayload))
                        {
                            (void)payload;
                            PushUndoSnapshot(editor);
                            Scene::CreateCone(spawnPos);
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneCreatePrefabPayload))
                        {
                            const char* prefabPath = static_cast<const char*>(payload->Data);
                            if (prefabPath && *prefabPath)
                            {
                                PushUndoSnapshot(editor);
                                Scene::InstantiatePrefab(prefabPath, spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginPopup("Scene Context##SceneViewport"))
                    {
                        CameraData contextCamera{};
                        DirectX::XMFLOAT3 spawnPos{ 0.0f, 0.5f, 0.0f };
                        if (Scene::TryGetLastRenderCameraData(contextCamera))
                            spawnPos = ComputeSceneDropSpawnPosition(contextCamera, sceneMin, size, ImGui::GetMousePos());

                        const bool canUndo = CanUndoCommand();
                        const bool canRedo = CanRedoCommand();
                        const bool hasSelection = (Scene::GetSelectedInstance() != nullptr);
                        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                            ExecuteUndoCommand();
                        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                            ExecuteRedoCommand();
                        if (canUndo || canRedo)
                            ImGui::Separator();
                        if (ImGui::MenuItem("Focus", "F", false, hasSelection))
                        {
                            DirectX::XMFLOAT3 focusPoint{};
                            if (TryGetCurrentSelectionAnchor(editor, focusPoint))
                                gfx.GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
                        }
                        if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
                        {
                            if (SceneInstance* selected = Scene::GetSelectedInstance())
                            {
                                g_PendingSceneInstanceRenameId = selected->instanceId;
                                strncpy_s(g_SceneInstanceRenameBuffer, selected->name.c_str(), _TRUNCATE);
                                g_RequestOpenSceneInstanceRenamePopup = true;
                            }
                        }
                        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
                        {
                            if (ExecuteEditorCommand(editor, std::make_unique<DuplicateSelectionCommand>(Scene::GetSelectedInstanceIds())))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
                        {
                            DeleteCurrentSelection(editor);
                        }
                        if (ImGui::MenuItem("Create Empty Parent", nullptr, false, hasSelection))
                            CreateEmptyParentForSelection(editor);
                        if (ImGui::MenuItem("Save Prefab", nullptr, false, hasSelection))
                            SaveCurrentSelectionAsPrefab(editor);
                        if (ImGui::MenuItem("Unpack Prefab", nullptr, false, hasSelection && SelectionContainsPrefabInstance()))
                        {
                            if (ExecuteEditorCommand(editor, std::make_unique<UnpackPrefabCommand>(Scene::GetSelectedInstanceIds())))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }

                        ImGui::Separator();
                        if (ImGui::BeginMenu("Create"))
                        {
                            if (ImGui::MenuItem("Empty Object"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateEmpty({ spawnPos.x, 0.0f, spawnPos.z });
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Cube"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateCube(spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Sphere"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateSphere(spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Plane"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreatePlane({ spawnPos.x, 0.0f, spawnPos.z });
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Cylinder"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateCylinder(spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Capsule"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateCapsule({ spawnPos.x, spawnPos.y + 0.25f, spawnPos.z });
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Torus"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateTorus(spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            if (ImGui::MenuItem("Cone"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::CreateCone(spawnPos);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            ImGui::EndMenu();
                        }

                        ImGui::EndPopup();
                    }

                    const char* modeText = "Translate";
                    switch (g_EditorGizmo.GetMode())
                    {
                    case EditorGizmo::Mode::Rotate: modeText = "Rotate"; break;
                    case EditorGizmo::Mode::Scale: modeText = "Scale"; break;
                    case EditorGizmo::Mode::Translate:
                    default: modeText = "Translate"; break;
                    }

                    const char* spaceText = (g_EditorGizmo.GetSpace() == EditorGizmo::Space::World) ? "World" : "Local";
                    const char* pivotText = (editor.gizmoPivotMode == GizmoPivotMode::Center) ? "Center" : "Pivot";
                    const char* projectionText = (sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective) ? "Perspective" : "Orthographic";
                    const char* viewText = sceneCamera.Is2DMode() ? "2D" : "3D";

                    const bool freelooking = gfx.GetSceneCamera().IsFreelooking();
                    char gizmoHud[320] = {};
                    if (g_EditorGizmo.HasActiveRotationFeedback())
                    {
                        const char axisChar = (g_EditorGizmo.GetActiveAxis() == 0) ? 'X' : (g_EditorGizmo.GetActiveAxis() == 1) ? 'Y' : 'Z';
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nPivot: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s\nRotate %c: %.1f deg",
                            modeText,
                            spaceText,
                            pivotText,
                            ImGui::GetIO().KeyCtrl ? "ON" : "OFF",
                            freelooking ? "Fly" : (sceneCamera.Is2DMode() ? "Pan" : "Idle"),
                            projectionText,
                            viewText,
                            axisChar,
                            g_EditorGizmo.GetActiveRotationDegrees());
                    }
                    else
                    {
                        snprintf(gizmoHud, sizeof(gizmoHud), "Mode: %s\nSpace: %s\nPivot: %s\nSnap: %s (1.0)\nCamera: %s\nProjection: %s\nView: %s",
                            modeText,
                            spaceText,
                            pivotText,
                            ImGui::GetIO().KeyCtrl ? "ON" : "OFF",
                            freelooking ? "Fly" : (sceneCamera.Is2DMode() ? "Pan" : "Idle"),
                            projectionText,
                            viewText);
                    }

                    const ImVec2 hudSize = ImGui::CalcTextSize(gizmoHud, nullptr, false);
                    const ImVec2 overlayPos = { sceneMin.x + 10.0f, sceneMin.y + 10.0f };
                    ImDrawList* overlayDraw = sceneDrawList;
                    overlayDraw->AddRectFilled(
                        ImVec2(overlayPos.x - 6.0f, overlayPos.y - 6.0f),
                        ImVec2(overlayPos.x + hudSize.x + 6.0f, overlayPos.y + hudSize.y + 10.0f),
                        IM_COL32(0, 0, 0, 160),
                        4.0f);
                    overlayDraw->AddText(overlayPos, IM_COL32(255, 255, 255, 255), gizmoHud);

                    ImGui::SetCursorScreenPos(ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f));
                    if (ImGui::Button(g_EditorGizmo.GetSpace() == EditorGizmo::Space::World ? "World##GizmoSpace" : "Local##GizmoSpace", ImVec2(74.0f, 0.0f)))
                        g_EditorGizmo.ToggleSpace();
                    ImGui::SameLine();
                    if (ImGui::Button(editor.gizmoPivotMode == GizmoPivotMode::Center ? "Center##GizmoPivot" : "Pivot##GizmoPivot", ImVec2(78.0f, 0.0f)))
                        editor.gizmoPivotMode = (editor.gizmoPivotMode == GizmoPivotMode::Center) ? GizmoPivotMode::Pivot : GizmoPivotMode::Center;

                    if (focused && !Engine::IsKeyboardCapturedByUI())
                    {
                        if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
                        {
                            if (PerformRedo(editor))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
                        {
                            if (PerformUndo(editor))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
                        {
                            if (PerformRedo(editor))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Home))
                        {
                            gfx.SetViewMode(ViewMode::Mode3D);
                            sceneCamera.ResetToDefaultView();
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_W))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Translate);
                        if (ImGui::IsKeyPressed(ImGuiKey_E))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Rotate);
                        if (ImGui::IsKeyPressed(ImGuiKey_R))
                            g_EditorGizmo.SetMode(EditorGizmo::Mode::Scale);

                        if (ImGui::IsKeyPressed(ImGuiKey_F))
                        {
                            DirectX::XMFLOAT3 focusPoint{};
                            if (TryGetCurrentSelectionAnchor(editor, focusPoint))
                                gfx.GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
                        {
                            DeleteCurrentSelection(editor);
                        }

                        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
                        {
                            if (ExecuteEditorCommand(editor, std::make_unique<DuplicateSelectionCommand>(Scene::GetSelectedInstanceIds())))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                    }

                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool navigatingCamera = IsSceneCameraNavigating(editor);

                        const bool allowCameraInput = hovered && focused && !g_EditorGizmo.IsDragging() && !Engine::IsKeyboardCapturedByUI();
                        gfx.SetSceneViewportCameraInputState(hovered, focused, allowCameraInput);

                        CameraData gizmoCamera{};
                        const float hudButtonHeight = ImGui::GetFrameHeight();
                        const ImVec2 hudButtonMin = ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f);
                        const ImVec2 hudButtonMax = ImVec2(hudButtonMin.x + 160.0f, hudButtonMin.y + hudButtonHeight);
                        const bool overHudButton = io.MousePos.x >= hudButtonMin.x && io.MousePos.x <= hudButtonMax.x &&
                            io.MousePos.y >= hudButtonMin.y && io.MousePos.y <= hudButtonMax.y;
                        const bool allowGizmoInput = hovered && focused && !navigatingCamera && !overHudButton;
                        if (Scene::TryGetLastRenderCameraData(gizmoCamera))
                        {
                            DrawSceneCameraIcons(gizmoCamera, sceneDrawList, sceneMin, size);

                            if (SceneInstance* selected = Scene::GetSelectedInstance())
                            {
                                const bool wasDragging = g_EditorGizmo.IsDragging();
                                g_EditorGizmo.Update(
                                    selected,
                                    gizmoCamera,
                                    sceneDrawList,
                                    sceneMin,
                                    size,
                                    hovered,
                                    allowGizmoInput,
                                    editor.gizmoPivotMode == GizmoPivotMode::Center);

                                if (!wasDragging && g_EditorGizmo.IsDragging())
                                    PushUndoSnapshot(editor);
                                g_WasGizmoDragging = g_EditorGizmo.IsDragging();

                                editor.selection.position = TryGetCurrentSelectionAnchor(editor, editor.selection.position)
                                    ? editor.selection.position
                                    : Scene::GetSelectionCenterOrActivePosition();
                            }
                            else
                            {
                                g_WasGizmoDragging = false;
                            }
                        }
                    }

                    // Scene picking (LMB) - preserve camera-nav priority (RMB freelook)
                    if (hovered)
                    {
                        ImGuiIO& io = ImGui::GetIO();
                        const bool rmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
                        const bool mmbDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
                        const bool navigatingCamera = IsSceneCameraNavigating(editor);
                        const ImVec2 hudButtonMin = ImVec2(overlayPos.x, overlayPos.y + hudSize.y + 18.0f);
                        const ImVec2 hudButtonMax = ImVec2(hudButtonMin.x + 160.0f, hudButtonMin.y + ImGui::GetFrameHeight());
                        const bool overHudButton = io.MousePos.x >= hudButtonMin.x && io.MousePos.x <= hudButtonMax.x &&
                            io.MousePos.y >= hudButtonMin.y && io.MousePos.y <= hudButtonMax.y;
                        const bool clickingGizmo = g_EditorGizmo.IsDragging() || g_EditorGizmo.HasHoveredHandle();

                        const float localMouseX = io.MousePos.x - sceneMin.x;
                        const float localMouseY = io.MousePos.y - sceneMin.y;
                        if (!navigatingCamera && !overHudButton && !clickingGizmo)
                            Scene::TryHoverInstanceAtViewportPoint(localMouseX, localMouseY, size.x, size.y);
                        else
                            Scene::ClearHoveredInstance();

                        DrawSceneMaterialDropOverlay(sceneDrawList, sceneMin, size);

                        if (!navigatingCamera && !overHudButton && !clickingGizmo && viewportClicked && !rmbDown && !mmbDown)
                        {
                            const bool shiftSelect = io.KeyShift;
                            if (Scene::TrySelectInstanceAtViewportPoint(localMouseX, localMouseY, size.x, size.y, shiftSelect, shiftSelect))
                            {
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();

                                Logger::Log(LogLevel::Info, std::format("Selected instance {}", Scene::GetSelectedInstanceIndex()), "Editor");
                            }
                            else if (!shiftSelect)
                            {
                                editor.selection.Clear();
                                Logger::Log(LogLevel::Info, "Selection cleared", "Editor");
                            }
                        }
                    }
                    else
                    {
                        Scene::ClearHoveredInstance();
                    }
                }
                else
                {
                    gfx.SetSceneViewportCameraInputState(false, false, false);
                    if (shouldLogSceneDiag)
                    {
                        Logger::Log(LogLevel::Debug, "[ScenePanel] Scene render target not ready for ImGui::Image", "[Editor]");
                    }
                    ImGui::TextUnformatted("Scene render target not ready...");
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_game{
        "Game",
        true,
        []()
        {
            auto& panel = Game();
            if (!panel.open)
                return;

            static bool s_GameMouseLocked = false;

            auto releaseGameMouseLock = [&]()
            {
                if (!s_GameMouseLocked)
                    return;
                ClipCursor(nullptr);
                ReleaseCapture();
                s_GameMouseLocked = false;
            };

            if (ImGui::Begin(panel.name, &panel.open))
            {
                Graphics& gfx = Graphics::GetInstance();
                ImVec2 size = ImGui::GetContentRegionAvail();
                const int rawW = (int)std::floor(size.x);
                const int rawH = (int)std::floor(size.y);

                static int s_lastRequestedGameW = 0;
                static int s_lastRequestedGameH = 0;

                int reqW = (rawW > 1) ? rawW : 1;
                int reqH = (rawH > 1) ? rawH : 1;

                if (s_lastRequestedGameW != 0 && s_lastRequestedGameH != 0)
                {
                    if (std::abs(reqW - s_lastRequestedGameW) <= 1) reqW = s_lastRequestedGameW;
                    if (std::abs(reqH - s_lastRequestedGameH) <= 1) reqH = s_lastRequestedGameH;
                }

                const UINT w = (UINT)reqW;
                const UINT h = (UINT)reqH;
                if ((int)w != s_lastRequestedGameW || (int)h != s_lastRequestedGameH)
                {
                    s_lastRequestedGameW = (int)w;
                    s_lastRequestedGameH = (int)h;
                    gfx.RequestGameRenderTargetResize(w, h, ResizeSource::User);
                }

                const Engine::State state = Engine::GetState();
                const bool runtimeActive = (state == Engine::State::Playing || state == Engine::State::Paused);
                const ImTextureID tex = gfx.GetGameImGuiTextureID();
                bool hovered = false;
                bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                ImVec2 imageMin{};
                ImVec2 imageMax{};

                if (runtimeActive && tex)
                {
                    ImGui::Image(tex, size, ImVec2(0, 0), ImVec2(1, 1));
                    hovered = ImGui::IsItemHovered();
                    imageMin = ImGui::GetItemRectMin();
                    imageMax = ImGui::GetItemRectMax();
                    ImDrawList* gameDraw = ImGui::GetWindowDrawList();
                    DrawGameInteractionTargetHighlight(ImGui::GetWindowDrawList(), imageMin, size);

                    ImVec4 moodAccent(1.0f, 0.77f, 0.46f, 1.0f);
                    ImVec4 moodSecondary(0.70f, 0.36f, 1.0f, 1.0f);
                    if (g_engineInstance)
                    {
                        float r = 1.0f;
                        float g = 0.77f;
                        float b = 0.46f;
                        g_engineInstance->GetGame().GetVaultMoodAccentColor(r, g, b);
                        moodAccent = ImVec4(r, g, b, 1.0f);

                        r = 0.70f;
                        g = 0.36f;
                        b = 1.0f;
                        g_engineInstance->GetGame().GetVaultMoodSecondaryColor(r, g, b);
                        moodSecondary = ImVec4(r, g, b, 1.0f);
                    }

                    auto colorFromMood = [](const ImVec4& color, float intensity, int alpha = 255)
                    {
                        return IM_COL32(
                            static_cast<int>(std::clamp(color.x * intensity, 0.0f, 1.0f) * 255.0f),
                            static_cast<int>(std::clamp(color.y * intensity, 0.0f, 1.0f) * 255.0f),
                            static_cast<int>(std::clamp(color.z * intensity, 0.0f, 1.0f) * 255.0f),
                            alpha);
                    };
                    struct HudLine
                    {
                        std::string text;
                        ImU32 color = IM_COL32_WHITE;
                    };

                    std::vector<HudLine> hudLines;
                    hudLines.reserve(14);
                    hudLines.push_back({ state == Engine::State::Paused ? "Game View (Paused)" : "Game View (Runtime)", IM_COL32(210, 218, 232, 255) });
                    hudLines.push_back({ "Controls: RMB=Look  WASD=Move  Shift=Sprint  E=Interact", IM_COL32(162, 173, 188, 255) });
                    hudLines.push_back({ "Controller: LS=Move  RS=Look  RT=Sprint  A=Interact", IM_COL32(142, 160, 182, 255) });
                    hudLines.push_back({ std::format("Input: hovered={} focused={} allow={}", hovered ? 1 : 0, focused ? 1 : 0, (runtimeActive && hovered && focused && !Engine::IsKeyboardCapturedByUI()) ? 1 : 0), IM_COL32(126, 141, 160, 255) });
                    if (g_engineInstance)
                    {
                        const ::Game& game = g_engineInstance->GetGame();
                        const VaultMissionState missionState = game.GetVaultMissionState();
                        if (game.IsTutorialVault())
                        {
                            if (const char* tutorialHeader = game.GetVaultTutorialHeader())
                                hudLines.push_back({ tutorialHeader, IM_COL32(255, 196, 116, 255) });
                            if (const char* tutorialPrimary = game.GetVaultTutorialHintPrimary())
                                hudLines.push_back({ tutorialPrimary, IM_COL32(238, 245, 255, 255) });
                            if (const char* tutorialSecondary = game.GetVaultTutorialHintSecondary())
                                hudLines.push_back({ tutorialSecondary, IM_COL32(176, 192, 214, 255) });
                        }
                        hudLines.push_back({ game.GetVaultMissionObjectiveText(), IM_COL32(184, 219, 255, 255) });
                        hudLines.push_back({ std::format("Vault: {} / {} nodes | Core: {}",
                            game.GetVaultActiveNodeCount(),
                            game.GetVaultTotalNodeCount(),
                            game.IsVaultCoreUnlocked() ? "Unlocked" : "Locked"), IM_COL32(184, 219, 255, 255) });
                        if (game.HasVaultContextHint())
                        {
                            const float hintAlpha = game.GetVaultContextHintAlpha();
                            if (const char* hintTitle = game.GetVaultContextHintTitle())
                                hudLines.push_back({ hintTitle, IM_COL32(255, 196, 116, static_cast<int>(255.0f * hintAlpha)) });
                            if (const char* hintText = game.GetVaultContextHintText())
                                hudLines.push_back({ hintText, IM_COL32(224, 232, 244, static_cast<int>(255.0f * hintAlpha)) });
                        }
                        if (missionState == VaultMissionState::Completed)
                            hudLines.push_back({ "Exit Open - Reach the Gate", IM_COL32(143, 255, 184, 255) });
                        else if (missionState == VaultMissionState::Failed)
                            hudLines.push_back({ "System Failure - Press R to Retry", IM_COL32(255, 107, 82, 255) });
                    }
                    if (g_engineInstance && g_engineInstance->GetGame().HasInteractionTarget())
                        hudLines.push_back({ g_engineInstance->GetGame().GetInteractionPrompt(), IM_COL32(255, 235, 115, 255) });
                    if (g_engineInstance && g_engineInstance->GetGame().HasVaultWarning())
                        hudLines.push_back({ g_engineInstance->GetGame().GetVaultWarningText(), IM_COL32(255, 107, 82, 255) });
                    if (g_engineInstance && g_engineInstance->GetGame().IsVaultMissionEscaped())
                    {
                        hudLines.push_back({ "Vault Escaped!", IM_COL32(143, 255, 184, 255) });
                        if (const char* nextVaultText = g_engineInstance->GetGame().GetNextVaultActionText())
                            hudLines.push_back({ nextVaultText, IM_COL32(184, 219, 255, 255) });
                    }
                    else if (g_engineInstance && g_engineInstance->GetGame().IsVaultMissionFailed())
                        hudLines.push_back({ "Vault Failed!", IM_COL32(255, 107, 82, 255) });

                    float maxHudWidth = 0.0f;
                    for (const HudLine& line : hudLines)
                        maxHudWidth = (std::max)(maxHudWidth, ImGui::CalcTextSize(line.text.c_str()).x);

                    const float hudPaddingX = 14.0f;
                    const float hudPaddingY = 12.0f;
                    const float hudLineAdvance = ImGui::GetTextLineHeightWithSpacing();
                    const float hudAccentHeight = 4.0f;
                    const float hudWidth = maxHudWidth + hudPaddingX * 2.0f;
                    const float hudHeight = hudPaddingY * 2.0f + hudAccentHeight + hudLineAdvance * static_cast<float>(hudLines.size());
                    const ImVec2 hudMin(imageMin.x + 10.0f, imageMin.y + 10.0f);
                    const ImVec2 hudMax(hudMin.x + hudWidth, hudMin.y + hudHeight);

                    gameDraw->AddRectFilled(
                        ImVec2(hudMin.x + 3.0f, hudMin.y + 4.0f),
                        ImVec2(hudMax.x + 3.0f, hudMax.y + 4.0f),
                        IM_COL32(0, 0, 0, 90),
                        10.0f);
                    gameDraw->AddRectFilledMultiColor(
                        hudMin,
                        ImVec2(hudMax.x, hudMin.y + hudAccentHeight),
                        colorFromMood(moodAccent, 1.0f, 235),
                        colorFromMood(moodSecondary, 1.0f, 220),
                        colorFromMood(moodSecondary, 1.0f, 220),
                        colorFromMood(moodAccent, 1.0f, 235));
                    gameDraw->AddRectFilled(
                        ImVec2(hudMin.x, hudMin.y + hudAccentHeight),
                        hudMax,
                        IM_COL32(8, 10, 18, 198),
                        10.0f);
                    gameDraw->AddRect(
                        hudMin,
                        hudMax,
                        colorFromMood(moodSecondary, 0.78f, 210),
                        10.0f,
                        0,
                        1.2f);

                    float hudTextY = hudMin.y + hudPaddingY + hudAccentHeight;
                    const float hudTextX = hudMin.x + hudPaddingX;
                    for (const HudLine& line : hudLines)
                    {
                        gameDraw->AddText(ImVec2(hudTextX + 1.0f, hudTextY + 1.0f), IM_COL32(0, 0, 0, 160), line.text.c_str());
                        gameDraw->AddText(ImVec2(hudTextX, hudTextY), line.color, line.text.c_str());
                        hudTextY += hudLineAdvance;
                    }

                    if (g_engineInstance)
                    {
                        const ::Game& game = g_engineInstance->GetGame();
                        gameDraw->PushClipRect(imageMin, imageMax, true);

                        if (const char* progressionLabel = game.GetVaultProgressionLabel())
                        {
                            const ImVec2 labelSize = ImGui::CalcTextSize(progressionLabel);
                            const ImVec2 labelPos(imageMax.x - labelSize.x - 22.0f, imageMin.y + 14.0f);
                            gameDraw->AddRectFilled(
                                ImVec2(labelPos.x - 10.0f, labelPos.y - 6.0f),
                                ImVec2(labelPos.x + labelSize.x + 10.0f, labelPos.y + labelSize.y + 6.0f),
                                IM_COL32(8, 12, 22, 180),
                                6.0f);
                            gameDraw->AddText(labelPos, colorFromMood(moodSecondary, 1.0f), progressionLabel);

                            if (game.HasVaultScannerTarget())
                            {
                                const float scannerWidth = 236.0f;
                                const float scannerHeight = 194.0f;
                                const ImVec2 scannerMin(imageMax.x - scannerWidth - 18.0f, labelPos.y + 26.0f);
                                const ImVec2 scannerMax(scannerMin.x + scannerWidth, scannerMin.y + scannerHeight);
                                const ImVec2 scannerCenter(scannerMin.x + scannerWidth * 0.5f, scannerMin.y + 76.0f);
                                const float relativeAngle = game.GetVaultScannerDirectionAngleRadians();
                                const float arrowAngle = relativeAngle - DirectX::XM_PIDIV2;
                                const float cosA = std::cos(arrowAngle);
                                const float sinA = std::sin(arrowAngle);
                                const float strength = game.GetVaultScannerStrength();
                                const float scannerTime = static_cast<float>(ImGui::GetTime());
                                const float pulse = 0.5f + 0.5f * std::sin(scannerTime * (2.8f + strength * 2.2f));
                                const float audioPulse = 0.5f + 0.5f * std::sin(scannerTime * (3.6f + strength * 4.8f));
                                const float pulseRadius = 12.0f + pulse * 18.0f;
                                const float sweepAngle = scannerTime * (0.9f + strength * 1.6f) - DirectX::XM_PIDIV2;
                                const std::string distanceText = std::format("{:.1f}m", game.GetVaultScannerDistance());
                                const std::string strengthText = std::format("Strength {:0.0f}%%", strength * 100.0f);

                                gameDraw->AddRectFilled(ImVec2(scannerMin.x + 3.0f, scannerMin.y + 4.0f), ImVec2(scannerMax.x + 3.0f, scannerMax.y + 4.0f), IM_COL32(0, 0, 0, 80), 10.0f);
                                gameDraw->AddRectFilledMultiColor(scannerMin, ImVec2(scannerMax.x, scannerMin.y + 4.0f), colorFromMood(moodAccent, 1.0f, 225), colorFromMood(moodSecondary, 1.0f, 210), colorFromMood(moodSecondary, 1.0f, 210), colorFromMood(moodAccent, 1.0f, 225));
                                gameDraw->AddRectFilled(ImVec2(scannerMin.x, scannerMin.y + 4.0f), scannerMax, IM_COL32(8, 10, 18, 198), 10.0f);
                                gameDraw->AddRect(scannerMin, scannerMax, colorFromMood(moodSecondary, 0.78f, 210), 10.0f, 0, 1.2f);

                                if (const char* scannerLabel = game.GetVaultScannerTargetLabel())
                                    gameDraw->AddText(ImVec2(scannerMin.x + 14.0f, scannerMin.y + 14.0f), colorFromMood(moodSecondary, 1.0f), scannerLabel);

                                for (int sweepTrail = 0; sweepTrail < 4; ++sweepTrail)
                                {
                                    const float trailAngle = sweepAngle - 0.22f * static_cast<float>(sweepTrail);
                                    const float trailAlpha = (1.0f - 0.22f * static_cast<float>(sweepTrail)) * (0.16f + 0.20f * strength);
                                    const ImVec2 sweepEnd(
                                        scannerCenter.x + std::cos(trailAngle) * 28.0f,
                                        scannerCenter.y + std::sin(trailAngle) * 28.0f);
                                    gameDraw->AddLine(scannerCenter, sweepEnd, colorFromMood(moodSecondary, 1.0f, static_cast<int>(255.0f * trailAlpha)), 1.4f - 0.15f * static_cast<float>(sweepTrail));
                                }

                                gameDraw->AddCircle(scannerCenter, pulseRadius, colorFromMood(moodAccent, 1.0f, static_cast<int>(22.0f + 78.0f * strength * (1.0f - pulse * 0.35f))), 40, 1.8f);
                                gameDraw->AddCircle(scannerCenter, 28.0f, colorFromMood(moodSecondary, 0.78f, 220), 32, 1.0f);
                                gameDraw->AddCircle(scannerCenter, 18.0f + pulse * 4.0f, colorFromMood(moodSecondary, 1.0f, static_cast<int>(36.0f + 44.0f * strength)), 32, 1.0f);
                                gameDraw->AddCircleFilled(scannerCenter, 3.5f + audioPulse * 1.4f, colorFromMood(moodAccent, 1.0f, static_cast<int>(185.0f + 70.0f * audioPulse)));

                                const auto rotatePoint = [&](float localX, float localY)
                                {
                                    return ImVec2(
                                        scannerCenter.x + localX * cosA - localY * sinA,
                                        scannerCenter.y + localX * sinA + localY * cosA);
                                };
                                const ImVec2 tip = rotatePoint(0.0f, -22.0f);
                                const ImVec2 left = rotatePoint(-10.0f, 11.0f);
                                const ImVec2 right = rotatePoint(10.0f, 11.0f);
                                gameDraw->AddTriangleFilled(tip, left, right, colorFromMood(moodAccent, 1.0f));

                                const ImVec2 distancePos(scannerMin.x + 16.0f, scannerMin.y + 134.0f);
                                const ImVec2 strengthPos(scannerMin.x + 16.0f, scannerMin.y + 156.0f);
                                const ImVec2 meterMin(scannerMin.x + 16.0f, scannerMin.y + 172.0f);
                                const ImVec2 meterMax(scannerMax.x - 16.0f, scannerMin.y + 184.0f);
                                gameDraw->AddText(distancePos, IM_COL32(238, 245, 255, 255), distanceText.c_str());
                                gameDraw->AddText(strengthPos, IM_COL32(176, 192, 214, static_cast<int>(168.0f + 87.0f * (0.4f + 0.6f * audioPulse) * strength)), strengthText.c_str());
                                gameDraw->AddRectFilled(meterMin, meterMax, IM_COL32(18, 24, 38, 200), 4.0f);
                                const float meterFill = (meterMax.x - meterMin.x - 2.0f) * strength;
                                gameDraw->AddRectFilled(
                                    ImVec2(meterMin.x + 1.0f, meterMin.y + 1.0f),
                                    ImVec2(meterMin.x + 1.0f + meterFill, meterMax.y - 1.0f),
                                    colorFromMood(moodAccent, 1.0f, static_cast<int>(120.0f + 110.0f * audioPulse)),
                                    3.0f);
                            }
                        }

                        if (game.HasVaultFailPulse())
                        {
                            const float pulseAlpha = game.GetVaultFailPulseAlpha();
                            gameDraw->AddRectFilled(
                                imageMin,
                                imageMax,
                                IM_COL32(130, 18, 18, static_cast<int>(85.0f * pulseAlpha)));
                        }

                        if (game.HasVaultPresentationBanner())
                        {
                            const char* bannerText = game.GetVaultPresentationBannerText();
                            const float bannerAlpha = game.GetVaultPresentationBannerAlpha();
                            if (bannerText && bannerAlpha > 0.0f)
                            {
                                const ImVec2 bannerSize = ImGui::CalcTextSize(bannerText);
                                const ImVec2 textPos(
                                    imageMin.x + (size.x - bannerSize.x) * 0.5f,
                                    imageMin.y + size.y * 0.14f);
                                const ImVec2 pad(18.0f, 10.0f);
                                gameDraw->AddRectFilled(
                                    ImVec2(textPos.x - pad.x, textPos.y - pad.y),
                                    ImVec2(textPos.x + bannerSize.x + pad.x, textPos.y + bannerSize.y + pad.y),
                                    IM_COL32(5, 8, 14, static_cast<int>(160.0f * bannerAlpha)),
                                    6.0f);
                                gameDraw->AddText(
                                    textPos,
                                    IM_COL32(235, 245, 255, static_cast<int>(255.0f * bannerAlpha)),
                                    bannerText);
                            }
                        }

                        const VaultMissionState missionState = game.GetVaultMissionState();
                        if (missionState == VaultMissionState::Escaped || missionState == VaultMissionState::Failed)
                        {
                            gameDraw->AddRectFilled(imageMin, imageMax, IM_COL32(0, 0, 0, 170));

                            const char* title = game.GetVaultEndOverlayTitle();
                            const char* subtitle = game.GetVaultEndOverlaySubtitle();
                            const char* restartText = "Press R / X to Restart";
                            const char* nextVaultText = game.GetNextVaultActionText();
                            const char* returnText = "Press Enter / B to Return to Editor";
                            const char* nextVaultButtonLabel = game.GetNextVaultButtonLabel();

                            ImFont* titleFont = g_UIFontBold ? g_UIFontBold : ImGui::GetFont();
                            const float titleFontSize = ImGui::GetFontSize() * 1.55f;
                            const ImU32 accentStart = (missionState == VaultMissionState::Failed)
                                ? IM_COL32(255, 116, 92, 240)
                                : colorFromMood(moodAccent, 1.0f, 240);
                            const ImU32 accentEnd = (missionState == VaultMissionState::Failed)
                                ? IM_COL32(174, 76, 255, 225)
                                : colorFromMood(moodSecondary, 1.0f, 225);

                            const ImVec2 titleSize = titleFont->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, title ? title : "");
                            const ImVec2 restartSize = ImGui::CalcTextSize(restartText);
                            const ImVec2 nextVaultSize = nextVaultText ? ImGui::CalcTextSize(nextVaultText) : ImVec2(0.0f, 0.0f);
                            const ImVec2 returnSize = ImGui::CalcTextSize(returnText);
                            const ImVec2 nextVaultButtonSize = ImGui::CalcTextSize(nextVaultButtonLabel ? nextVaultButtonLabel : "Next Vault");
                            const float centerY = imageMin.y + size.y * 0.38f;
                            const float availableCardWidth = (std::max)(360.0f, size.x - 72.0f);
                            const float textContentWidth = (std::max)(titleSize.x, (std::max)(restartSize.x, (std::max)(nextVaultSize.x, (std::max)(returnSize.x, nextVaultButtonSize.x + 36.0f))));
                            const float cardWidth = (std::min)(availableCardWidth, (std::max)(420.0f, textContentWidth + 96.0f));
                            const float subtitleWrapWidth = cardWidth - 56.0f;
                            const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle ? subtitle : "", nullptr, false, subtitleWrapWidth);
                            const float cardHeight = nextVaultText ? 236.0f : 152.0f;
                            const ImVec2 cardMin(imageMin.x + (size.x - cardWidth) * 0.5f, centerY - 26.0f);
                            const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);
                            const float accentHeight = 5.0f;

                            gameDraw->AddRectFilled(
                                ImVec2(cardMin.x + 6.0f, cardMin.y + 8.0f),
                                ImVec2(cardMax.x + 6.0f, cardMax.y + 8.0f),
                                IM_COL32(0, 0, 0, 88),
                                14.0f);
                            gameDraw->AddRectFilledMultiColor(
                                cardMin,
                                ImVec2(cardMax.x, cardMin.y + accentHeight),
                                accentStart,
                                accentEnd,
                                accentEnd,
                                accentStart);
                            gameDraw->AddRectFilled(
                                ImVec2(cardMin.x, cardMin.y + accentHeight),
                                cardMax,
                                IM_COL32(9, 11, 18, 226),
                                14.0f);
                            gameDraw->AddRect(
                                cardMin,
                                cardMax,
                                colorFromMood(moodSecondary, 0.78f, 220),
                                14.0f,
                                0,
                                1.2f);

                            const ImVec2 titlePos(imageMin.x + (size.x - titleSize.x) * 0.5f, centerY);
                            const ImVec2 subtitlePos(imageMin.x + (size.x - subtitleSize.x) * 0.5f, centerY + 34.0f);
                            const ImVec2 restartPos(imageMin.x + (size.x - restartSize.x) * 0.5f, centerY + 84.0f);
                            float returnTextY = centerY + 108.0f;

                            gameDraw->AddText(titleFont, titleFontSize, ImVec2(titlePos.x + 1.5f, titlePos.y + 1.5f), IM_COL32(0, 0, 0, 160), title ? title : "");
                            gameDraw->AddText(titleFont, titleFontSize, titlePos, IM_COL32(238, 245, 255, 255), title ? title : "");
                            gameDraw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(subtitlePos.x + 1.0f, subtitlePos.y + 1.0f), IM_COL32(0, 0, 0, 140), subtitle ? subtitle : "", nullptr, subtitleWrapWidth);
                            gameDraw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), subtitlePos, IM_COL32(178, 182, 196, 255), subtitle ? subtitle : "", nullptr, subtitleWrapWidth);
                            gameDraw->AddText(ImVec2(restartPos.x + 1.0f, restartPos.y + 1.0f), IM_COL32(0, 0, 0, 140), restartText);
                            gameDraw->AddText(restartPos, IM_COL32(255, 235, 115, 255), restartText);
                            if (nextVaultText)
                            {
                                const ImVec2 nextVaultPos(imageMin.x + (size.x - nextVaultSize.x) * 0.5f, centerY + 108.0f);
                                gameDraw->AddText(ImVec2(nextVaultPos.x + 1.0f, nextVaultPos.y + 1.0f), IM_COL32(0, 0, 0, 140), nextVaultText);
                                gameDraw->AddText(nextVaultPos, IM_COL32(184, 219, 255, 255), nextVaultText);
                                const float buttonWidth = (std::max)(132.0f, nextVaultButtonSize.x + 32.0f);
                                const ImVec2 buttonPos(imageMin.x + (size.x - buttonWidth) * 0.5f, centerY + 134.0f);
                                ImGui::SetCursorScreenPos(buttonPos);
                                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(66, 42, 28, 230));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(112, 68, 40, 240));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(148, 84, 52, 255));
                                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(214, 146, 92, 220));
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                                if (ImGui::Button((std::string(nextVaultButtonLabel ? nextVaultButtonLabel : "Next Vault") + "##GameEndOverlay").c_str(), ImVec2(buttonWidth, 0.0f)))
                                    (void)g_engineInstance->GetGame().AdvanceToNextVaultNow();
                                ImGui::PopStyleVar(2);
                                ImGui::PopStyleColor(4);

                                const float autoAdvanceSeconds = game.GetNextVaultAutoAdvanceSecondsRemaining();
                                if (autoAdvanceSeconds > 0.0f)
                                {
                                    const std::string autoAdvanceText = std::format("Auto advancing in {:.1f}s", autoAdvanceSeconds);
                                    const ImVec2 autoAdvanceSize = ImGui::CalcTextSize(autoAdvanceText.c_str());
                                    const ImVec2 autoAdvancePos(imageMin.x + (size.x - autoAdvanceSize.x) * 0.5f, centerY + 164.0f);
                                    gameDraw->AddText(ImVec2(autoAdvancePos.x + 1.0f, autoAdvancePos.y + 1.0f), IM_COL32(0, 0, 0, 140), autoAdvanceText.c_str());
                                    gameDraw->AddText(autoAdvancePos, IM_COL32(224, 196, 132, 255), autoAdvanceText.c_str());
                                }

                                returnTextY += 86.0f;
                            }
                            const ImVec2 returnPos(imageMin.x + (size.x - returnSize.x) * 0.5f, returnTextY);
                            gameDraw->AddText(ImVec2(returnPos.x + 1.0f, returnPos.y + 1.0f), IM_COL32(0, 0, 0, 140), returnText);
                            gameDraw->AddText(returnPos, IM_COL32(184, 219, 255, 255), returnText);
                        }

                        gameDraw->PopClipRect();
                    }
                }
                else
                {
                    releaseGameMouseLock();
                    ImGui::TextUnformatted("Game View");
                    ImGui::Separator();
                    ImGui::TextDisabled("Not Playing");
                    ImGui::Dummy(size);
                    hovered = ImGui::IsItemHovered();
                }

                const bool allowRuntimeInput = runtimeActive && hovered && focused && !Engine::IsKeyboardCapturedByUI();
                gfx.SetGameViewportInputState(hovered, focused, allowRuntimeInput);

                const bool wantsGameMouseLock = allowRuntimeInput && ImGui::IsMouseDown(ImGuiMouseButton_Right) && tex != ImTextureID{};
                if (wantsGameMouseLock)
                {
                    RECT clipRect{};
                    clipRect.left = static_cast<LONG>(std::floor(imageMin.x));
                    clipRect.top = static_cast<LONG>(std::floor(imageMin.y));
                    clipRect.right = static_cast<LONG>(std::ceil(imageMax.x));
                    clipRect.bottom = static_cast<LONG>(std::ceil(imageMax.y));
                    ClipCursor(&clipRect);
                    SetCapture(gfx.GetHWND());
                    s_GameMouseLocked = true;
                }
                else
                {
                    releaseGameMouseLock();
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_diagnostics{
        "Diagnostics",
        false,
        []()
        {
            auto& panel = Diagnostics();
            if (!panel.open)
                return;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                auto& gfx = Graphics::GetInstance();
                ImGuiIO& io = ImGui::GetIO();

                const float fps = io.Framerate;
                const float dt = (fps > 0.0f) ? (1.0f / fps) : 0.0f;
                const float frameMs = dt * 1000.0f;

                if (g_MonoFont) ImGui::PushFont(g_MonoFont);
                ImGui::Text("FPS: %.1f", fps);
                ImGui::Text("DeltaTime: %.4f s", dt);
                ImGui::Text("Frame Time: %.2f ms", frameMs);
                ImGui::Separator();

                ImGui::Text("SwapChain: %u x %u", gfx.GetSwapChainWidth(), gfx.GetSwapChainHeight());
                ImGui::Text("BackBuffer Index: %u", gfx.GetBackBufferIndex());
                ImGui::Separator();

                ImGui::Text("Scene RT: %u x %u", gfx.GetSceneRTWidth(), gfx.GetSceneRTHeight());
                ImGui::Text("Scene RT Pending Resize: %s", gfx.IsSceneRTPendingResize() ? "YES" : "NO");
                if (gfx.IsSceneRTPendingResize())
                    ImGui::Text("Pending Scene RT: %u x %u", gfx.GetPendingSceneRTWidth(), gfx.GetPendingSceneRTHeight());

                ImGui::Separator();

                const SceneStats stats = Scene::GetLastStats();
                ImGui::TextUnformatted("Scene Stats");
                ImGui::Text("Objects: %u", stats.totalObjects);
                ImGui::Text("Visible: %u", stats.visibleObjects);
                ImGui::Text("Draws:   %u", stats.drawCalls);

                ImGui::Separator();

                ImGui::TextUnformatted("RuntimeWorld");
                if (g_engineInstance)
                {
                    const Engine::State engineState = Engine::GetState();
                    const char* engineStateLabel = "Editing";
                    switch (engineState)
                    {
                    case Engine::State::Playing:
                        engineStateLabel = "Playing";
                        break;
                    case Engine::State::Paused:
                        engineStateLabel = "Paused";
                        break;
                    case Engine::State::Editing:
                    default:
                        engineStateLabel = "Editing";
                        break;
                    }

                    const RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
                    const RuntimeWorldStats runtimeStats = runtimeWorld.GetStats();
                    const RuntimeEntityId mainCameraEntity = runtimeWorld.FindMainCameraEntity();
                    const RuntimeEntityId playerEntity = runtimeWorld.FindFirstPlayerControllerEntity();

                    ImGui::Text("Engine State: %s", engineStateLabel);
                    ImGui::Text("Entities:        %llu", static_cast<unsigned long long>(runtimeStats.entities));
                    ImGui::Text("Transforms:      %llu", static_cast<unsigned long long>(runtimeStats.transforms));
                    ImGui::Text("Cameras:         %llu", static_cast<unsigned long long>(runtimeStats.cameras));
                    ImGui::Text("Mesh Renderers:  %llu", static_cast<unsigned long long>(runtimeStats.meshRenderers));
                    ImGui::Text("Players:         %llu", static_cast<unsigned long long>(runtimeStats.playerControllers));
                    ImGui::Text("Trigger Volumes: %llu", static_cast<unsigned long long>(runtimeStats.triggerVolumes));
                    ImGui::Text("Vault Nodes:     %llu", static_cast<unsigned long long>(runtimeStats.vaultNodes));
                    ImGui::Text("Vault Cores:     %llu", static_cast<unsigned long long>(runtimeStats.vaultCores));
                    ImGui::Text("Vault Rings:     %llu", static_cast<unsigned long long>(runtimeStats.vaultRings));
                    ImGui::Text("Vault Exits:     %llu", static_cast<unsigned long long>(runtimeStats.vaultExits));
                    ImGui::Text("Main Camera Entity: %u", mainCameraEntity);
                    ImGui::Text("Player Entity:      %u", playerEntity);

                    ImGui::Separator();
                    ImGui::TextUnformatted("Selected Runtime Entity");
                    const uint32_t selectedSceneInstanceId = Scene::GetSelectedInstanceId();
                    const RuntimeEntityId selectedRuntimeEntityId = runtimeWorld.FindBySourceSceneInstanceId(selectedSceneInstanceId);
                    ImGui::Text("Selected Scene Instance: %u", selectedSceneInstanceId);
                    ImGui::Text("Runtime Entity:          %u", selectedRuntimeEntityId);
                    if (const RuntimeEntity* selectedRuntimeEntity = runtimeWorld.GetEntity(selectedRuntimeEntityId))
                    {
                        ImGui::Text("Name: %s", selectedRuntimeEntity->name.c_str());
                        ImGui::Text("Parent Entity: %u", selectedRuntimeEntity->parent);

                        const RuntimeTransformComponent* transform = runtimeWorld.GetTransform(selectedRuntimeEntityId);
                        if (transform)
                        {
                            ImGui::Text("Runtime Position: (%.2f, %.2f, %.2f)", transform->position.x, transform->position.y, transform->position.z);
                            ImGui::Text("Runtime Rotation: (%.2f, %.2f, %.2f)", transform->rotation.x, transform->rotation.y, transform->rotation.z);
                            ImGui::Text("Runtime Scale:    (%.2f, %.2f, %.2f)", transform->scale.x, transform->scale.y, transform->scale.z);
                        }

                        ImGui::Text("Components: %s%s%s%s%s%s%s%s",
                            runtimeWorld.GetTransform(selectedRuntimeEntityId) ? "Transform " : "",
                            runtimeWorld.GetCamera(selectedRuntimeEntityId) ? "Camera " : "",
                            runtimeWorld.GetMeshRenderer(selectedRuntimeEntityId) ? "MeshRenderer " : "",
                            runtimeWorld.GetPlayerController(selectedRuntimeEntityId) ? "PlayerController " : "",
                            runtimeWorld.GetTriggerVolume(selectedRuntimeEntityId) ? "TriggerVolume " : "",
                            runtimeWorld.GetVaultNode(selectedRuntimeEntityId) ? "VaultNode " : "",
                            runtimeWorld.GetVaultCore(selectedRuntimeEntityId) ? "VaultCore " : "",
                            runtimeWorld.GetVaultExit(selectedRuntimeEntityId) ? "VaultExit" : "");

                        if (runtimeWorld.GetVaultRing(selectedRuntimeEntityId))
                            ImGui::TextUnformatted("Additional Component: VaultRing");
                    }
                    else
                    {
                        ImGui::TextDisabled("No selected runtime entity mapping. Enter Play mode or select a cloned scene object.");
                    }
                }
                else
                {
                    ImGui::TextDisabled("Engine instance unavailable.");
                }

                ImGui::Separator();

                SceneCamera& cam = gfx.GetSceneCamera();
                DirectX::XMFLOAT3 targ{};
                DirectX::XMStoreFloat3(&targ, cam.GetTarget());

                ImGui::Text("Camera Yaw:   %.3f", cam.GetYaw());
                ImGui::Text("Camera Pitch: %.3f", cam.GetPitch());
                ImGui::Text("Camera Dist:  %.3f", cam.GetDistance());
                ImGui::Text("Camera Target: (%.2f, %.2f, %.2f)", targ.x, targ.y, targ.z);
                ImGui::Separator();
                ImGui::Text("UI Mouse Capture: %d", Engine::IsMouseCapturedByUI() ? 1 : 0);
                ImGui::Text("UI Keyboard Capture: %d", Engine::IsKeyboardCapturedByUI() ? 1 : 0);
                if (g_MonoFont) ImGui::PopFont();
            }
            ImGui::End();
        }
    };

    static EditorPanel g_logViewer{
        "Log Viewer",
        false,
        []()
        {
            auto& panel = LogViewer();
            if (!panel.open)
                return;

            static char filter[256] = "";
            static int levelIdx = 0;
            static bool autoScroll = true;
            static constexpr size_t kMaxLines = 500;

            auto passesLevel = [&](const std::string& line) -> bool
            {
                // Level dropdown: Trace/Info/Warn/Error
                switch (levelIdx)
                {
                case 0: return line.find("[TRACE]") != std::string::npos;
                case 1: return line.find("[INFO]") != std::string::npos || line.find("[SUCCESS]") != std::string::npos;
                case 2: return line.find("[WARNING]") != std::string::npos;
                case 3: return line.find("[ERROR]") != std::string::npos || line.find("[CRITICAL]") != std::string::npos;
                default: return true;
                }
            };

            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (ImGui::Button("Clear"))
                    Logger::ClearAllLogs();
                ImGui::SameLine();
                ImGui::Checkbox("Auto-scroll", &autoScroll);

                ImGui::InputText("Filter", filter, sizeof(filter));

                const char* levels[] = { "Trace", "Info", "Warn", "Error" };
                ImGui::Combo("Level", &levelIdx, levels, IM_ARRAYSIZE(levels));

                ImGui::Separator();

                ImGui::BeginChild("LogViewerChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

                const auto lines = Logger::GetRecentLogs(kMaxLines);
                for (const auto& line : lines)
                {
                    if (filter[0] != '\0' && line.find(filter) == std::string::npos)
                        continue;
                    if (!passesLevel(line))
                        continue;

                    ImGui::TextUnformatted(line.c_str());
                }

                if (autoScroll)
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
            }
            ImGui::End();
        }
    };

    static void DrawHierarchyNode(uint32_t instanceId, EditorState& editor)
    {
        SceneInstance* instance = nullptr;
        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            if (instances[i].instanceId == instanceId)
            {
                instance = Scene::GetInstance(i);
                break;
            }
        }
        if (!instance)
            return;

        const auto children = Scene::GetChildInstanceIds(instanceId);
        const bool isSelected = Scene::IsInstanceSelected(instance->instanceId);
        const bool isPrefabInstance = !instance->prefabSourcePath.empty();
        PrefabOverrideState prefabOverrides{};
        const bool hasPrefabOverrides = isPrefabInstance && Scene::TryGetPrefabOverrideState(instance->instanceId, prefabOverrides) && prefabOverrides.Any();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        const bool isActiveMainCamera = instance->camera.enabled && instance->camera.isMain;
        const char* hierarchyIcon = instance->camera.enabled
            ? ICON_FA_SEARCH
            : ((instance->primitive == ScenePrimitive::Empty) ? ICON_FA_SITEMAP : ICON_FA_CUBE);
        const std::string hierarchyBaseLabel = isPrefabInstance
            ? std::format("{} {}{}", hierarchyIcon, instance->name, hasPrefabOverrides ? " *" : "")
            : std::format("{} {}", hierarchyIcon, instance->name);
        const std::string hierarchyLabel = isActiveMainCamera
            ? std::format("{}  [ACTIVE]", hierarchyBaseLabel)
            : hierarchyBaseLabel;

        const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)instance->instanceId, flags, "%s", hierarchyLabel.c_str());
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyShift)
                Scene::ToggleSelectedInstanceId(instance->instanceId);
            else
                Scene::SetSelectedInstanceId(instance->instanceId);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (!isSelected)
            {
                Scene::SetSelectedInstanceId(instance->instanceId);
                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
            }
            g_PendingSceneInstanceRenameId = instance->instanceId;
            strncpy_s(g_SceneInstanceRenameBuffer, instance->name.c_str(), _TRUNCATE);
            g_RequestOpenSceneInstanceRenamePopup = true;
        }

        if (ImGui::BeginDragDropSource())
        {
            const uint32_t payloadId = instance->instanceId;
            ImGui::SetDragDropPayload("SCENE_INSTANCE", &payloadId, sizeof(payloadId));
            ImGui::TextUnformatted(hierarchyLabel.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMaterialPayload))
            {
                if (payload->DataSize == sizeof(int))
                {
                    const int materialIndex = *static_cast<const int*>(payload->Data);
                    ApplyMaterialToSceneInstance(editor, instance, materialIndex);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_INSTANCE"))
            {
                const uint32_t draggedId = *(const uint32_t*)payload->Data;
                if (draggedId != instance->instanceId && Scene::CanParentInstance(draggedId, instance->instanceId))
                {
                    g_PendingReparentChildId = draggedId;
                    g_PendingReparentParentId = instance->instanceId;
                    Scene::CanPreserveWorldTransformOnReparent(draggedId, instance->instanceId, &g_PendingReparentKeepWorldReason);
                    g_RequestOpenReparentPopup = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem("##HierarchyItemContext"))
        {
            if (!isSelected)
            {
                Scene::SetSelectedInstanceId(instance->instanceId);
                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
            }

            const bool canUndo = CanUndoCommand();
            const bool canRedo = CanRedoCommand();
            const bool hasSelection = (Scene::GetSelectedInstance() != nullptr);
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                ExecuteUndoCommand();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                ExecuteRedoCommand();
            if (canUndo || canRedo)
                ImGui::Separator();
            if (ImGui::MenuItem("Focus", "F", false, hasSelection))
            {
                DirectX::XMFLOAT3 focusPoint{};
                if (TryGetCurrentSelectionAnchor(editor, focusPoint))
                    Graphics::GetInstance().GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
            }
            if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
            {
                g_PendingSceneInstanceRenameId = instance->instanceId;
                strncpy_s(g_SceneInstanceRenameBuffer, instance->name.c_str(), _TRUNCATE);
                g_RequestOpenSceneInstanceRenamePopup = true;
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
            {
                if (ExecuteEditorCommand(editor, std::make_unique<DuplicateSelectionCommand>(Scene::GetSelectedInstanceIds())))
                    editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
            }
            if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
            {
                DeleteCurrentSelection(editor);
            }
            if (ImGui::MenuItem("Create Empty Parent", nullptr, false, hasSelection))
                CreateEmptyParentForSelection(editor);
            ImGui::Separator();
            if (ImGui::MenuItem("Save Prefab", nullptr, false, hasSelection))
                SaveCurrentSelectionAsPrefab(editor);
            if (ImGui::MenuItem("Unpack Prefab", nullptr, false, hasSelection && SelectionContainsPrefabInstance()))
            {
                            if (ExecuteEditorCommand(editor, std::make_unique<UnpackPrefabCommand>(Scene::GetSelectedInstanceIds())))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            for (uint32_t childId : children)
                DrawHierarchyNode(childId, editor);
            ImGui::TreePop();
        }
    }

    static EditorPanel g_hierarchy{
        "Hierarchy",
        true,
        []()
        {
            auto& panel = Hierarchy();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                extern Engine* g_engineInstance;
                EditorState& editor = g_engineInstance->GetEditorState();

                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_SITEMAP " Scene Hierarchy");
                if (g_UIFontBold) ImGui::PopFont();

                ImGui::Separator();
                const auto& instances = Scene::GetInstances();
                for (const SceneInstance& instance : instances)
                {
                    if (instance.parentInstanceId == 0)
                        DrawHierarchyNode(instance.instanceId, editor);
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_INSTANCE"))
                    {
                        const uint32_t draggedId = *(const uint32_t*)payload->Data;
                        if (Scene::GetParentInstanceId(draggedId) != 0)
                        {
                            PushUndoSnapshot(editor);
                            if (Scene::SetParentInstance(draggedId, 0, true))
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (g_RequestOpenReparentPopup)
                {
                    ImGui::OpenPopup("Reparent Instance##Hierarchy");
                    g_RequestOpenReparentPopup = false;
                }

                if (ImGui::BeginPopupModal("Reparent Instance##Hierarchy", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    const SceneInstance* child = nullptr;
                    const SceneInstance* parent = nullptr;
                    for (const SceneInstance& instance : Scene::GetInstances())
                    {
                        if (instance.instanceId == g_PendingReparentChildId)
                            child = &instance;
                        if (instance.instanceId == g_PendingReparentParentId)
                            parent = &instance;
                    }

                    ImGui::TextUnformatted("Choose reparent mode");
                    ImGui::Separator();
                    ImGui::Text("Child: %s", child ? child->name.c_str() : "<Unknown>");
                    ImGui::Text("Parent: %s", parent ? parent->name.c_str() : "<Unknown>");

                    const bool canKeepWorld = Scene::CanPreserveWorldTransformOnReparent(
                        g_PendingReparentChildId,
                        g_PendingReparentParentId,
                        &g_PendingReparentKeepWorldReason);

                    if (!canKeepWorld && !g_PendingReparentKeepWorldReason.empty())
                        ImGui::TextWrapped("%s", g_PendingReparentKeepWorldReason.c_str());

                    if (!canKeepWorld)
                        ImGui::BeginDisabled();
                    if (ImGui::Button("Keep World##Reparent", ImVec2(120.0f, 0.0f)))
                    {
                        if (ExecuteEditorCommand(editor, std::make_unique<ReparentCommand>(g_PendingReparentChildId, g_PendingReparentParentId, true)))
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        g_PendingReparentChildId = 0;
                        g_PendingReparentParentId = 0;
                        g_PendingReparentKeepWorldReason.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    if (!canKeepWorld)
                        ImGui::EndDisabled();
                    if (!canKeepWorld)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(Unavailable)");
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Keep Local##Reparent", ImVec2(120.0f, 0.0f)))
                    {
                        if (ExecuteEditorCommand(editor, std::make_unique<ReparentCommand>(g_PendingReparentChildId, g_PendingReparentParentId, false)))
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        g_PendingReparentChildId = 0;
                        g_PendingReparentParentId = 0;
                        g_PendingReparentKeepWorldReason.clear();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##Reparent", ImVec2(100.0f, 0.0f)))
                    {
                        g_PendingReparentChildId = 0;
                        g_PendingReparentParentId = 0;
                        g_PendingReparentKeepWorldReason.clear();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (g_RequestOpenSceneInstanceRenamePopup)
                {
                    ImGui::OpenPopup("Rename Scene Object##Hierarchy");
                    g_RequestOpenSceneInstanceRenamePopup = false;
                }

                if (ImGui::BeginPopupModal("Rename Scene Object##Hierarchy", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    SceneInstance* pendingRenameInstance = FindSceneInstanceById(g_PendingSceneInstanceRenameId);
                    ImGui::TextUnformatted("Rename selected object");
                    ImGui::Separator();
                    ImGui::InputText("Name", g_SceneInstanceRenameBuffer, IM_ARRAYSIZE(g_SceneInstanceRenameBuffer));

                    if (ImGui::Button("Rename##SceneInstance"))
                    {
                        if (pendingRenameInstance && g_SceneInstanceRenameBuffer[0] != '\0')
                        {
                            ExecuteEditorCommand(editor, std::make_unique<RenameSceneInstanceCommand>(
                                pendingRenameInstance->instanceId,
                                pendingRenameInstance->name,
                                g_SceneInstanceRenameBuffer));
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                        g_PendingSceneInstanceRenameId = 0;
                        g_SceneInstanceRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##SceneInstanceRename"))
                    {
                        g_PendingSceneInstanceRenameId = 0;
                        g_SceneInstanceRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_inspector{
        "Inspector",
        true,
        []()
        {
            auto& panel = Inspector();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_SEARCH " Inspector");
                if (g_UIFontBold) ImGui::PopFont();

                if (SceneInstance* selected = Scene::GetSelectedInstance())
                {
                    extern Engine* g_engineInstance;
                    EditorState& editor = g_engineInstance->GetEditorState();

                    char nameBuffer[128] = {};
                    strncpy_s(nameBuffer, selected->name.c_str(), sizeof(nameBuffer) - 1);

                    PrefabOverrideState prefabOverrides{};
                    const bool hasPrefabOverrideState = !selected->prefabSourcePath.empty() &&
                        Scene::TryGetPrefabOverrideState(selected->instanceId, prefabOverrides);

                    const ImVec4 overrideTextColor = ImVec4(1.0f, 0.78f, 0.35f, 1.0f);
                    const ImVec4 overrideFrameBg = ImVec4(0.42f, 0.26f, 0.08f, 0.45f);
                    const ImVec4 overrideFrameBgHovered = ImVec4(0.50f, 0.31f, 0.10f, 0.60f);
                    const ImVec4 overrideFrameBgActive = ImVec4(0.58f, 0.35f, 0.12f, 0.75f);

                    auto pushOverrideHighlight = [&](bool enabled)
                    {
                        if (!enabled)
                            return;
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, overrideFrameBg);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, overrideFrameBgHovered);
                        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, overrideFrameBgActive);
                    };
                    auto popOverrideHighlight = [&](bool enabled)
                    {
                        if (enabled)
                            ImGui::PopStyleColor(3);
                    };
                    auto revertPrefabProperty = [&](PrefabProperty property)
                    {
                        PushUndoSnapshot(editor);
                        if (!Scene::RevertSelectedPrefabProperty(property))
                            Logger::Log(LogLevel::Error, "Failed to revert prefab property.", "[Editor]");
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    };
                    auto applyPrefabProperty = [&](PrefabProperty property)
                    {
                        if (!Scene::ApplySelectedPrefabProperty(property))
                            Logger::Log(LogLevel::Error, "Failed to apply prefab property.", "[Editor]");
                    };
                    auto drawOverrideControls = [&](bool enabled, const char* id, PrefabProperty property)
                    {
                        if (!enabled)
                            return;
                        ImGui::SameLine();
                        ImGui::TextColored(overrideTextColor, "Override");
                        ImGui::SameLine();
                        const std::string applyButtonId = std::format("Apply##{}", id);
                        if (ImGui::SmallButton(applyButtonId.c_str()))
                            applyPrefabProperty(property);
                        ImGui::SameLine();
                        const std::string revertButtonId = std::format("Revert##{}", id);
                        if (ImGui::SmallButton(revertButtonId.c_str()))
                            revertPrefabProperty(property);
                    };

                    ImGui::Separator();
                    ImGui::Text("Object ID: %u", selected->instanceId);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Move To Origin"))
                    {
                        PushUndoSnapshot(editor);
                        selected->position = { 0.0f, 0.0f, 0.0f };
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Focus"))
                    {
                        DirectX::XMFLOAT3 focusPoint = Scene::GetSelectionCenterOrActivePosition();
                        Graphics::GetInstance().GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Duplicate"))
                    {
                        if (ExecuteEditorCommand(editor, std::make_unique<DuplicateSelectionCommand>(Scene::GetSelectedInstanceIds())))
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete"))
                    {
                        DeleteCurrentSelection(editor);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Save Prefab"))
                    {
                        const std::filesystem::path prefabPath = std::filesystem::path("Assets") / "Prefabs" /
                            (SanitizeAssetName(selected->name) + ".prefab.json");
                        if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
                            Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
                        else
                            EditorPanels::PrefabWorkflow().open = true;
                    }

                    if (!selected->prefabSourcePath.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Apply Prefab"))
                        {
                            if (!Scene::ApplySelectedToPrefab())
                                Logger::Log(LogLevel::Error, "Failed to apply prefab.", "[Editor]");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Revert Prefab"))
                        {
                            PushUndoSnapshot(editor);
                            if (!Scene::RevertSelectedToPrefab())
                                Logger::Log(LogLevel::Error, "Failed to revert prefab.", "[Editor]");
                            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                        }
                    }

                    if (ImGui::SmallButton("Material Preview"))
                        EditorPanels::MaterialPreview().open = true;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Prefab Workflow"))
                        EditorPanels::PrefabWorkflow().open = true;
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Black Flame"))
                        EditorPanels::BlackFlame().open = true;

                    if (hasPrefabOverrideState)
                    {
                        ImGui::TextColored(overrideTextColor, "Prefab Instance");
                        if (prefabOverrides.Any())
                        {
                            std::string overrideList;
                            auto appendOverride = [&overrideList](const char* label)
                            {
                                if (!overrideList.empty())
                                    overrideList += ", ";
                                overrideList += label;
                            };
                            if (prefabOverrides.name) appendOverride("Name");
                            if (prefabOverrides.rotation) appendOverride("Rotation");
                            if (prefabOverrides.scale) appendOverride("Scale");
                            if (prefabOverrides.visible) appendOverride("Visible");
                            if (prefabOverrides.material) appendOverride("Material");
                            if (prefabOverrides.vaultType) appendOverride("Vault Type");
                            ImGui::TextColored(overrideTextColor, "Overrides: %s", overrideList.c_str());
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f), "Overrides: None");
                        }
                    }

                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.name);
                    const bool nameChanged = ImGui::InputText("Name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
                    const bool nameActivated = ImGui::IsItemActivated();
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.name);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.name, "Name", PrefabProperty::Name);
                    if (nameActivated)
                        PushUndoSnapshot(editor);
                    if (nameChanged)
                        selected->name = nameBuffer;

                    DirectX::XMFLOAT3 position = selected->position;
                    const bool positionChanged = DrawTickFloat3("Position", position, 0.1f, -10000.0f, 10000.0f);
                    if (positionChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->position = position;
                        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                    }

                    DirectX::XMFLOAT3 rotation = selected->rotation;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    const bool rotationChanged = DrawTickFloat3("Rotation", rotation, 1.0f, -360.0f, 360.0f);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.rotation);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.rotation, "Rotation", PrefabProperty::Rotation);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset##Rotation"))
                    {
                        PushUndoSnapshot(editor);
                        selected->rotation = { 0.0f, 0.0f, 0.0f };
                    }
                    else if (rotationChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->rotation = rotation;
                    }

                    DirectX::XMFLOAT3 scale = selected->scale;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    const bool scaleChanged = DrawTickFloat3("Scale", scale, 0.05f, 0.01f, 100.0f);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.scale);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.scale, "Scale", PrefabProperty::Scale);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset##Scale"))
                    {
                        PushUndoSnapshot(editor);
                        selected->scale = { 1.0f, 1.0f, 1.0f };
                    }
                    else if (scaleChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->scale.x = (std::max)(scale.x, 0.01f);
                        selected->scale.y = (std::max)(scale.y, 0.01f);
                        selected->scale.z = (std::max)(scale.z, 0.01f);
                    }

                    const bool hasNonUniformScale =
                        fabsf(selected->scale.x - selected->scale.y) > 1e-4f ||
                        fabsf(selected->scale.x - selected->scale.z) > 1e-4f ||
                        fabsf(selected->scale.y - selected->scale.z) > 1e-4f;
                    if (hasNonUniformScale)
                    {
                        ImGui::TextColored(
                            ImVec4(1.0f, 0.78f, 0.35f, 1.0f),
                            "Warning: Non-uniform scale can prevent Keep World parenting.");
                    }

                    bool visible = selected->visible;
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.visible);
                    const bool visibleChanged = ImGui::Checkbox("Visible", &visible);
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.visible);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.visible, "Visible", PrefabProperty::Visible);
                    if (visibleChanged)
                    {
                        PushUndoSnapshot(editor);
                        selected->visible = visible;
                    }

                    ImGui::Separator();
                    ImGui::TextUnformatted("Gameplay");
                    pushOverrideHighlight(hasPrefabOverrideState && prefabOverrides.vaultType);
                    const char* currentVaultTypeLabel = GetVaultTypeLabel(selected->vaultType);
                    if (ImGui::BeginCombo("Vault Type", currentVaultTypeLabel))
                    {
                        constexpr VaultType vaultTypes[] = {
                            VaultType::None,
                            VaultType::Node,
                            VaultType::Ring,
                            VaultType::Core,
                        };
                        for (VaultType vaultType : vaultTypes)
                        {
                            const bool isCurrent = (selected->vaultType == vaultType);
                            if (ImGui::Selectable(GetVaultTypeLabel(vaultType), isCurrent))
                            {
                                PushUndoSnapshot(editor);
                                selected->vaultType = vaultType;
                            }
                            if (isCurrent)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    popOverrideHighlight(hasPrefabOverrideState && prefabOverrides.vaultType);
                    drawOverrideControls(hasPrefabOverrideState && prefabOverrides.vaultType, "VaultType", PrefabProperty::VaultType);

                    if (selected->camera.enabled)
                    {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Camera");
                        if (selected->camera.isMain)
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.45f, 1.0f), "ACTIVE");
                        }

                        bool isMainCamera = selected->camera.isMain;
                        if (ImGui::Checkbox("Main Camera", &isMainCamera))
                        {
                            PushUndoSnapshot(editor);
                            selected->camera.isMain = isMainCamera;
                            if (isMainCamera)
                                SetInstanceAsMainCamera(selected);
                        }

                        if (!selected->camera.isMain)
                        {
                            if (ImGui::SmallButton("Set As Main Camera"))
                            {
                                PushUndoSnapshot(editor);
                                SetInstanceAsMainCamera(selected);
                            }
                        }

                        float fovDegrees = DirectX::XMConvertToDegrees(selected->camera.fovY);
                        if (ImGui::DragFloat("FOV", &fovDegrees, 0.1f, 10.0f, 120.0f, "%.1f deg"))
                        {
                            PushUndoSnapshot(editor);
                            fovDegrees = (std::clamp)(fovDegrees, 10.0f, 120.0f);
                            selected->camera.fovY = DirectX::XMConvertToRadians(fovDegrees);
                        }

                        float nearClip = selected->camera.nearClip;
                        if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 10.0f, "%.3f"))
                        {
                            PushUndoSnapshot(editor);
                            selected->camera.nearClip = (std::max)(0.001f, nearClip);
                            selected->camera.farClip = (std::max)(selected->camera.nearClip + 0.1f, selected->camera.farClip);
                        }

                        float farClip = selected->camera.farClip;
                        if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 10.0f, 10000.0f, "%.1f"))
                        {
                            PushUndoSnapshot(editor);
                            selected->camera.farClip = (std::max)(selected->camera.nearClip + 0.1f, farClip);
                        }
                    }

                    if (hasPrefabOverrideState && prefabOverrides.material)
                    {
                        ImGui::TextColored(overrideTextColor, "Material Index: %d", selected->materialIndex);
                        drawOverrideControls(true, "Material", PrefabProperty::Material);
                    }
                    else
                    {
                        ImGui::Text("Material Index: %d", selected->materialIndex);
                    }

                    MaterialManager& materials = MaterialManager::GetInstance();
                    Material* material = materials.GetMaterialByIndex(selected->materialIndex);

                    auto emitMaterialChanged = [&](Material* changedMaterial)
                    {
                        if (!changedMaterial)
                            return;
                        SceneEvent evt{};
                        evt.Type = SceneEventType::MaterialChanged;
                        evt.Entity = selected;
                        evt.Material = changedMaterial;
                        evt.ColorBias = changedMaterial->baseColor;
                        evt.EventStrength = 0.8f;
                        editor.sceneEvents.Emit(evt);
                    };

                    ImGui::Separator();
                    ImGui::TextUnformatted("Material");
                    ImGui::Text("Material Slot: %d", selected->materialIndex);

                    const std::string materialLabel = material ? material->name : std::string("<Invalid>");
                    if (ImGui::BeginCombo("Assigned Material", materialLabel.c_str()))
                    {
                        for (int i = 0; i < materials.GetMaterialCount(); ++i)
                        {
                            const std::string* materialName = materials.GetMaterialNameByIndex(i);
                            if (!materialName)
                                continue;
                            const bool isCurrent = (selected->materialIndex == i);
                            if (ImGui::Selectable(materialName->c_str(), isCurrent))
                            {
                                PushUndoSnapshot(editor);
                                selected->materialIndex = i;
                                material = materials.GetMaterialByIndex(i);
                                Scene::RebuildRenderInstancesFromSceneData();
                                Scene::MarkInstancesDirty();
                                emitMaterialChanged(material);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::SmallButton("Create Material"))
                    {
                        Material* created = materials.CreateMaterial(std::format("Material_{}", selected->instanceId));
                        if (created)
                        {
                            PushUndoSnapshot(editor);
                            selected->materialIndex = materials.GetMaterialCount() - 1;
                            material = created;
                            Scene::RebuildRenderInstancesFromSceneData();
                            Scene::MarkInstancesDirty();
                            emitMaterialChanged(material);
                        }
                    }

                    if (material)
                        DrawMaterialInspectorFields(material, editor, "SelectedObjectMaterial", emitMaterialChanged);
                }
                else
                {
                    ImGui::Separator();

                    extern Engine* g_engineInstance;
                    EditorState* editor = g_engineInstance ? &g_engineInstance->GetEditorState() : nullptr;
                    MaterialManager& materials = MaterialManager::GetInstance();
                    Material* focusedMaterial = (editor && editor->focusedMaterialIndex >= 0) ? materials.GetMaterialByIndex(editor->focusedMaterialIndex) : nullptr;

                    if (editor && focusedMaterial)
                    {
                        ImGui::TextUnformatted("Focused Material Asset");
                        ImGui::Text("Material: %s", focusedMaterial->name.c_str());
                        if (ImGui::SmallButton("Clear Material Focus"))
                            editor->focusedMaterialIndex = -1;
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Material Preview"))
                            EditorPanels::MaterialPreview().open = true;

                        auto emitMaterialChanged = [&](Material* changedMaterial)
                        {
                            if (!changedMaterial)
                                return;
                            SceneEvent evt{};
                            evt.Type = SceneEventType::MaterialChanged;
                            evt.Material = changedMaterial;
                            evt.ColorBias = changedMaterial->baseColor;
                            evt.EventStrength = 0.8f;
                            editor->sceneEvents.Emit(evt);
                        };

                        DrawMaterialInspectorFields(focusedMaterial, *editor, "FocusedMaterialAsset", emitMaterialChanged);
                    }
                    else
                    {
                        ImGui::TextUnformatted("No object selected.");
                    }
                }
            }
            ImGui::End();
        }
    };

    static EditorPanel g_assets{
        "Assets",
        true,
        []()
        {
            auto& panel = Assets();
            if (!panel.open)
                return;
            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
                ImGui::Text(ICON_FA_FOLDER_OPEN " Asset Browser");
                if (g_UIFontBold) ImGui::PopFont();

                EnsureAssetCurrentFolderValid();
                const std::filesystem::path assetRoot = GetAssetRootPath();
                const std::filesystem::path currentFolder = g_AssetCurrentFolder;
                const std::filesystem::path relativeCurrentFolder = (currentFolder == assetRoot)
                    ? std::filesystem::path{}
                    : std::filesystem::relative(currentFolder, assetRoot);

                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##AssetSearch", "Search assets...", g_AssetSearch, IM_ARRAYSIZE(g_AssetSearch));
                const std::string currentFolderLabel = relativeCurrentFolder.empty() ? std::string("Assets") : std::format("Assets/{}", relativeCurrentFolder.generic_string());
                ImGui::TextDisabled("%s / %s", currentFolderLabel.c_str(), GetAssetCategoryLabel(g_SelectedAssetCategory));
                if (ImGui::Button("Create Folder##Assets"))
                {
                    std::filesystem::path createdFolder;
                    if (CreateAssetFolder(createdFolder))
                        QueueAssetSelection(createdFolder.string());
                }
                ImGui::Separator();

                const float sidebarWidth = 150.0f;
                const float paneHeight = ImGui::GetContentRegionAvail().y;

                ImGui::BeginChild("##AssetCategories", ImVec2(sidebarWidth, paneHeight), true);
                if (ImGui::Selectable("All", g_SelectedAssetCategory == AssetCategory::All))
                    g_SelectedAssetCategory = AssetCategory::All;
                if (ImGui::Selectable("Primitives", g_SelectedAssetCategory == AssetCategory::Primitives))
                    g_SelectedAssetCategory = AssetCategory::Primitives;
                if (ImGui::Selectable("Prefabs", g_SelectedAssetCategory == AssetCategory::Prefabs))
                    g_SelectedAssetCategory = AssetCategory::Prefabs;
                if (ImGui::Selectable("Scenes", g_SelectedAssetCategory == AssetCategory::Scenes))
                    g_SelectedAssetCategory = AssetCategory::Scenes;
                if (ImGui::Selectable("Textures", g_SelectedAssetCategory == AssetCategory::Textures))
                    g_SelectedAssetCategory = AssetCategory::Textures;
                if (ImGui::Selectable("Materials", g_SelectedAssetCategory == AssetCategory::Materials))
                    g_SelectedAssetCategory = AssetCategory::Materials;
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##AssetContent", ImVec2(0, paneHeight), false);

                if (currentFolder != assetRoot)
                {
                    if (ImGui::SmallButton("Up##AssetsFolderNav"))
                        g_AssetCurrentFolder = currentFolder.parent_path();
                    ImGui::SameLine();
                }
                if (ImGui::SmallButton("Assets##BreadcrumbRoot"))
                    g_AssetCurrentFolder = assetRoot;
                if (!relativeCurrentFolder.empty())
                {
                    std::filesystem::path breadcrumbPath = assetRoot;
                    for (const auto& part : relativeCurrentFolder)
                    {
                        ImGui::SameLine();
                        ImGui::TextUnformatted(">");
                        ImGui::SameLine();
                        breadcrumbPath /= part;
                        const std::string crumbLabel = std::format("{}##Breadcrumb{}", part.string(), breadcrumbPath.generic_string());
                        if (ImGui::SmallButton(crumbLabel.c_str()))
                            g_AssetCurrentFolder = breadcrumbPath;
                    }
                }
                ImGui::Separator();

                const std::string filterText = g_AssetSearch;
                auto matchesFilter = [&filterText](const std::string& label)
                {
                    if (filterText.empty())
                        return true;

                    std::string lowerLabel = label;
                    std::string lowerFilter = filterText;
                    std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                    return lowerLabel.find(lowerFilter) != std::string::npos;
                };

                constexpr float kTileWidth = 118.0f;
                constexpr float kTileSpacing = 10.0f;
                const float availableWidth = ImGui::GetContentRegionAvail().x;
                const int columns = GetAssetTileColumnCount(availableWidth, kTileWidth, kTileSpacing);

                const bool showPrimitives = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Primitives;
                const bool showPrefabs = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Prefabs;
                const bool showScenes = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Scenes;
                const bool showTextures = g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Textures;
                const bool showMaterials = (g_SelectedAssetCategory == AssetCategory::All || g_SelectedAssetCategory == AssetCategory::Materials) && currentFolder == assetRoot;
                const bool showFolders = g_SelectedAssetCategory == AssetCategory::All;

                if (showFolders)
                {
                    DrawAssetSectionHeader("Folders");
                    const auto assetFolders = EnumerateAssetFolders();
                    int tileIndex = 0;
                    bool anyFolderShown = false;
                    for (const auto& folderPath : assetFolders)
                    {
                        const std::string label = folderPath.filename().string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string folderPathString = folderPath.string();
                        if (DrawAssetTile(folderPathString.c_str(), label.c_str(), ICON_FA_FOLDER_OPEN, kAssetFolderPayload, folderPathString.c_str(), folderPathString.size() + 1u, g_SelectedAssetId == folderPathString, ImTextureID{}))
                            QueueAssetSelection(folderPathString);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            g_AssetCurrentFolder = folderPath;
                            QueueAssetSelection(folderPathString);
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            auto tryMovePayload = [&](const char* payloadType)
                            {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadType))
                                {
                                    const char* sourcePath = static_cast<const char*>(payload->Data);
                                    if (sourcePath && *sourcePath)
                                    {
                                        std::filesystem::path movedPath;
                                        if (MoveAssetFileToFolder(sourcePath, folderPath, movedPath))
                                            QueueAssetSelection(movedPath.string());
                                    }
                                }
                            };
                            tryMovePayload(kSceneCreatePrefabPayload);
                            tryMovePayload(kAssetTexturePayload);
                            tryMovePayload(kAssetScenePayload);
                            ImGui::EndDragDropTarget();
                        }

                        const std::string folderContextId = std::format("##FolderContext:{}", folderPathString);
                        ImGui::OpenPopupOnItemClick(folderContextId.c_str(), ImGuiPopupFlags_MouseButtonRight);
                        if (ImGui::BeginPopup(folderContextId.c_str()))
                        {
                            if (ImGui::MenuItem("Rename"))
                            {
                                g_PendingFolderRenamePath = folderPath;
                                strncpy_s(g_FolderRenameBuffer, label.c_str(), _TRUNCATE);
                                g_RequestOpenRenameFolderPopup = true;
                            }
                            std::string deleteReason;
                            const bool canDeleteFolder = CanDeleteAssetPath(folderPath, true, deleteReason);
                            const char* deleteFolderLabel = canDeleteFolder
                                ? "Delete"
                                : (deleteReason.find("currently loaded scene") != std::string::npos ? "Delete (Current Scene Locked)" : "Delete (Locked)");
                            if (ImGui::MenuItem(deleteFolderLabel, nullptr, false, canDeleteFolder))
                            {
                                g_PendingDeletePath = folderPath;
                                g_PendingDeleteIsFolder = true;
                                g_PendingDeleteKindLabel = "folder";
                                g_RequestOpenDeleteAssetPopup = true;
                            }
                            ImGui::EndPopup();
                        }

                        ++tileIndex;
                        anyFolderShown = true;
                    }

                    if (!anyFolderShown)
                    {
                        if (assetFolders.empty())
                            ImGui::TextDisabled("No top-level folders were found under Assets.");
                        else
                            ImGui::TextDisabled("No folders match the current filter.");
                    }
                }

                if (showPrimitives)
                {
                    DrawAssetSectionHeader("Primitives");
                    int primitiveTileIndex = 0;
                    auto drawPrimitive = [&](const char* assetId, const char* label, const char* payloadType, const char* icon = ICON_FA_CUBE)
                    {
                        if (!matchesFilter(label))
                            return;
                        if (primitiveTileIndex > 0 && (primitiveTileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);
                        const ImTextureID thumb = GetAssetThumbnailTexture(assetId, "Assets/Textures/crate.png");
                        if (DrawAssetTile(assetId, label, icon, payloadType, label, std::strlen(label) + 1u, g_SelectedAssetId == assetId, thumb))
                            QueueAssetSelection(assetId);
                        ++primitiveTileIndex;
                    };

                    drawPrimitive("PrimitiveEmpty", "Empty Object", kSceneCreateEmptyPayload, ICON_FA_SITEMAP);
                    drawPrimitive("PrimitiveCube", "Cube", kSceneCreateCubePayload);
                    drawPrimitive("PrimitiveSphere", "Sphere", kSceneCreateSpherePayload);
                    drawPrimitive("PrimitivePlane", "Plane", kSceneCreatePlanePayload);
                    drawPrimitive("PrimitiveCylinder", "Cylinder", kSceneCreateCylinderPayload);
                    drawPrimitive("PrimitiveCapsule", "Capsule", kSceneCreateCapsulePayload);
                    drawPrimitive("PrimitiveTorus", "Torus", kSceneCreateTorusPayload);
                    drawPrimitive("PrimitiveCone", "Cone", kSceneCreateConePayload);

                    if (primitiveTileIndex == 0)
                        ImGui::TextDisabled("No primitive assets match the filter.");
                }

                if (showPrefabs)
                {
                    DrawAssetSectionHeader("Prefabs");
                    const auto prefabAssets = EnumeratePrefabAssets();
                    int tileIndex = 0;
                    bool anyPrefabShown = false;
                    for (const auto& prefabPath : prefabAssets)
                    {
                        if (!IsDirectChildOfFolder(prefabPath, currentFolder))
                            continue;
                        const std::string label = prefabPath.filename().string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string prefabPathString = prefabPath.string();
                        const ImTextureID prefabThumb = GetAssetThumbnailTexture(prefabPathString, "Assets/Textures/crate.png");
                        if (DrawAssetTile(prefabPathString.c_str(), label.c_str(), ICON_FA_CUBE, kSceneCreatePrefabPayload, prefabPathString.c_str(), prefabPathString.size() + 1u, g_SelectedAssetId == prefabPathString, prefabThumb))
                            QueueAssetSelection(prefabPathString);
                        const std::string prefabContextId = std::format("##PrefabContext:{}", prefabPathString);
                        if (ImGui::BeginPopupContextItem(prefabContextId.c_str()))
                        {
                            if (ImGui::MenuItem("Duplicate"))
                            {
                                std::filesystem::path duplicatedPath;
                                if (DuplicateAssetFile(prefabPath, duplicatedPath))
                                    QueueAssetSelection(duplicatedPath.string());
                            }
                            if (ImGui::MenuItem("Rename"))
                            {
                                g_PendingAssetRenamePath = prefabPath;
                                g_PendingAssetRenameKindLabel = "prefab";
                                strncpy_s(g_AssetRenameBuffer, prefabPath.stem().string().c_str(), _TRUNCATE);
                                g_RequestOpenRenameAssetPopup = true;
                            }
                            if (ImGui::MenuItem("Delete"))
                            {
                                g_PendingDeletePath = prefabPath;
                                g_PendingDeleteIsFolder = false;
                                g_PendingDeleteKindLabel = "prefab";
                                g_RequestOpenDeleteAssetPopup = true;
                            }
                            ImGui::EndPopup();
                        }
                        ++tileIndex;
                        anyPrefabShown = true;
                    }

                    if (!anyPrefabShown)
                    {
                        if (prefabAssets.empty())
                            ImGui::TextDisabled("No prefab assets were found under Assets.");
                        else
                            ImGui::TextDisabled("No prefab assets match the current folder/filter.");
                    }
                }

                if (showScenes)
                {
                    DrawAssetSectionHeader("Scenes");
                    if (ImGui::Button("Save Current Scene##Assets"))
                        UI::SaveCurrentSceneAsset();
                    ImGui::SameLine();
                    if (ImGui::Button("Save Scene As...##Assets"))
                        UI::SaveCurrentSceneAssetAs();
                    const auto sceneAssets = EnumerateSceneAssets();
                    int tileIndex = 0;
                    bool anySceneShown = false;
                    for (const auto& scenePath : sceneAssets)
                    {
                        if (!IsDirectChildOfFolder(scenePath, currentFolder))
                            continue;
                        const std::string label = scenePath.filename().string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string scenePathString = scenePath.string();
                        if (DrawAssetTile(scenePathString.c_str(), label.c_str(), ICON_FA_FOLDER_OPEN, kAssetScenePayload, scenePathString.c_str(), scenePathString.size() + 1u, g_SelectedAssetId == scenePathString, ImTextureID{}))
                            QueueAssetSelection(scenePathString);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            if (UI::LoadSceneAssetFromPath(scenePathString))
                                QueueAssetSelection(scenePathString);
                        }
                        const std::string sceneContextId = std::format("##SceneContext:{}", scenePathString);
                        ImGui::OpenPopupOnItemClick(sceneContextId.c_str(), ImGuiPopupFlags_MouseButtonRight);
                        if (ImGui::BeginPopup(sceneContextId.c_str()))
                        {
                            if (ImGui::MenuItem("Duplicate"))
                            {
                                std::filesystem::path duplicatedPath;
                                if (DuplicateAssetFile(scenePath, duplicatedPath))
                                    QueueAssetSelection(duplicatedPath.string());
                            }
                            if (ImGui::MenuItem("Rename"))
                            {
                                g_PendingSceneRenamePath = scenePath;
                                strncpy_s(g_SceneRenameBuffer, scenePath.stem().string().c_str(), _TRUNCATE);
                                g_RequestOpenRenameScenePopup = true;
                            }
                            std::string deleteReason;
                            const bool canDeleteScene = CanDeleteAssetPath(scenePath, false, deleteReason);
                            const char* deleteSceneLabel = canDeleteScene
                                ? "Delete"
                                : (deleteReason.find("currently loaded scene") != std::string::npos ? "Delete (Current Scene Locked)" : "Delete (Locked)");
                            if (ImGui::MenuItem(deleteSceneLabel, nullptr, false, canDeleteScene))
                            {
                                g_PendingDeletePath = scenePath;
                                g_PendingDeleteIsFolder = false;
                                g_PendingDeleteKindLabel = "scene";
                                g_RequestOpenDeleteAssetPopup = true;
                            }
                            ImGui::EndPopup();
                        }
                        ++tileIndex;
                        anySceneShown = true;
                    }

                    if (!anySceneShown)
                    {
                        if (sceneAssets.empty())
                            ImGui::TextDisabled("No scene files were found under Assets.");
                        else
                            ImGui::TextDisabled("No scene assets match the current folder/filter.");
                    }
                }

                if (showTextures)
                {
                    DrawAssetSectionHeader("Textures");
                    const auto textureAssets = EnumerateTextureAssets();
                    int tileIndex = 0;
                    bool anyTextureShown = false;
                    for (const auto& texturePath : textureAssets)
                    {
                        if (!IsDirectChildOfFolder(texturePath, currentFolder))
                            continue;
                        const std::string label = texturePath.filename().string();
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string texturePathString = texturePath.string();
                        const ImTextureID textureThumb = GetAssetThumbnailTexture(texturePathString, texturePathString);
                        if (DrawAssetTile(texturePathString.c_str(), label.c_str(), ICON_FA_FOLDER_OPEN, kAssetTexturePayload, texturePathString.c_str(), texturePathString.size() + 1u, g_SelectedAssetId == texturePathString, textureThumb))
                            QueueAssetSelection(texturePathString);
                        const std::string textureContextId = std::format("##TextureContext:{}", texturePathString);
                        if (ImGui::BeginPopupContextItem(textureContextId.c_str()))
                        {
                            if (ImGui::MenuItem("Duplicate"))
                            {
                                std::filesystem::path duplicatedPath;
                                if (DuplicateAssetFile(texturePath, duplicatedPath))
                                {
                                    QueueAssetSelection(duplicatedPath.string());
                                    g_AssetThumbnailCache.erase(duplicatedPath.string());
                                }
                            }
                            if (ImGui::MenuItem("Rename"))
                            {
                                g_PendingAssetRenamePath = texturePath;
                                g_PendingAssetRenameKindLabel = "texture";
                                strncpy_s(g_AssetRenameBuffer, texturePath.stem().string().c_str(), _TRUNCATE);
                                g_RequestOpenRenameAssetPopup = true;
                            }
                            if (ImGui::MenuItem("Delete"))
                            {
                                g_PendingDeletePath = texturePath;
                                g_PendingDeleteIsFolder = false;
                                g_PendingDeleteKindLabel = "texture";
                                g_RequestOpenDeleteAssetPopup = true;
                            }
                            ImGui::EndPopup();
                        }
                        ++tileIndex;
                        anyTextureShown = true;
                    }

                    if (!anyTextureShown)
                    {
                        if (textureAssets.empty())
                            ImGui::TextDisabled("No textures were found under Assets.");
                        else
                            ImGui::TextDisabled("No texture assets match the current folder/filter.");
                    }
                }

                if (showMaterials)
                {
                    DrawAssetSectionHeader("Materials");
                    MaterialManager& materialManager = MaterialManager::GetInstance();
                    int tileIndex = 0;
                    bool anyMaterialShown = false;
                    extern Engine* g_engineInstance;
                    for (int i = 0; i < materialManager.GetMaterialCount(); ++i)
                    {
                        const std::string* materialNamePtr = materialManager.GetMaterialNameByIndex(i);
                        Material* material = materialManager.GetMaterialByIndex(i);
                        if (!materialNamePtr || !material)
                            continue;

                        const std::string label = *materialNamePtr;
                        if (!matchesFilter(label))
                            continue;

                        if (tileIndex > 0 && (tileIndex % columns) != 0)
                            ImGui::SameLine(0.0f, kTileSpacing);

                        const std::string materialId = std::format("Material:{}", label);
                        const std::string thumbPath = (material->albedo && !material->albedo->sourcePath.empty()) ? material->albedo->sourcePath : "Assets/Textures/crate.png";
                        const ImTextureID materialThumb = GetAssetThumbnailTexture(materialId, thumbPath);
                        const int materialPayloadIndex = i;
                        if (DrawAssetTile(materialId.c_str(), label.c_str(), ICON_FA_GEAR, kAssetMaterialPayload, &materialPayloadIndex, sizeof(materialPayloadIndex), g_SelectedAssetId == materialId, materialThumb))
                        {
                            QueueAssetSelection(materialId);
                            if (g_engineInstance)
                            {
                                EditorState& editor = g_engineInstance->GetEditorState();
                                editor.focusedMaterialIndex = i;
                                Inspector().open = true;
                                MaterialPreview().open = true;
                            }
                        }
                        const std::string materialContextId = std::format("##MaterialContext:{}", materialId);
                        ImGui::OpenPopupOnItemClick(materialContextId.c_str(), ImGuiPopupFlags_MouseButtonRight);
                        if (ImGui::BeginPopup(materialContextId.c_str()))
                        {
                            if (ImGui::MenuItem("Duplicate"))
                            {
                                Material* duplicatedMaterial = materialManager.DuplicateMaterial(*material, std::format("{}_Copy", label));
                                if (duplicatedMaterial)
                                {
                                    const int duplicatedIndex = materialManager.GetMaterialCount() - 1;
                                    QueueMaterialSelection(duplicatedIndex, duplicatedMaterial->name);
                                }
                            }
                            if (ImGui::MenuItem("Rename"))
                            {
                                g_PendingMaterialRenameIndex = i;
                                strncpy_s(g_MaterialRenameBuffer, label.c_str(), _TRUNCATE);
                                g_RequestOpenRenameMaterialPopup = true;
                            }

                            const bool canDeleteMaterial = materialManager.GetMaterialCount() > 1;
                            if (ImGui::MenuItem(canDeleteMaterial ? "Delete" : "Delete (Last Material Locked)", nullptr, false, canDeleteMaterial))
                            {
                                g_PendingMaterialDeleteIndex = i;
                                g_RequestOpenDeleteMaterialPopup = true;
                            }
                            ImGui::EndPopup();
                        }
                        ++tileIndex;
                        anyMaterialShown = true;
                    }

                    if (!anyMaterialShown)
                        ImGui::TextDisabled("No material assets match the filter.");
                }

                if (g_RequestOpenRenameFolderPopup)
                {
                    ImGui::OpenPopup("Rename Folder##Assets");
                    g_RequestOpenRenameFolderPopup = false;
                }
                if (g_RequestOpenRenameScenePopup)
                {
                    ImGui::OpenPopup("Rename Scene##Assets");
                    g_RequestOpenRenameScenePopup = false;
                }
                if (g_RequestOpenRenameAssetPopup)
                {
                    ImGui::OpenPopup("Rename Asset##Assets");
                    g_RequestOpenRenameAssetPopup = false;
                }
                if (g_RequestOpenRenameMaterialPopup)
                {
                    ImGui::OpenPopup("Rename Material##Assets");
                    g_RequestOpenRenameMaterialPopup = false;
                }
                if (g_RequestOpenDeleteMaterialPopup)
                {
                    ImGui::OpenPopup("Delete Material##Assets");
                    g_RequestOpenDeleteMaterialPopup = false;
                }
                if (g_RequestOpenDeleteAssetPopup)
                {
                    ImGui::OpenPopup("Delete Item##Assets");
                    g_RequestOpenDeleteAssetPopup = false;
                }

                if (ImGui::BeginPopupModal("Rename Folder##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Rename folder");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::InputText("##FolderRenameInput", g_FolderRenameBuffer, IM_ARRAYSIZE(g_FolderRenameBuffer));

                    if (ImGui::Button("Rename##FolderConfirm"))
                    {
                        std::filesystem::path renamedPath;
                        if (RenameAssetFolder(g_PendingFolderRenamePath, g_FolderRenameBuffer, renamedPath))
                        {
                            if (g_SelectedAssetId == g_PendingFolderRenamePath.string())
                                QueueAssetSelection(renamedPath.string());
                            g_PendingFolderRenamePath.clear();
                            g_FolderRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##FolderRename"))
                    {
                        g_PendingFolderRenamePath.clear();
                        g_FolderRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("Rename Scene##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Rename scene");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::InputText("##SceneRenameInput", g_SceneRenameBuffer, IM_ARRAYSIZE(g_SceneRenameBuffer));

                    if (ImGui::Button("Rename##SceneConfirm"))
                    {
                        std::filesystem::path renamedPath;
                        if (RenameSceneAsset(g_PendingSceneRenamePath, g_SceneRenameBuffer, renamedPath))
                        {
                            if (g_SelectedAssetId == g_PendingSceneRenamePath.string())
                                QueueAssetSelection(renamedPath.string());
                            if (UI::GetCurrentSceneAssetPath() == g_PendingSceneRenamePath.string())
                                UI::SetCurrentSceneAssetPath(renamedPath.string());
                            g_PendingSceneRenamePath.clear();
                            g_SceneRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##SceneRename"))
                    {
                        g_PendingSceneRenamePath.clear();
                        g_SceneRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("Rename Asset##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Rename %s", g_PendingAssetRenameKindLabel ? g_PendingAssetRenameKindLabel : "asset");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::InputText("##AssetRenameInput", g_AssetRenameBuffer, IM_ARRAYSIZE(g_AssetRenameBuffer));

                    if (ImGui::Button("Rename##AssetConfirm"))
                    {
                        std::filesystem::path renamedPath;
                        if (RenameAssetFile(g_PendingAssetRenamePath, g_AssetRenameBuffer, renamedPath))
                        {
                            if (g_SelectedAssetId == g_PendingAssetRenamePath.string())
                                QueueAssetSelection(renamedPath.string());
                            g_AssetThumbnailCache.erase(g_PendingAssetRenamePath.string());
                            g_AssetThumbnailCache.erase(renamedPath.string());
                            g_PendingAssetRenamePath.clear();
                            g_PendingAssetRenameKindLabel = "asset";
                            g_AssetRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##AssetRename"))
                    {
                        g_PendingAssetRenamePath.clear();
                        g_PendingAssetRenameKindLabel = "asset";
                        g_AssetRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("Rename Material##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Rename material");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(320.0f);
                    ImGui::InputText("##MaterialRenameInput", g_MaterialRenameBuffer, IM_ARRAYSIZE(g_MaterialRenameBuffer));

                    if (ImGui::Button("Rename##MaterialConfirm"))
                    {
                        std::string renamedName;
                        if (MaterialManager::GetInstance().RenameMaterialByIndex(g_PendingMaterialRenameIndex, g_MaterialRenameBuffer, renamedName))
                        {
                            QueueMaterialSelection(g_PendingMaterialRenameIndex, renamedName);
                            g_PendingMaterialRenameIndex = -1;
                            g_MaterialRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##MaterialRename"))
                    {
                        g_PendingMaterialRenameIndex = -1;
                        g_MaterialRenameBuffer[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("Delete Material##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    extern Engine* g_engineInstance;
                    MaterialManager& materialManager = MaterialManager::GetInstance();
                    const std::string* materialNamePtr = (g_PendingMaterialDeleteIndex >= 0 && g_PendingMaterialDeleteIndex < materialManager.GetMaterialCount())
                        ? materialManager.GetMaterialNameByIndex(g_PendingMaterialDeleteIndex)
                        : nullptr;
                    const std::string materialName = materialNamePtr ? *materialNamePtr : std::string("<Unknown>");

                    ImGui::TextUnformatted("Delete material?");
                    ImGui::TextWrapped("%s", materialName.c_str());
                    ImGui::Separator();

                    if (ImGui::Button("Delete##MaterialConfirm"))
                    {
                        const int deletedIndex = g_PendingMaterialDeleteIndex;
                        if (materialManager.DeleteMaterialByIndex(deletedIndex))
                        {
                            if (g_engineInstance)
                            {
                                EditorState& editor = g_engineInstance->GetEditorState();
                                if (editor.focusedMaterialIndex == deletedIndex)
                                    editor.focusedMaterialIndex = -1;
                                else if (editor.focusedMaterialIndex > deletedIndex)
                                    --editor.focusedMaterialIndex;
                            }

                            if (materialManager.GetMaterialCount() > 0)
                            {
                                const int nextIndex = (std::min)(deletedIndex, materialManager.GetMaterialCount() - 1);
                                if (const std::string* nextMaterialName = materialManager.GetMaterialNameByIndex(nextIndex))
                                    QueueMaterialSelection(nextIndex, *nextMaterialName);
                                else
                                    g_SelectedAssetId.clear();
                            }
                            else
                            {
                                g_SelectedAssetId.clear();
                            }

                            g_PendingMaterialDeleteIndex = -1;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##MaterialDelete"))
                    {
                        g_PendingMaterialDeleteIndex = -1;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                if (ImGui::BeginPopupModal("Delete Item##Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Delete %s?", g_PendingDeleteKindLabel ? g_PendingDeleteKindLabel : "asset");
                    ImGui::TextWrapped("%s", g_PendingDeletePath.string().c_str());
                    ImGui::Separator();

                    if (ImGui::Button("Delete##AssetConfirm"))
                    {
                        const std::string deletedPath = g_PendingDeletePath.string();
                        if (DeleteAssetPath(g_PendingDeletePath, g_PendingDeleteIsFolder))
                        {
                            if (g_SelectedAssetId == deletedPath)
                            {
                                const std::string fallbackSelection = FindFirstSelectableAssetIdInFolder(g_AssetCurrentFolder);
                                if (!fallbackSelection.empty())
                                    QueueAssetSelection(fallbackSelection);
                                else
                                    g_SelectedAssetId.clear();
                            }
                            g_PendingDeletePath.clear();
                            g_PendingDeleteIsFolder = false;
                            g_PendingDeleteKindLabel = "Asset";
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel##AssetDelete"))
                    {
                        g_PendingDeletePath.clear();
                        g_PendingDeleteIsFolder = false;
                        g_PendingDeleteKindLabel = "Asset";
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                ImGui::EndChild();
            }
            ImGui::End();
        }
    };

    static EditorPanel g_debugOverlay{
        "Debug Overlay",
        false,
        []()
        {
            auto& panel = DebugOverlay();
            if (!panel.open)
                return;

            uint32_t targetInstanceCount = Scene::GetTargetInstanceCount();
            int instanceCount = (int)targetInstanceCount;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (ImGui::SliderInt("Instance Count", &instanceCount, 1, 10000))
                    Scene::SetTargetInstanceCount((uint32_t)instanceCount);

                ImGui::Separator();
                ImGui::Text("Visible: %u", (unsigned)Scene::GetVisibleInstanceCount());
                ImGui::Text("Total:   %u", (unsigned)Scene::GetInstanceCount());
            }
            ImGui::End();
        }
    };

    static EditorPanel g_instancing{
        "Instancing",
        false,
        []()
        {
            auto& panel = Instancing();
            if (!panel.open)
                return;

            uint32_t targetInstanceCount = Scene::GetTargetInstanceCount();
            int instanceCount = (int)targetInstanceCount;

            if (ImGui::Begin(panel.name, &panel.open))
            {
                if (ImGui::SliderInt("Instance Count", &instanceCount, 1, 10000))
                    Scene::SetTargetInstanceCount((uint32_t)instanceCount);

                ImGui::Separator();
                ImGui::Text("Visible: %u", (unsigned)Scene::GetVisibleInstanceCount());
                ImGui::Text("Total:   %u", (unsigned)Scene::GetInstanceCount());
            }
            ImGui::End();
        }
    };

    EditorPanel& Scene() { return g_scene; }
    EditorPanel& Game() { return g_game; }
    EditorPanel& Hierarchy() { return g_hierarchy; }
    EditorPanel& Inspector() { return g_inspector; }
    EditorPanel& Assets() { return g_assets; }
    EditorPanel& DebugOverlay() { return g_debugOverlay; }
    EditorPanel& Diagnostics() { return g_diagnostics; }
    EditorPanel& LogViewer() { return g_logViewer; }
    EditorPanel& Instancing() { return g_instancing; }

    void DrawAll()
    {
        if (g_scene.open) g_scene.draw();
        if (g_game.open) g_game.draw();
        if (g_hierarchy.open) g_hierarchy.draw();
        if (g_inspector.open) g_inspector.draw();
        if (g_assets.open) g_assets.draw();
        if (g_instancing.open) g_instancing.draw();
        if (g_debugOverlay.open) g_debugOverlay.draw();
        if (g_diagnostics.open) g_diagnostics.draw();
        if (g_logViewer.open) g_logViewer.draw();

        if (g_engineInstance &&
            Engine::GetState() == Engine::State::Editing &&
            !Engine::IsKeyboardCapturedByUI() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            Scene::GetSelectedInstance() != nullptr &&
            ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            EditorState& editor = g_engineInstance->GetEditorState();
            DeleteCurrentSelection(editor);
        }
    }

    bool ExecuteDuplicateSelectionCommand()
    {
        if (!g_engineInstance)
            return false;
        return DuplicateCurrentSelection(g_engineInstance->GetEditorState());
    }

    bool ExecuteFocusSelectionCommand()
    {
        if (!g_engineInstance)
            return false;

        EditorState& editor = g_engineInstance->GetEditorState();
        DirectX::XMFLOAT3 focusPoint{};
        if (!TryGetCurrentSelectionAnchor(editor, focusPoint))
            return false;

        Graphics::GetInstance().GetSceneCamera().FocusOnPoint(focusPoint, ComputeSelectionFocusDistance(editor));
        editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return true;
    }

    bool ExecuteRenameSelectionCommand()
    {
        if (!g_engineInstance)
            return false;

        SceneInstance* selected = Scene::GetSelectedInstance();
        if (!selected)
            return false;

        g_PendingSceneInstanceRenameId = selected->instanceId;
        strncpy_s(g_SceneInstanceRenameBuffer, selected->name.c_str(), _TRUNCATE);
        g_RequestOpenSceneInstanceRenamePopup = true;
        return true;
    }

    bool ExecuteCreateEmptyParentCommand()
    {
        if (!g_engineInstance)
            return false;
        return CreateEmptyParentForSelection(g_engineInstance->GetEditorState());
    }

    bool ExecuteSaveSelectionAsPrefabCommand()
    {
        if (!g_engineInstance)
            return false;
        return SaveCurrentSelectionAsPrefab(g_engineInstance->GetEditorState());
    }

    bool ExecuteUnpackPrefabCommand()
    {
        if (!g_engineInstance)
            return false;

        EditorState& editor = g_engineInstance->GetEditorState();
        if (!SelectionContainsPrefabInstance())
            return false;

        const bool success = ExecuteEditorCommand(editor, std::make_unique<UnpackPrefabCommand>(Scene::GetSelectedInstanceIds()));
        if (success)
            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return success;
    }

    bool CanUnpackPrefabSelection()
    {
        if (!g_engineInstance)
            return false;
        return SelectionContainsPrefabInstance();
    }

    bool ExecuteUndoCommand()
    {
        if (!g_engineInstance)
            return false;

        EditorState& editor = g_engineInstance->GetEditorState();
        const bool success = PerformUndo(editor);
        if (success)
            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return success;
    }

    bool ExecuteRedoCommand()
    {
        if (!g_engineInstance)
            return false;

        EditorState& editor = g_engineInstance->GetEditorState();
        const bool success = PerformRedo(editor);
        if (success)
            editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
        return success;
    }

    bool CanUndoCommand()
    {
        if (!g_engineInstance)
            return false;
        return !g_engineInstance->GetEditorState().undoEntryKinds.empty();
    }

    bool CanRedoCommand()
    {
        if (!g_engineInstance)
            return false;
        return !g_engineInstance->GetEditorState().redoEntryKinds.empty();
    }

    bool ExecuteDeleteSelectionCommand()
    {
        if (!g_engineInstance)
            return false;
        return DeleteCurrentSelection(g_engineInstance->GetEditorState());
    }

    static int GetAssetTileColumnCount(float availableWidth, float tileWidth, float spacing)
    {
        return (std::max)(1, (int)((availableWidth + spacing) / (tileWidth + spacing)));
    }

    static ImTextureID GetAssetThumbnailTexture(const std::string& assetId, const std::string& fallbackTexturePath)
    {
        if (assetId.empty() && fallbackTexturePath.empty())
            return 0;

        const std::string cacheKey = assetId.empty() ? fallbackTexturePath : assetId;
        if (auto it = g_AssetThumbnailCache.find(cacheKey); it != g_AssetThumbnailCache.end())
            return it->second;

        auto tryLoadTexture = [](const std::string& path) -> ImTextureID
        {
            if (path.empty())
                return 0;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec)
                return 0;
            if (Texture* texture = TextureManager::GetInstance().LoadTexture(path))
            {
                if (texture->srvGPU.ptr != 0)
                    return (ImTextureID)texture->srvGPU.ptr;
            }
            return 0;
        };

        ImTextureID thumbnail = 0;
        thumbnail = tryLoadTexture(assetId);
        if (!thumbnail)
            thumbnail = tryLoadTexture(fallbackTexturePath);
        if (!thumbnail && assetId != fallbackTexturePath)
            thumbnail = tryLoadTexture("Assets/Textures/crate.png");

        g_AssetThumbnailCache[cacheKey] = thumbnail;
        return thumbnail;
    }

    static bool DrawAssetTile(const char* id, const char* title, const char* icon, const char* payloadType, const void* payloadData, size_t payloadSize, bool selected, ImTextureID thumbnailTexture)
    {
        constexpr float kTileWidth = 118.0f;
        constexpr float kTileHeight = 92.0f;
        constexpr float kCornerRounding = 8.0f;

        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##AssetTile", ImVec2(kTileWidth, kTileHeight));

        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const ImU32 bg = selected
            ? IM_COL32(64, 96, 150, 235)
            : (active ? IM_COL32(82, 112, 156, 230) : (hovered ? IM_COL32(54, 58, 66, 235) : IM_COL32(36, 38, 43, 220)));
        const ImU32 border = selected
            ? IM_COL32(140, 190, 255, 255)
            : (hovered ? IM_COL32(110, 160, 235, 255) : IM_COL32(78, 82, 90, 255));
        const ImU32 iconColor = (hovered || selected) ? IM_COL32(255, 255, 255, 255) : IM_COL32(224, 228, 236, 255);
        const ImU32 textColor = IM_COL32(240, 242, 246, 255);
        const ImU32 subTextColor = selected ? IM_COL32(210, 226, 255, 255) : IM_COL32(150, 156, 168, 255);

        draw->AddRectFilled(min, max, bg, kCornerRounding);
        draw->AddRect(min, max, border, kCornerRounding, 0, selected ? 2.5f : (hovered ? 2.0f : 1.0f));

        const ImVec2 previewMin(min.x + 8.0f, min.y + 8.0f);
        const ImVec2 previewMax(max.x - 8.0f, min.y + 48.0f);
        draw->AddRectFilled(previewMin, previewMax, IM_COL32(24, 26, 31, 235), 6.0f);
        draw->AddRect(previewMin, previewMax, IM_COL32(255, 255, 255, 12), 6.0f, 0, 1.0f);

        if (thumbnailTexture)
        {
            draw->AddImage(thumbnailTexture, previewMin, previewMax, ImVec2(0, 0), ImVec2(1, 1));
            draw->AddRect(previewMin, previewMax, IM_COL32(255, 255, 255, 28), 6.0f, 0, 1.0f);
        }
        else
        {
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 iconPos(previewMin.x + ((previewMax.x - previewMin.x) - iconSize.x) * 0.5f, previewMin.y + ((previewMax.y - previewMin.y) - iconSize.y) * 0.5f);
            draw->AddText(iconPos, iconColor, icon);
        }

        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        const ImVec2 titlePos(min.x + 10.0f, min.y + 58.0f);
        const char* typeLabel = "Asset";
        if (payloadType && (std::strcmp(payloadType, kSceneCreateCubePayload) == 0 || std::strcmp(payloadType, kSceneCreateEmptyPayload) == 0))
            typeLabel = "Primitive";
        else if (payloadType && std::strcmp(payloadType, kSceneCreatePrefabPayload) == 0)
            typeLabel = "Prefab";
        else if (payloadType && std::strcmp(payloadType, kAssetTexturePayload) == 0)
            typeLabel = "Texture";
        else if (payloadType && std::strcmp(payloadType, kAssetMaterialPayload) == 0)
            typeLabel = "Material";
        else if (payloadType && std::strcmp(payloadType, kAssetFolderPayload) == 0)
            typeLabel = "Folder";
        else if (payloadType && std::strcmp(payloadType, kAssetScenePayload) == 0)
            typeLabel = "Scene";
        const ImVec2 typeSize = ImGui::CalcTextSize(typeLabel);
        const ImVec2 typePos(min.x + 10.0f, max.y - typeSize.y - 8.0f);

        draw->AddText(titlePos, textColor, title);
        draw->AddText(typePos, subTextColor, typeLabel);

        if (titleSize.x > (kTileWidth - 20.0f))
            draw->AddLine(ImVec2(min.x + 10.0f, min.y + 74.0f), ImVec2(max.x - 10.0f, min.y + 74.0f), IM_COL32(255, 255, 255, 18), 1.0f);

        if (payloadType && payloadData && payloadSize > 0 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload(payloadType, payloadData, payloadSize);
            ImGui::TextUnformatted(title);
            ImGui::EndDragDropSource();
        }

        if (!g_PendingAssetRevealId.empty() && g_PendingAssetRevealId == id)
        {
            ImGui::SetScrollHereY(0.35f);
            g_PendingAssetRevealId.clear();
        }

        ImGui::PopID();
        return clicked;
    }

    static void QueueAssetSelection(const std::string& assetId)
    {
        g_SelectedAssetId = assetId;
        g_PendingAssetRevealId = assetId;
    }

    void RevealAssetInBrowser(const std::string& assetId)
    {
        if (assetId.empty())
            return;
        QueueAssetSelection(assetId);
        Assets().open = true;
    }

    static void QueueMaterialSelection(int materialIndex, const std::string& materialName)
    {
        QueueAssetSelection(std::format("Material:{}", materialName));
        if (g_engineInstance)
            g_engineInstance->GetEditorState().focusedMaterialIndex = materialIndex;
    }

    static SceneInstance* FindSceneInstanceById(uint32_t instanceId)
    {
        if (instanceId == 0)
            return nullptr;
        const auto& instances = Scene::GetInstances();
        for (size_t i = 0; i < instances.size(); ++i)
        {
            SceneInstance* instance = Scene::GetInstance(i);
            if (instance && instance->instanceId == instanceId)
                return instance;
        }
        return nullptr;
    }

    static bool CanAssignMaterialToInstance(const SceneInstance* instance)
    {
        return instance && !instance->camera.enabled && instance->primitive != ScenePrimitive::Empty;
    }

    static bool ApplyMaterialToSceneInstance(EditorState& editor, SceneInstance* instance, int materialIndex)
    {
        MaterialManager& materials = MaterialManager::GetInstance();
        if (!CanAssignMaterialToInstance(instance))
            return false;
        if (!materials.GetMaterialByIndex(materialIndex))
            return false;
        if (instance->materialIndex == materialIndex)
            return false;

        PushUndoSnapshot(editor);
        instance->materialIndex = materialIndex;
        Scene::SetSelectedInstanceId(instance->instanceId);
        if (const std::string* materialName = materials.GetMaterialNameByIndex(materialIndex))
            QueueMaterialSelection(materialIndex, *materialName);
        Scene::RebuildRenderInstancesFromSceneData();
        Scene::MarkInstancesDirty();
        return true;
    }

    static void DrawSceneMaterialDropOverlay(ImDrawList* drawList, ImVec2 imageMin, ImVec2 imageSize)
    {
        if (!drawList)
            return;

        const ImGuiPayload* activePayload = ImGui::GetDragDropPayload();
        if (!activePayload || !activePayload->IsDataType(kAssetMaterialPayload) || activePayload->DataSize != sizeof(int))
            return;

        const int materialIndex = *static_cast<const int*>(activePayload->Data);
        MaterialManager& materials = MaterialManager::GetInstance();
        const std::string materialLabel = [&]()
        {
            if (const std::string* materialName = materials.GetMaterialNameByIndex(materialIndex))
                return *materialName;
            return std::string("Material");
        }();

        SceneInstance* target = nullptr;
        const uint32_t hoveredId = Scene::GetHoveredInstanceId();
        if (hoveredId != 0)
            target = FindSceneInstanceById(hoveredId);
        if (!CanAssignMaterialToInstance(target))
            target = Scene::GetSelectedInstance();
        if (!CanAssignMaterialToInstance(target))
            target = nullptr;

        const ImVec2 panelMin(imageMin.x + 16.0f, imageMin.y + 16.0f);
        const ImVec2 panelMax(panelMin.x + 300.0f, panelMin.y + (target ? 56.0f : 74.0f));
        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(10, 12, 18, 210), 10.0f);
        drawList->AddRect(panelMin, panelMax, IM_COL32(96, 168, 255, 180), 10.0f, 0, 1.4f);
        drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 10.0f), IM_COL32(255, 255, 255, 235), materialLabel.c_str());
        const char* hintText = target
            ? "Release to assign material to target object"
            : "Hover or select a renderable object to assign this material";
        drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 30.0f), IM_COL32(184, 192, 208, 220), hintText);

        if (!target)
            return;

        CameraData camera{};
        if (!Scene::TryGetLastRenderCameraData(camera))
            return;

        ImVec2 targetScreen{};
        if (!ProjectWorldToSceneScreen(target->position, camera, imageMin, imageSize, targetScreen))
            return;

        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        const float radius = 22.0f + pulse * 6.0f;
        drawList->AddCircleFilled(targetScreen, radius - 6.0f, IM_COL32(96, 168, 255, 34), 28);
        drawList->AddCircle(targetScreen, radius, IM_COL32(96, 168, 255, 240), 28, 2.5f);
        drawList->AddCircle(targetScreen, radius + 8.0f, IM_COL32(18, 22, 30, 220), 28, 1.5f);
        const std::string targetLabel = std::format("Drop Material -> {}", target->name);
        drawList->AddText(ImVec2(targetScreen.x + radius + 8.0f, targetScreen.y - 8.0f), IM_COL32(96, 168, 255, 240), targetLabel.c_str());
    }

    static bool ProjectWorldToSceneScreen(const DirectX::XMFLOAT3& worldPos, const CameraData& camera, ImVec2 sceneMin, ImVec2 sceneSize, ImVec2& outScreen)
    {
        using namespace DirectX;

        const XMMATRIX view = XMLoadFloat4x4(&camera.view);
        const XMMATRIX proj = XMLoadFloat4x4(&camera.proj);
        const XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        const XMVECTOR clip = XMVector4Transform(pos, XMMatrixMultiply(view, proj));

        const float w = XMVectorGetW(clip);
        if (fabsf(w) < 1e-6f)
            return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        const float ndcZ = XMVectorGetZ(clip) / w;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return false;

        outScreen.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
        outScreen.y = sceneMin.y + (-ndcY * 0.5f + 0.5f) * sceneSize.y;
        return true;
    }

    static void DrawSceneCameraIcons(const CameraData& camera, ImDrawList* drawList, ImVec2 sceneMin, ImVec2 sceneSize)
    {
        using namespace DirectX;

        if (!drawList)
            return;

        const uint32_t selectedId = Scene::GetSelectedInstanceId();
        for (const SceneInstance& instance : Scene::GetInstances())
        {
            if (!instance.camera.enabled)
                continue;

            XMFLOAT4X4 world{};
            if (!Scene::TryGetInstanceWorldMatrix(instance.instanceId, world))
                continue;

            const XMMATRIX worldMatrix = XMLoadFloat4x4(&world);
            const XMVECTOR origin = XMVector3TransformCoord(XMVectorZero(), worldMatrix);
            const XMVECTOR forward = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), worldMatrix));
            const XMVECTOR right = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), worldMatrix));
            const XMVECTOR up = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), worldMatrix));

            XMFLOAT3 originWorld{};
            XMStoreFloat3(&originWorld, origin);

            constexpr float kFrustumDistance = 1.35f;
            const float frustumHalfHeight = tanf(instance.camera.fovY * 0.5f) * kFrustumDistance;
            const float frustumHalfWidth = frustumHalfHeight * 0.75f;

            XMFLOAT3 targetWorld{};
            XMFLOAT3 leftWorld{};
            XMFLOAT3 rightWorld{};
            XMFLOAT3 topWorld{};
            XMFLOAT3 bottomWorld{};
            XMStoreFloat3(&targetWorld, origin + forward * kFrustumDistance);
            XMStoreFloat3(&leftWorld, origin + forward * kFrustumDistance - right * frustumHalfWidth);
            XMStoreFloat3(&rightWorld, origin + forward * kFrustumDistance + right * frustumHalfWidth);
            XMStoreFloat3(&topWorld, origin + forward * kFrustumDistance + up * frustumHalfHeight);
            XMStoreFloat3(&bottomWorld, origin + forward * kFrustumDistance - up * frustumHalfHeight);

            ImVec2 originScreen{}, targetScreen{}, leftScreen{}, rightScreen{}, topScreen{}, bottomScreen{};
            if (!ProjectWorldToSceneScreen(originWorld, camera, sceneMin, sceneSize, originScreen))
                continue;

            const bool hasTarget = ProjectWorldToSceneScreen(targetWorld, camera, sceneMin, sceneSize, targetScreen);
            const bool hasLeft = ProjectWorldToSceneScreen(leftWorld, camera, sceneMin, sceneSize, leftScreen);
            const bool hasRight = ProjectWorldToSceneScreen(rightWorld, camera, sceneMin, sceneSize, rightScreen);
            const bool hasTop = ProjectWorldToSceneScreen(topWorld, camera, sceneMin, sceneSize, topScreen);
            const bool hasBottom = ProjectWorldToSceneScreen(bottomWorld, camera, sceneMin, sceneSize, bottomScreen);

            const bool isSelected = (instance.instanceId == selectedId);
            const ImU32 color = instance.camera.isMain
                ? (isSelected ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 208, 96, 220))
                : (isSelected ? IM_COL32(120, 220, 255, 255) : IM_COL32(96, 168, 255, 210));
            const float radius = isSelected ? 6.0f : 5.0f;

            if (hasTarget)
                drawList->AddLine(originScreen, targetScreen, color, isSelected ? 2.5f : 1.75f);
            if (hasLeft)
                drawList->AddLine(originScreen, leftScreen, color, 1.0f);
            if (hasRight)
                drawList->AddLine(originScreen, rightScreen, color, 1.0f);
            if (hasTop)
                drawList->AddLine(originScreen, topScreen, color, 1.0f);
            if (hasBottom)
                drawList->AddLine(originScreen, bottomScreen, color, 1.0f);
            if (hasLeft && hasTop)
                drawList->AddLine(leftScreen, topScreen, color, 1.0f);
            if (hasTop && hasRight)
                drawList->AddLine(topScreen, rightScreen, color, 1.0f);
            if (hasRight && hasBottom)
                drawList->AddLine(rightScreen, bottomScreen, color, 1.0f);
            if (hasBottom && hasLeft)
                drawList->AddLine(bottomScreen, leftScreen, color, 1.0f);

            drawList->AddCircleFilled(originScreen, radius, color, 18);
            drawList->AddCircle(originScreen, radius + 2.0f, IM_COL32(18, 20, 24, 235), 18, 1.5f);
            if (instance.camera.isMain)
                drawList->AddText(ImVec2(originScreen.x + 8.0f, originScreen.y - 8.0f), color, "Main");
        }
    }

    static void DrawGameInteractionTargetHighlight(ImDrawList* drawList, ImVec2 imageMin, ImVec2 imageSize)
    {
        if (!drawList || !g_engineInstance)
            return;

        const ::Game& game = g_engineInstance->GetGame();
        if (!game.HasInteractionTarget())
            return;

        const uint32_t targetId = game.GetInteractionTargetId();
        const SceneInstance* targetInstance = nullptr;
        for (const SceneInstance& instance : Scene::GetInstances())
        {
            if (instance.instanceId == targetId)
            {
                targetInstance = &instance;
                break;
            }
        }
        if (!targetInstance)
            return;

        CameraData camera{};
        const float aspect = (imageSize.y > 0.0f) ? (imageSize.x / imageSize.y) : 1.0f;
        if (!Scene::TryBuildMainCameraData(aspect, camera))
            return;

        ImVec2 targetScreen{};
        const DirectX::XMFLOAT3 targetWorld = {
            targetInstance->position.x,
            targetInstance->position.y + 0.75f,
            targetInstance->position.z
        };
        if (!ProjectWorldToSceneScreen(targetWorld, camera, imageMin, imageSize, targetScreen))
            return;

        constexpr float kRadius = 16.0f;
        const ImU32 ringColor = IM_COL32(255, 214, 102, 235);
        const ImU32 fillColor = IM_COL32(255, 214, 102, 38);
        drawList->AddCircleFilled(targetScreen, kRadius - 5.0f, fillColor, 24);
        drawList->AddCircle(targetScreen, kRadius, ringColor, 24, 2.5f);
        drawList->AddCircle(targetScreen, kRadius + 6.0f, IM_COL32(32, 24, 10, 220), 24, 1.5f);
        drawList->AddText(ImVec2(targetScreen.x + 18.0f, targetScreen.y - 8.0f), ringColor, game.GetInteractionActionLabel());
    }

    static void SetInstanceAsMainCamera(SceneInstance* selected)
    {
        if (!selected || !selected->camera.enabled)
            return;

        selected->camera.isMain = true;
        for (UINT instanceIndex = 0; instanceIndex < Scene::GetInstanceCount(); ++instanceIndex)
        {
            SceneInstance* other = Scene::GetInstance(instanceIndex);
            if (!other || other == selected || !other->camera.enabled)
                continue;
            other->camera.isMain = false;
        }
    }

    static const char* GetVaultTypeLabel(VaultType vaultType)
    {
        switch (vaultType)
        {
        case VaultType::Node: return "Node";
        case VaultType::Ring: return "Ring";
        case VaultType::Core: return "Core";
        case VaultType::None:
        default: return "None";
        }
    }

    static void DrawAssetSectionHeader(const char* label)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", label);
        ImGui::Separator();
    }

    static std::vector<std::filesystem::path> EnumerateTextureAssets()
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path assetRoot = std::filesystem::path("Assets");
        if (!std::filesystem::exists(assetRoot))
            return result;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file())
                continue;

            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga")
                continue;

            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    static bool IsTextureAssetPath(const std::string& path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
    }

    static std::string FindFirstSelectableAssetIdInFolder(const std::filesystem::path& folder)
    {
        for (const auto& assetFolder : EnumerateAssetFolders())
        {
            if (IsDirectChildOfFolder(assetFolder, folder))
                return assetFolder.string();
        }

        for (const auto& prefabPath : EnumeratePrefabAssets())
        {
            if (IsDirectChildOfFolder(prefabPath, folder))
                return prefabPath.string();
        }

        for (const auto& scenePath : EnumerateSceneAssets())
        {
            if (IsDirectChildOfFolder(scenePath, folder))
                return scenePath.string();
        }

        for (const auto& texturePath : EnumerateTextureAssets())
        {
            if (IsDirectChildOfFolder(texturePath, folder))
                return texturePath.string();
        }

        if (folder == GetAssetRootPath())
        {
            MaterialManager& materials = MaterialManager::GetInstance();
            if (materials.GetMaterialCount() > 0)
            {
                if (const std::string* materialName = materials.GetMaterialNameByIndex(0))
                    return std::format("Material:{}", *materialName);
            }
        }

        return {};
    }

    static std::vector<std::filesystem::path> EnumerateSceneAssets()
    {
        std::vector<std::filesystem::path> result;
        const std::filesystem::path assetRoot = std::filesystem::path("Assets");
        if (!std::filesystem::exists(assetRoot))
            return result;

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_regular_file())
                continue;
            const std::string filename = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (filename.find(".prefab.json") != std::string::npos)
                continue;
            if (ext != ".scene" && ext != ".json")
                continue;
            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    static std::vector<std::filesystem::path> EnumerateAssetFolders()
    {
        std::vector<std::filesystem::path> result;
        EnsureAssetCurrentFolderValid();
        const std::filesystem::path assetRoot = g_AssetCurrentFolder;
        if (!std::filesystem::exists(assetRoot))
            return result;

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(assetRoot, ec))
        {
            if (ec)
                break;
            if (!entry.is_directory())
                continue;
            result.push_back(entry.path());
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    static bool CreateAssetFolder(std::filesystem::path& outCreatedPath)
    {
        EnsureAssetCurrentFolderValid();
        const std::string folderName = "New Folder";
        std::filesystem::path folderPath = g_AssetCurrentFolder / folderName;
        for (uint32_t attempt = 1; std::filesystem::exists(folderPath); ++attempt)
        {
            folderPath = g_AssetCurrentFolder / (folderName + " " + std::to_string(attempt));
        }

        std::error_code ec;
        if (!std::filesystem::create_directory(folderPath, ec))
        {
            Logger::Log(LogLevel::Error, std::format("Failed to create directory: {}", ec.message()), "[Editor]");
            return false;
        }
        outCreatedPath = folderPath;
        return true;
    }

    static bool RenameAssetFolder(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath)
    {
        if (requestedName.empty() || requestedName == "." || requestedName == "..")
            return false;

        std::filesystem::path targetPath = sourcePath.parent_path() / requestedName;
        for (uint32_t attempt = 1; std::filesystem::exists(targetPath); ++attempt)
        {
            targetPath = sourcePath.parent_path() / (requestedName + " " + std::to_string(attempt));
        }

        std::error_code ec;
        std::filesystem::rename(sourcePath, targetPath, ec);
        if (ec)
        {
            Logger::Log(LogLevel::Error, std::format("Failed to rename folder: {}", ec.message()), "[Editor]");
            return false;
        }

        if (IsPathWithinFolder(g_AssetCurrentFolder, sourcePath))
        {
            const std::filesystem::path relative = std::filesystem::relative(g_AssetCurrentFolder, sourcePath);
            g_AssetCurrentFolder = relative.empty() ? targetPath : (targetPath / relative);
        }

        outRenamedPath = targetPath;
        return true;
    }

    static bool DuplicateAssetFile(const std::filesystem::path& sourcePath, std::filesystem::path& outDuplicatedPath)
    {
        if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
            return false;

        const std::string filename = sourcePath.filename().string();
        std::string baseName;
        std::string extensionSuffix;
        if (filename.size() > 12 && filename.ends_with(".prefab.json"))
        {
            baseName = filename.substr(0, filename.size() - 12);
            extensionSuffix = ".prefab.json";
        }
        else
        {
            baseName = sourcePath.stem().string();
            extensionSuffix = sourcePath.extension().string();
        }

        if (baseName.empty())
            baseName = "Asset";

        std::filesystem::path candidatePath = sourcePath.parent_path() / (baseName + "_Copy" + extensionSuffix);
        for (uint32_t attempt = 1; std::filesystem::exists(candidatePath); ++attempt)
            candidatePath = sourcePath.parent_path() / std::format("{}_Copy_{}{}", baseName, attempt, extensionSuffix);

        std::error_code ec;
        std::filesystem::copy_file(sourcePath, candidatePath, std::filesystem::copy_options::none, ec);
        if (ec)
        {
            Logger::Log(LogLevel::Error, std::format("Failed to duplicate asset: {}", ec.message()), "[Editor]");
            return false;
        }

        outDuplicatedPath = candidatePath;
        Logger::Log(LogLevel::Info, std::format("Duplicated asset: {} -> {}", sourcePath.string(), candidatePath.string()), "[Editor]");
        return true;
    }

    static bool RenameAssetFile(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath)
    {
        if (requestedName.empty() || requestedName == "." || requestedName == "..")
            return false;
        if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
            return false;

        std::string sanitized = requestedName;
        sanitized.erase(sanitized.begin(), std::find_if(sanitized.begin(), sanitized.end(), [](unsigned char c) { return !std::isspace(c); }));
        sanitized.erase(std::find_if(sanitized.rbegin(), sanitized.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), sanitized.end());
        for (char& c : sanitized)
        {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        if (sanitized.empty())
            return false;

        const std::filesystem::path targetPath = sourcePath.parent_path() / (sanitized + sourcePath.extension().string());
        if (targetPath == sourcePath)
        {
            outRenamedPath = sourcePath;
            return true;
        }
        if (std::filesystem::exists(targetPath))
            return false;

        std::error_code ec;
        std::filesystem::rename(sourcePath, targetPath, ec);
        if (ec)
        {
            Logger::Log(LogLevel::Error, std::format("Failed to rename asset: {}", ec.message()), "[Editor]");
            return false;
        }

        outRenamedPath = targetPath;
        return true;
    }

    static bool RenameSceneAsset(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outRenamedPath)
    {
        if (requestedName.empty() || requestedName == "." || requestedName == "..")
            return false;
        if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
            return false;

        std::string sanitized = requestedName;
        sanitized.erase(sanitized.begin(), std::find_if(sanitized.begin(), sanitized.end(), [](unsigned char c) { return !std::isspace(c); }));
        sanitized.erase(std::find_if(sanitized.rbegin(), sanitized.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), sanitized.end());
        for (char& c : sanitized)
        {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        }
        if (sanitized.empty())
            return false;

        const std::filesystem::path targetPath = sourcePath.parent_path() / (sanitized + sourcePath.extension().string());
        if (targetPath == sourcePath)
        {
            outRenamedPath = sourcePath;
            return true;
        }
        if (std::filesystem::exists(targetPath))
            return false;

        std::error_code ec;
        std::filesystem::rename(sourcePath, targetPath, ec);
        if (ec)
        {
            Logger::Log(LogLevel::Error, std::format("Failed to rename scene asset: {}", ec.message()), "[Editor]");
            return false;
        }
        outRenamedPath = targetPath;
        return true;
    }

    static bool MoveAssetFileToFolder(const std::filesystem::path& sourcePath, const std::filesystem::path& targetFolder, std::filesystem::path& outMovedPath)
    {
        if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_regular_file(sourcePath))
            return false;

        std::error_code ec;
        const auto targetPath = targetFolder / sourcePath.filename();
        if (std::filesystem::exists(targetPath))
        {
            Logger::Log(LogLevel::Warning, std::format("Target file already exists: {}", targetPath.string()), "[Editor]");
            return false;
        }
        std::filesystem::rename(sourcePath, targetPath, ec);
        if (ec)
        {
            Logger::Log(LogLevel::Error, std::format("Failed to move file: {}", ec.message()), "[Editor]");
            return false;
        }
        if (UI::GetCurrentSceneAssetPath() == sourcePath.string())
            UI::SetCurrentSceneAssetPath(targetPath.string());
        outMovedPath = targetPath;
        return true;
    }

    static bool DeleteAssetPath(const std::filesystem::path& targetPath, bool isFolder)
    {
        std::string reason;
        if (!CanDeleteAssetPath(targetPath, isFolder, reason))
        {
            if (!reason.empty())
                Logger::Log(LogLevel::Warning, reason, "[Editor]");
            return false;
        }

        std::error_code ec;
        bool removed = false;
        if (isFolder)
        {
            const uintmax_t removedCount = std::filesystem::remove_all(targetPath, ec);
            removed = (!ec && removedCount > 0);
        }
        else
        {
            removed = std::filesystem::remove(targetPath, ec);
        }

        if (ec || !removed)
        {
            const std::string errorText = ec ? ec.message() : std::string("Path was not removed.");
            Logger::Log(LogLevel::Error, std::format("Failed to delete {}: {}", isFolder ? "folder" : "asset", targetPath.string(), errorText), "[Editor]");
            return false;
        }

        if (isFolder && IsPathWithinFolder(g_AssetCurrentFolder, targetPath))
            g_AssetCurrentFolder = targetPath.parent_path().empty() ? GetAssetRootPath() : targetPath.parent_path();

        Logger::Log(LogLevel::Info, std::format("Deleted {}: {}", isFolder ? "folder" : "asset", targetPath.string()), "[Editor]");
        return true;
    }

    static bool CanDeleteAssetPath(const std::filesystem::path& targetPath, bool isFolder, std::string& outReason)
    {
        outReason.clear();
        if (targetPath.empty() || !std::filesystem::exists(targetPath))
        {
            outReason = "Delete target does not exist.";
            return false;
        }

        const std::filesystem::path currentScenePath = std::filesystem::path(UI::GetCurrentSceneAssetPath()).lexically_normal();
        const std::filesystem::path normalizedTarget = targetPath.lexically_normal();
        if (!currentScenePath.empty())
        {
            if (!isFolder && normalizedTarget == currentScenePath)
            {
                outReason = "Cannot delete the currently loaded scene. Load a different scene or create a new one first.";
                return false;
            }

            if (isFolder)
            {
                const std::string folderPrefix = normalizedTarget.generic_string();
                const std::string currentSceneString = currentScenePath.generic_string();
                if (!folderPrefix.empty() && currentSceneString.size() >= folderPrefix.size() &&
                    currentSceneString.compare(0, folderPrefix.size(), folderPrefix) == 0)
                {
                    outReason = "Cannot delete a folder containing the currently loaded scene.";
                    return false;
                }
            }
        }

        return true;
    }

    static std::filesystem::path GetAssetRootPath()
    {
        return std::filesystem::path("Assets");
    }

    static void EnsureAssetCurrentFolderValid()
    {
        const std::filesystem::path assetRoot = GetAssetRootPath().lexically_normal();
        std::error_code ec;
        if (g_AssetCurrentFolder.empty())
            g_AssetCurrentFolder = assetRoot;

        const std::filesystem::path current = g_AssetCurrentFolder.lexically_normal();
        const std::string currentString = current.generic_string();
        const std::string rootString = assetRoot.generic_string();
        const bool insideRoot = currentString == rootString ||
            (currentString.size() > rootString.size() && currentString.compare(0, rootString.size(), rootString) == 0 && currentString[rootString.size()] == '/');

        if (!insideRoot || !std::filesystem::exists(current, ec) || ec || !std::filesystem::is_directory(current, ec))
            g_AssetCurrentFolder = assetRoot;
    }

    static bool IsDirectChildOfFolder(const std::filesystem::path& path, const std::filesystem::path& folder)
    {
        return path.parent_path().lexically_normal() == folder.lexically_normal();
    }

    static bool IsPathWithinFolder(const std::filesystem::path& path, const std::filesystem::path& folder)
    {
        const std::string pathString = path.lexically_normal().generic_string();
        const std::string folderString = folder.lexically_normal().generic_string();
        return pathString == folderString ||
            (pathString.size() > folderString.size() && pathString.compare(0, folderString.size(), folderString) == 0 && pathString[folderString.size()] == '/');
    }
}
