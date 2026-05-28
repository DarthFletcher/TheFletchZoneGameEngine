#include "UI.h"
#include "Logger.h"
#include "Utils.h"
#include <Windows.h>
#include <commdlg.h>
#include <string>
#include <Engine.h>
#include <GameplayAudio.h>
#include <Graphics.h>
#include "SplashScreen.h"
#include "EditorPanels.h"
#include "EditorState.h"
#include "EditorIcons.h"
#include "ImGuiUtils.h"
#include "imgui_internal.h"
#include <filesystem>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

ImFont* g_UIFont = nullptr;
ImFont* g_UIFontBold = nullptr;
ImFont* g_MonoFont = nullptr;

extern Engine* g_engineInstance;

namespace UI {

    static constexpr size_t kMaxUndoSnapshots = 64;

    static void ClearRuntimeWorldIfEditing(const char* reason)
    {
        if (!g_engineInstance || Engine::GetState() != Engine::State::Editing)
            return;

        RuntimeWorld& runtimeWorld = g_engineInstance->GetRuntimeWorld();
        const size_t entityCount = runtimeWorld.GetEntityCount();
        if (entityCount == 0)
            return;

        runtimeWorld.Clear();
        Logger::Log(LogLevel::Info,
            std::format("RuntimeWorld cleared while editing: {} ({} entities)", reason, entityCount),
            "[RuntimeWorld]");
    }

    static void ApplyEditorFontSize(float fontSize)
    {
        ImGui::GetIO().FontGlobalScale = fontSize / 16.0f;
        Logger::Log(LogLevel::Info, std::format("Editor font scale set to {:.2f} ({}px preset)", ImGui::GetIO().FontGlobalScale, fontSize), "[UI]");
    }

    static SceneHistoryEntry MakeHistoryEntry()
    {
        SceneHistoryEntry entry{};
        entry.sceneSnapshot = Scene::SerializeToString();
        entry.activeSelectedInstanceId = Scene::GetSelectedInstanceId();
        entry.selectedInstanceIds = Scene::GetSelectedInstanceIds();
        return entry;
    }

    static void PushUndoSnapshot()
    {
        if (!g_engineInstance)
            return;

        EditorState& editor = g_engineInstance->GetEditorState();
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

    // 🔧 Current theme tracking
    static Theme currentTheme = Theme::Dark;

    static bool g_showImGuiDemo = false;
    static bool g_showImGuiMetrics = false;
    static bool g_showFrameDiag = false;
    static std::string g_CurrentScenePath;
    static std::string g_CurrentProjectPath;
    static std::string g_CurrentProjectName;
    static std::filesystem::path g_CurrentProjectRoot = std::filesystem::current_path();
    static std::string g_ProjectStartupScenePath;
    static std::string g_ProjectLastOpenedScenePath;
    static std::vector<std::string> g_RecentProjectPaths;
    static bool g_RecentProjectsLoaded = false;
    static bool g_ShowProjectBrowser = true;
    static bool g_RequestOpenProjectBrowserPopup = true;

    struct ProjectTemplateDefinition
    {
        std::string id;
        std::string name;
        std::string description;
        std::string thumbnailPath;
        std::filesystem::path definitionFilePath;
        std::filesystem::path templateRoot;
        std::string sourceScenePath;
        std::string copyRoot;
        bool hasCameraOverride = false;
        DirectX::XMFLOAT3 cameraPosition{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 cameraRotationDegrees{ 0.0f, 0.0f, 0.0f };

        struct CopyEntry
        {
            std::string sourcePath;
            std::string destinationPath;
        };

        struct ObjectDefinition
        {
            ScenePrimitive primitive = ScenePrimitive::Cube;
            std::string name;
            DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        };

        std::vector<CopyEntry> copyEntries;
        std::vector<ObjectDefinition> objects;
    };

    struct ProjectTemplateValidationResult
    {
        std::vector<std::string> warnings;
        std::vector<std::string> errors;

        bool HasErrors() const { return !errors.empty(); }
        bool HasWarnings() const { return !warnings.empty(); }
    };

    static std::string g_SelectedProjectTemplateId = "basic3d";
    static std::vector<ProjectTemplateDefinition> g_ProjectTemplateDefinitions;
    static bool g_ProjectTemplateDefinitionsLoaded = false;
    static std::unordered_map<std::string, ImTextureID> g_ProjectTemplateThumbnailCache;

    static std::filesystem::path ResolvePathFromRoot(const std::filesystem::path& root, const std::string& relativeOrAbsolute);

    // Command strip visibility (treated like a panel toggle)
    static bool g_showCommandStrip = true;

    static float g_commandStripHeight = 32.0f;

    // Frame diagnostics for editor shell ordering/visibility
    static int g_editorShellFrameCounter = 0;
    static bool g_dockspaceTouchedThisFrame = false;
    static bool g_commandStripTouchedThisFrame = false;

    static std::filesystem::path GetDefaultSceneDirectory()
    {
        return std::filesystem::path("Assets") / "Scenes";
    }

    static std::filesystem::path GetApplicationBaseDirectory()
    {
        char modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return std::filesystem::current_path();
        return std::filesystem::path(modulePath).parent_path();
    }

    static std::string TrimString(std::string value)
    {
        const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace(static_cast<unsigned char>(c)); }).base(), value.end());
        return value;
    }

    static std::string ToLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    static std::filesystem::path GetApplicationSettingsDirectory()
    {
        return GetApplicationBaseDirectory() / "Settings";
    }

    static std::filesystem::path GetProjectTemplatesDirectory()
    {
        return GetApplicationBaseDirectory() / "Templates" / "ProjectTemplates";
    }

    static std::filesystem::path GetProjectTemplateThumbnailsDirectory()
    {
        return GetProjectTemplatesDirectory() / "Thumbnails";
    }

    static std::string SanitizeTemplateId(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (unsigned char c : value)
        {
            if (std::isalnum(c))
                result.push_back(static_cast<char>(std::tolower(c)));
            else if (c == '_' || c == '-' || std::isspace(c))
                result.push_back('_');
        }
        result.erase(std::unique(result.begin(), result.end(), [](char a, char b) { return a == '_' && b == '_'; }), result.end());
        while (!result.empty() && result.front() == '_')
            result.erase(result.begin());
        while (!result.empty() && result.back() == '_')
            result.pop_back();
        if (result.empty())
            result = "template";
        return result;
    }

    static std::vector<ProjectTemplateDefinition> GetDefaultProjectTemplateDefinitions()
    {
        std::vector<ProjectTemplateDefinition> definitions;

        ProjectTemplateDefinition emptyTemplate{};
        emptyTemplate.id = "empty";
        emptyTemplate.name = "Empty";
        emptyTemplate.description = "Creates a clean project with only the default scene camera.";
        emptyTemplate.thumbnailPath = (GetProjectTemplateThumbnailsDirectory() / "empty.png").string();
        definitions.push_back(std::move(emptyTemplate));

        ProjectTemplateDefinition basic3D{};
        basic3D.id = "basic3d";
        basic3D.name = "Basic 3D";
        basic3D.description = "Creates a simple ground-and-primitives scene for general 3D prototyping.";
        basic3D.thumbnailPath = (GetProjectTemplateThumbnailsDirectory() / "basic3d.png").string();
        basic3D.copyRoot.clear();
        basic3D.objects = {
            { ScenePrimitive::Plane, "Ground", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Cube, "Starter Cube", { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Sphere, "Starter Sphere", { 2.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Cylinder, "Starter Cylinder", { -2.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        };
        definitions.push_back(std::move(basic3D));

        ProjectTemplateDefinition basic2D{};
        basic2D.id = "basic2d";
        basic2D.name = "2D";
        basic2D.description = "Creates a flat front-view starter scene with objects arranged on the XY plane.";
        basic2D.thumbnailPath = (GetProjectTemplateThumbnailsDirectory() / "basic2d.png").string();
        basic2D.copyRoot.clear();
        basic2D.hasCameraOverride = true;
        basic2D.cameraPosition = { 0.0f, 0.0f, -10.0f };
        basic2D.cameraRotationDegrees = { 0.0f, 0.0f, 0.0f };
        basic2D.objects = {
            { ScenePrimitive::Cube, "Sprite A", { -2.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.2f } },
            { ScenePrimitive::Cube, "Sprite B", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.2f } },
            { ScenePrimitive::Cube, "Sprite C", { 2.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.2f } },
        };
        definitions.push_back(std::move(basic2D));

        ProjectTemplateDefinition vault{};
        vault.id = "vaultprototype";
        vault.name = "Vault Prototype";
        vault.description = "Creates a starter Siniavault scene with a player avatar, activatable nodes, a stabilizable core, and an exit gate.";
        vault.thumbnailPath = (GetProjectTemplateThumbnailsDirectory() / "vaultprototype.png").string();
        vault.copyRoot.clear();
        vault.hasCameraOverride = true;
        vault.cameraPosition = { 0.0f, 3.0f, -8.0f };
        vault.cameraRotationDegrees = { 12.0f, 0.0f, 0.0f };
        vault.objects = {
            { ScenePrimitive::Capsule, "VaultRunner", { 0.0f, 0.9f, -4.0f }, { 0.75f, 1.8f, 0.75f } },
            { ScenePrimitive::Plane, "Vault Floor", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Sphere, "VaultCore", { 0.0f, 0.75f, 0.0f }, { 1.25f, 1.25f, 1.25f } },
            { ScenePrimitive::Torus, "VaultRing_1", { 0.0f, 0.55f, 0.0f }, { 1.7f, 1.0f, 1.7f } },
            { ScenePrimitive::Torus, "VaultRing_2", { 0.0f, 0.65f, 0.0f }, { 2.4f, 1.0f, 2.4f } },
            { ScenePrimitive::Capsule, "VaultNode_1", { 0.0f, 0.75f, 3.0f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Capsule, "VaultNode_2", { 2.6f, 0.75f, -1.5f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Capsule, "VaultNode_3", { -2.6f, 0.75f, -1.5f }, { 1.0f, 1.0f, 1.0f } },
            { ScenePrimitive::Cube, "VaultExitGate", { 0.0f, 1.2f, -7.0f }, { 1.8f, 2.6f, 0.35f } },
        };
        definitions.push_back(std::move(vault));

        return definitions;
    }

    static bool TryParseProjectTemplateId(const std::string& value, std::string& outTemplateId)
    {
        outTemplateId = SanitizeTemplateId(value);
        return !outTemplateId.empty();
    }

    static const char* GetScenePrimitiveToken(ScenePrimitive primitive)
    {
        switch (primitive)
        {
        case ScenePrimitive::Cube: return "Cube";
        case ScenePrimitive::Sphere: return "Sphere";
        case ScenePrimitive::Plane: return "Plane";
        case ScenePrimitive::Cylinder: return "Cylinder";
        case ScenePrimitive::Capsule: return "Capsule";
        case ScenePrimitive::Torus: return "Torus";
        case ScenePrimitive::Cone: return "Cone";
        case ScenePrimitive::Empty: return "Empty";
        default: return "Cube";
        }
    }

    static bool TryParseScenePrimitiveToken(const std::string& value, ScenePrimitive& outPrimitive)
    {
        const std::string normalized = ToLowerAscii(TrimString(value));
        if (normalized == "cube")
        {
            outPrimitive = ScenePrimitive::Cube;
            return true;
        }
        if (normalized == "sphere")
        {
            outPrimitive = ScenePrimitive::Sphere;
            return true;
        }
        if (normalized == "plane")
        {
            outPrimitive = ScenePrimitive::Plane;
            return true;
        }
        if (normalized == "cylinder")
        {
            outPrimitive = ScenePrimitive::Cylinder;
            return true;
        }
        if (normalized == "capsule")
        {
            outPrimitive = ScenePrimitive::Capsule;
            return true;
        }
        if (normalized == "torus")
        {
            outPrimitive = ScenePrimitive::Torus;
            return true;
        }
        if (normalized == "cone")
        {
            outPrimitive = ScenePrimitive::Cone;
            return true;
        }
        if (normalized == "empty")
        {
            outPrimitive = ScenePrimitive::Empty;
            return true;
        }
        return false;
    }

    static bool TryParseFloat3(const std::string& value, DirectX::XMFLOAT3& outValue)
    {
        std::stringstream stream(value);
        std::string part;
        float components[3]{};
        int index = 0;
        while (std::getline(stream, part, ',') && index < 3)
        {
            try
            {
                components[index++] = std::stof(TrimString(part));
            }
            catch (...)
            {
                return false;
            }
        }

        if (index != 3)
            return false;

        outValue = { components[0], components[1], components[2] };
        return true;
    }

    static std::vector<std::string> SplitString(const std::string& value, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream stream(value);
        std::string part;
        while (std::getline(stream, part, delimiter))
            result.push_back(TrimString(part));
        return result;
    }

    static void EnsureDefaultProjectTemplateFiles()
    {
        const std::filesystem::path templateDir = GetProjectTemplatesDirectory();
        const std::filesystem::path thumbnailDir = GetProjectTemplateThumbnailsDirectory();
        std::error_code ec;
        std::filesystem::create_directories(templateDir, ec);
        std::filesystem::create_directories(thumbnailDir, ec);

        for (const ProjectTemplateDefinition& definition : GetDefaultProjectTemplateDefinitions())
        {
            const std::filesystem::path filePath = templateDir / std::format("{}.tfztemplate", definition.id);
            std::ofstream file(filePath, std::ios::trunc);
            if (!file.is_open())
                continue;

            file << "# TheFletchZone Engine project template definition\n";
            file << "#\n";
            file << "# Required fields:\n";
            file << "#   id=<unique template id; custom ids are allowed>\n";
            file << "#   name=<display name shown in the launcher>\n";
            file << "#   description=<short description shown in the launcher>\n";
            file << "#\n";
            file << "# Optional fields:\n";
            file << "#   thumbnail=<relative or absolute path to a preview image>\n";
            file << "#   sourceScene=<scene file copied into the new project and opened as the startup scene>\n";
            file << "#   copyRoot=<relative destination root used for implicit sourceScene copying>\n";
            file << "#   copy=<source relative path>|<destination relative path inside project>\n";
            file << "#\n";
            file << "# Example custom template folder layout:\n";
            file << "#   my_template.tfztemplate\n";
            file << "#   MyTemplateContent/Assets/Scenes/Main.scene\n";
            file << "#   MyTemplateContent/Assets/Textures/...\n";
            file << "#\n";
            file << "# If sourceScene is set and no copy= rules are present, the engine will\n";
            file << "# try to copy the surrounding template content into the new project automatically.\n";
            file << "#   cameraPosition=x, y, z\n";
            file << "#   cameraRotationDegrees=pitch, yaw, roll\n";
            file << "#\n";
            file << "# Object rows use this format:\n";
            file << "#   object=<Primitive>|<Name>|x, y, z|sx, sy, sz\n";
            file << "#\n";
            file << "# Supported primitive names:\n";
            file << "#   Cube, Sphere, Plane, Cylinder, Capsule, Torus, Cone, Empty\n\n";

            file << "# You can choose any unique id for custom templates.\n";
            file << "id=" << definition.id << "\n";
            file << "name=" << definition.name << "\n";
            file << "description=" << definition.description << "\n";
            file << "thumbnail=" << definition.thumbnailPath << "\n";
            if (!definition.sourceScenePath.empty())
                file << "sourceScene=" << definition.sourceScenePath << "\n";
            if (!definition.copyRoot.empty())
                file << "copyRoot=" << definition.copyRoot << "\n";
            for (const ProjectTemplateDefinition::CopyEntry& copyEntry : definition.copyEntries)
                file << "copy=" << copyEntry.sourcePath << "|" << copyEntry.destinationPath << "\n";
            if (definition.hasCameraOverride)
            {
                file << "cameraPosition=" << definition.cameraPosition.x << ", " << definition.cameraPosition.y << ", " << definition.cameraPosition.z << "\n";
                file << "cameraRotationDegrees=" << definition.cameraRotationDegrees.x << ", " << definition.cameraRotationDegrees.y << ", " << definition.cameraRotationDegrees.z << "\n";
            }
            for (const ProjectTemplateDefinition::ObjectDefinition& object : definition.objects)
            {
                file << "object="
                    << GetScenePrimitiveToken(object.primitive) << "|"
                    << object.name << "|"
                    << object.position.x << ", " << object.position.y << ", " << object.position.z << "|"
                    << object.scale.x << ", " << object.scale.y << ", " << object.scale.z << "\n";
            }
        }
    }

    static bool TryLoadProjectTemplateDefinition(const std::filesystem::path& filePath, ProjectTemplateDefinition& outDefinition)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
            return false;

        std::string line;
        std::string idValue;
        std::string typeValue;
        std::string nameValue;
        std::string descriptionValue;
        std::string thumbnailValue;
        std::string sourceSceneValue;
        std::string copyRootValue;
        DirectX::XMFLOAT3 cameraPosition{};
        DirectX::XMFLOAT3 cameraRotationDegrees{};
        bool hasCameraPosition = false;
        bool hasCameraRotation = false;
        std::vector<ProjectTemplateDefinition::CopyEntry> copyEntries;
        std::vector<ProjectTemplateDefinition::ObjectDefinition> objects;
        while (std::getline(file, line))
        {
            line = TrimString(line);
            if (line.empty() || line[0] == '#')
                continue;

            const size_t separatorPos = line.find('=');
            if (separatorPos == std::string::npos)
                continue;

            const std::string key = ToLowerAscii(TrimString(line.substr(0, separatorPos)));
            const std::string value = TrimString(line.substr(separatorPos + 1));
            if (key == "id")
                idValue = value;
            else if (key == "type")
                typeValue = value;
            else if (key == "name")
                nameValue = value;
            else if (key == "description")
                descriptionValue = value;
            else if (key == "thumbnail")
                thumbnailValue = value;
            else if (key == "sourcescene")
                sourceSceneValue = value;
            else if (key == "copyroot")
                copyRootValue = value;
            else if (key == "copy")
            {
                const std::vector<std::string> parts = SplitString(value, '|');
                if (parts.size() >= 2 && !parts[0].empty() && !parts[1].empty())
                    copyEntries.push_back({ parts[0], parts[1] });
            }
            else if (key == "cameraposition")
                hasCameraPosition = TryParseFloat3(value, cameraPosition);
            else if (key == "camerarotationdegrees")
                hasCameraRotation = TryParseFloat3(value, cameraRotationDegrees);
            else if (key == "object")
            {
                const std::vector<std::string> parts = SplitString(value, '|');
                if (parts.size() < 4)
                    continue;

                ScenePrimitive primitive = ScenePrimitive::Cube;
                DirectX::XMFLOAT3 position{};
                DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
                if (!TryParseScenePrimitiveToken(parts[0], primitive))
                    continue;
                if (!TryParseFloat3(parts[2], position))
                    continue;
                if (!TryParseFloat3(parts[3], scale))
                    continue;

                objects.push_back({ primitive, parts[1], position, scale });
            }
        }

        std::string templateId = !idValue.empty() ? idValue : typeValue;
        if (templateId.empty())
            templateId = filePath.stem().string();
        if (!TryParseProjectTemplateId(templateId, outDefinition.id))
            return false;

        outDefinition.name = nameValue.empty() ? filePath.stem().string() : nameValue;
        outDefinition.description = descriptionValue;
        outDefinition.thumbnailPath = thumbnailValue.empty() ? std::string{} : ResolvePathFromRoot(filePath.parent_path(), thumbnailValue).string();
        outDefinition.hasCameraOverride = hasCameraPosition || hasCameraRotation;
        outDefinition.cameraPosition = cameraPosition;
        outDefinition.cameraRotationDegrees = cameraRotationDegrees;
        outDefinition.objects = std::move(objects);
        return true;
    }

    static void EnsureProjectTemplateDefinitionsLoaded()
    {
        if (g_ProjectTemplateDefinitionsLoaded)
            return;

        g_ProjectTemplateDefinitionsLoaded = true;
        g_ProjectTemplateDefinitions.clear();
        const std::vector<ProjectTemplateDefinition> defaultDefinitions = GetDefaultProjectTemplateDefinitions();
        EnsureDefaultProjectTemplateFiles();

        std::error_code ec;
        const std::filesystem::path templateDir = GetProjectTemplatesDirectory();
        if (std::filesystem::exists(templateDir, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(templateDir, ec))
            {
                if (ec || !entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".tfztemplate")
                    continue;

                ProjectTemplateDefinition definition{};
                if (TryLoadProjectTemplateDefinition(entry.path(), definition))
                    g_ProjectTemplateDefinitions.push_back(std::move(definition));
            }
        }

        if (g_ProjectTemplateDefinitions.empty())
            g_ProjectTemplateDefinitions = defaultDefinitions;

        for (ProjectTemplateDefinition& definition : g_ProjectTemplateDefinitions)
        {
            auto defaultIt = std::find_if(defaultDefinitions.begin(), defaultDefinitions.end(), [&](const ProjectTemplateDefinition& candidate)
            {
                return candidate.id == definition.id;
            });
            if (defaultIt == defaultDefinitions.end())
                continue;

            if (definition.description.empty())
                definition.description = defaultIt->description;
            if (definition.thumbnailPath.empty())
                definition.thumbnailPath = defaultIt->thumbnailPath;
            if (definition.sourceScenePath.empty())
                definition.sourceScenePath = defaultIt->sourceScenePath;
            if (definition.copyRoot.empty())
                definition.copyRoot = defaultIt->copyRoot;
            if (!definition.hasCameraOverride && defaultIt->hasCameraOverride)
            {
                definition.hasCameraOverride = true;
                definition.cameraPosition = defaultIt->cameraPosition;
                definition.cameraRotationDegrees = defaultIt->cameraRotationDegrees;
            }
            if (definition.copyEntries.empty() && !defaultIt->copyEntries.empty())
                definition.copyEntries = defaultIt->copyEntries;
            if (definition.objects.empty() && !defaultIt->objects.empty())
                definition.objects = defaultIt->objects;
        }

        std::sort(g_ProjectTemplateDefinitions.begin(), g_ProjectTemplateDefinitions.end(), [](const ProjectTemplateDefinition& a, const ProjectTemplateDefinition& b)
        {
            const bool aIsEmpty = (a.id == "empty");
            const bool bIsEmpty = (b.id == "empty");
            if (aIsEmpty != bIsEmpty)
                return aIsEmpty;
            return a.name < b.name;
        });

        if (g_ProjectTemplateDefinitions.empty())
            g_SelectedProjectTemplateId.clear();
        else if (std::none_of(g_ProjectTemplateDefinitions.begin(), g_ProjectTemplateDefinitions.end(), [](const ProjectTemplateDefinition& definition)
        {
            return definition.id == g_SelectedProjectTemplateId;
        }))
        {
            g_SelectedProjectTemplateId = g_ProjectTemplateDefinitions.front().id;
        }
    }

    static const std::vector<ProjectTemplateDefinition>& GetProjectTemplateDefinitions()
    {
        EnsureProjectTemplateDefinitionsLoaded();
        return g_ProjectTemplateDefinitions;
    }

    static const ProjectTemplateDefinition* FindProjectTemplateDefinition(const std::string& projectTemplateId)
    {
        const auto& definitions = GetProjectTemplateDefinitions();
        for (const ProjectTemplateDefinition& definition : definitions)
        {
            if (definition.id == projectTemplateId)
                return &definition;
        }
        return nullptr;
    }

    static ImTextureID GetProjectTemplateThumbnailTexture(const std::string& projectTemplateId)
    {
        const ProjectTemplateDefinition* definition = FindProjectTemplateDefinition(projectTemplateId);
        if (!definition || definition->thumbnailPath.empty())
            return 0;

        const std::string thumbnailPath = definition->thumbnailPath;
        auto cachedIt = g_ProjectTemplateThumbnailCache.find(thumbnailPath);
        if (cachedIt != g_ProjectTemplateThumbnailCache.end())
            return cachedIt->second;

        if (!std::filesystem::exists(thumbnailPath))
        {
            g_ProjectTemplateThumbnailCache[thumbnailPath] = 0;
            return 0;
        }

        const ImTextureID texture = ::ImGuiUtils::LoadTextureFromFile(thumbnailPath.c_str());
        g_ProjectTemplateThumbnailCache[thumbnailPath] = texture;
        return texture;
    }

    static const char* GetProjectTemplateLabel(const std::string& projectTemplateId)
    {
        if (const ProjectTemplateDefinition* definition = FindProjectTemplateDefinition(projectTemplateId))
            return definition->name.c_str();
        return "Template";
    }

    static const char* GetProjectTemplateDescription(const std::string& projectTemplateId)
    {
        if (const ProjectTemplateDefinition* definition = FindProjectTemplateDefinition(projectTemplateId))
            return definition->description.c_str();
        return "";
    }

    static SceneInstance* RenameSelectedInstance(const std::string& name)
    {
        SceneInstance* selected = Scene::GetSelectedInstance();
        if (selected)
            selected->name = name;
        return selected;
    }

    static ImU32 GetProjectTemplateAccent(const std::string& projectTemplateId, float alpha = 1.0f)
    {
        const int a = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(projectTemplateId));
        const int r = 96 + static_cast<int>(hash & 0x3F);
        const int g = 96 + static_cast<int>((hash >> 6) & 0x3F);
        const int b = 96 + static_cast<int>((hash >> 12) & 0x3F);
        return IM_COL32(r, g, b, a);
    }

    static void DrawProjectTemplateThumbnail(ImDrawList* drawList, const ProjectTemplateDefinition& definition, ImVec2 min, ImVec2 max)
    {
        if (!drawList)
            return;

        const ImU32 accent = GetProjectTemplateAccent(definition.id, 0.95f);
        const ImU32 line = GetProjectTemplateAccent(definition.id, 0.55f);
        const ImU32 muted = IM_COL32(210, 214, 224, 120);
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        if (definition.objects.empty())
        {
            drawList->AddRect(ImVec2(min.x + 18.0f, min.y + 16.0f), ImVec2(max.x - 18.0f, max.y - 16.0f), muted, 10.0f, 0, 2.0f);
            drawList->AddLine(ImVec2(min.x + 28.0f, center.y), ImVec2(max.x - 28.0f, center.y), line, 1.0f);
            drawList->AddCircle(center, 13.0f, accent, 24, 2.0f);
            drawList->AddCircleFilled(center, 4.0f, accent, 16);
            return;
        }

        drawList->AddLine(ImVec2(min.x + 18.0f, max.y - 20.0f), ImVec2(max.x - 18.0f, max.y - 20.0f), muted, 1.0f);
        const size_t previewCount = (std::min)(definition.objects.size(), size_t(4));
        for (size_t objectIndex = 0; objectIndex < previewCount; ++objectIndex)
        {
            const auto& object = definition.objects[objectIndex];
            const float t = previewCount > 1 ? static_cast<float>(objectIndex) / static_cast<float>(previewCount - 1) : 0.5f;
            const ImVec2 objectCenter(min.x + 34.0f + t * ((max.x - min.x) - 68.0f), center.y + ((objectIndex % 2 == 0) ? -8.0f : 16.0f));
            switch (object.primitive)
            {
            case ScenePrimitive::Sphere:
                drawList->AddCircleFilled(objectCenter, 14.0f, accent, 20);
                break;
            case ScenePrimitive::Plane:
                drawList->AddRectFilled(ImVec2(objectCenter.x - 20.0f, objectCenter.y + 8.0f), ImVec2(objectCenter.x + 20.0f, objectCenter.y + 14.0f), muted, 2.0f);
                break;
            case ScenePrimitive::Cylinder:
            case ScenePrimitive::Capsule:
                drawList->AddRectFilled(ImVec2(objectCenter.x - 10.0f, objectCenter.y - 16.0f), ImVec2(objectCenter.x + 10.0f, objectCenter.y + 18.0f), accent, 8.0f);
                break;
            case ScenePrimitive::Torus:
                drawList->AddCircle(objectCenter, 16.0f, accent, 24, 3.0f);
                break;
            case ScenePrimitive::Cone:
                drawList->AddTriangleFilled(ImVec2(objectCenter.x, objectCenter.y - 18.0f), ImVec2(objectCenter.x - 16.0f, objectCenter.y + 18.0f), ImVec2(objectCenter.x + 16.0f, objectCenter.y + 18.0f), accent);
                break;
            case ScenePrimitive::Empty:
                drawList->AddCircle(objectCenter, 8.0f, accent, 16, 2.0f);
                drawList->AddLine(ImVec2(objectCenter.x - 12.0f, objectCenter.y), ImVec2(objectCenter.x + 12.0f, objectCenter.y), accent, 1.5f);
                drawList->AddLine(ImVec2(objectCenter.x, objectCenter.y - 12.0f), ImVec2(objectCenter.x, objectCenter.y + 12.0f), accent, 1.5f);
                break;
            case ScenePrimitive::Cube:
            default:
                drawList->AddRectFilled(ImVec2(objectCenter.x - 14.0f, objectCenter.y - 14.0f), ImVec2(objectCenter.x + 14.0f, objectCenter.y + 14.0f), accent, 5.0f);
                break;
            }
        }
    }

    static bool DrawProjectTemplateCard(const ProjectTemplateDefinition& definition, bool selected, ImVec2 size)
    {
        ImGui::PushID(definition.id.c_str());
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##ProjectTemplateCard", size);
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect rect(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y));
        const ImU32 border = selected
            ? GetProjectTemplateAccent(definition.id, 1.0f)
            : hovered ? IM_COL32(170, 176, 192, 170) : IM_COL32(94, 100, 116, 135);
        const ImU32 bg = selected
            ? IM_COL32(26, 30, 38, 235)
            : hovered ? IM_COL32(22, 25, 32, 222) : IM_COL32(18, 20, 26, 208);
        drawList->AddRectFilled(rect.Min, rect.Max, bg, 12.0f);
        drawList->AddRect(rect.Min, rect.Max, border, 12.0f, 0, selected ? 2.4f : 1.4f);

        const ImVec2 thumbMin(rect.Min.x + 14.0f, rect.Min.y + 14.0f);
        const ImVec2 thumbMax(rect.Min.x + 198.0f, rect.Max.y - 14.0f);
        drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(10, 12, 18, 220), 10.0f);
        drawList->AddRect(thumbMin, thumbMax, IM_COL32(255, 255, 255, 18), 10.0f);
        if (const ImTextureID thumbnailTexture = GetProjectTemplateThumbnailTexture(definition.id))
        {
            drawList->AddImageRounded(thumbnailTexture, thumbMin, thumbMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 10.0f);
            drawList->AddRectFilled(thumbMin, thumbMax, IM_COL32(10, 12, 18, 90), 10.0f);
        }
        else
        {
            DrawProjectTemplateThumbnail(drawList, definition, thumbMin, thumbMax);
        }

        const float textStartX = thumbMax.x + 18.0f;
        const float textRightX = rect.Max.x - 16.0f;
        const float textWidth = (std::max)(120.0f, textRightX - textStartX);
        const ImVec2 badgeMin(textRightX - 88.0f, rect.Min.y + 14.0f);
        const ImVec2 badgeMax(textRightX, rect.Min.y + 38.0f);
        if (selected)
        {
            drawList->AddRectFilled(badgeMin, badgeMax, GetProjectTemplateAccent(definition.id, 0.95f), 6.0f);
            drawList->AddText(ImVec2(badgeMin.x + 16.0f, badgeMin.y + 4.0f), IM_COL32(12, 14, 18, 255), "SELECTED");
        }
        else if (hovered)
        {
            drawList->AddRect(badgeMin, badgeMax, IM_COL32(255, 255, 255, 60), 6.0f);
            drawList->AddText(ImVec2(badgeMin.x + 22.0f, badgeMin.y + 4.0f), IM_COL32(220, 224, 232, 180), "SELECT");
        }

        ImGui::SetCursorScreenPos(ImVec2(textStartX, rect.Min.y + 18.0f));
        ImGui::PushTextWrapPos(textStartX + textWidth);
        if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
        ImGui::TextUnformatted(definition.name.c_str());
        if (g_UIFontBold) ImGui::PopFont();
        ImGui::Dummy(ImVec2(1.0f, 2.0f));
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.84f, 0.95f), "%s", definition.description.c_str());
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
        ImGui::TextDisabled("Id: %s", definition.id.c_str());
        ImGui::TextDisabled("Objects: %d", static_cast<int>(definition.objects.size()));
        if (!definition.sourceScenePath.empty())
            ImGui::TextDisabled("Uses source scene");
        else if (!definition.copyEntries.empty())
            ImGui::TextDisabled("Copies starter content");
        else
            ImGui::TextDisabled("Generated starter scene");
        ImGui::PopTextWrapPos();

        ImGui::PopID();
        return pressed;
    }

    static bool CopyTemplateEntryToProject(const std::filesystem::path& templateRoot, const ProjectTemplateDefinition::CopyEntry& copyEntry, const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path sourcePath = ResolvePathFromRoot(templateRoot, copyEntry.sourcePath);
        const std::filesystem::path destinationPath = ResolvePathFromRoot(projectRoot, copyEntry.destinationPath);
        if (!std::filesystem::exists(sourcePath))
            return false;

        std::error_code ec;
        if (std::filesystem::is_directory(sourcePath, ec))
        {
            std::filesystem::create_directories(destinationPath, ec);
            ec.clear();
            std::filesystem::copy(sourcePath, destinationPath,
                std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::overwrite_existing,
                ec);
            return !ec;
        }

        std::filesystem::create_directories(destinationPath.parent_path(), ec);
        ec.clear();
        std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }

    static bool IsProjectRootLikeFolderName(const std::string& name)
    {
        const std::string normalized = ToLowerAscii(name);
        return normalized == "assets" || normalized == "settings";
    }

    static std::filesystem::path GetImplicitTemplateDestinationRoot(const ProjectTemplateDefinition& definition, const std::filesystem::path& projectRoot)
    {
        if (definition.copyRoot.empty())
            return projectRoot;
        return ResolvePathFromRoot(projectRoot, definition.copyRoot);
    }

    static ProjectTemplateValidationResult ValidateProjectTemplateDefinition(const ProjectTemplateDefinition& definition)
    {
        ProjectTemplateValidationResult result{};

        if (definition.id.empty())
            result.errors.push_back("Template id is empty.");
        if (definition.name.empty())
            result.warnings.push_back("Template name is empty.");

        if (!definition.sourceScenePath.empty())
        {
            const std::filesystem::path sourceSceneAbsolute = ResolvePathFromRoot(definition.templateRoot, definition.sourceScenePath);
            if (!std::filesystem::exists(sourceSceneAbsolute))
            {
                result.errors.push_back(std::format("sourceScene not found: {}", sourceSceneAbsolute.string()));
            }
            else if (definition.copyEntries.empty())
            {
                std::filesystem::path inferredRoot = definition.templateRoot;
                const std::filesystem::path sourceSceneRelative(definition.sourceScenePath);
                std::vector<std::filesystem::path> parts;
                for (const auto& part : sourceSceneRelative)
                {
                    if (!part.empty() && part != ".")
                        parts.push_back(part);
                }

                bool foundRootMarker = false;
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    if (!IsProjectRootLikeFolderName(parts[i].string()))
                        continue;

                    for (size_t prefixIndex = 0; prefixIndex < i; ++prefixIndex)
                        inferredRoot /= parts[prefixIndex];
                    foundRootMarker = true;
                    break;
                }

                if (!foundRootMarker)
                    result.warnings.push_back("Implicit sourceScene copy could not infer an Assets/Settings root marker; consider adding copy= rules or copyRoot.");
                else if (!std::filesystem::exists(inferredRoot))
                    result.warnings.push_back(std::format("Implicit template copy root not found: {}", inferredRoot.string()));
            }
        }

        for (const ProjectTemplateDefinition::CopyEntry& copyEntry : definition.copyEntries)
        {
            const std::filesystem::path sourcePath = ResolvePathFromRoot(definition.templateRoot, copyEntry.sourcePath);
            if (!std::filesystem::exists(sourcePath))
                result.errors.push_back(std::format("copy source not found: {}", sourcePath.string()));
        }

        if (definition.sourceScenePath.empty() && definition.copyEntries.empty() && definition.objects.empty())
            result.warnings.push_back("Template has no sourceScene, copy rules, or object definitions; it will create an empty scene.");

        return result;
    }

    static bool CopyTemplateDirectoryContentsToProject(const std::filesystem::path& sourceRoot, const std::filesystem::path& destinationRoot, const ProjectTemplateDefinition& definition)
    {
        std::error_code ec;
        if (!std::filesystem::exists(sourceRoot, ec))
            return false;

        const std::filesystem::path thumbnailPath = definition.thumbnailPath.empty() ? std::filesystem::path() : std::filesystem::path(definition.thumbnailPath).lexically_normal();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot, ec))
        {
            if (ec)
                return false;

            const std::filesystem::path sourcePath = entry.path().lexically_normal();
            if (sourcePath == definition.definitionFilePath.lexically_normal())
                continue;
            if (!thumbnailPath.empty() && sourcePath == thumbnailPath)
                continue;
            if (sourcePath.extension() == ".tfztemplate")
                continue;

            const std::filesystem::path relativePath = std::filesystem::relative(sourcePath, sourceRoot, ec);
            if (ec || relativePath.empty())
                continue;

            const std::filesystem::path destinationPath = (destinationRoot / relativePath).lexically_normal();
            if (entry.is_directory())
            {
                std::filesystem::create_directories(destinationPath, ec);
                if (ec)
                    return false;
                continue;
            }

            std::filesystem::create_directories(destinationPath.parent_path(), ec);
            if (ec)
                return false;
            std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
                return false;
        }

        return true;
    }

    static bool TryImplicitTemplateContentCopy(const ProjectTemplateDefinition& definition, const std::filesystem::path& projectRoot)
    {
        if (definition.sourceScenePath.empty())
            return false;

        const std::filesystem::path sourceSceneRelative(definition.sourceScenePath);
        std::vector<std::filesystem::path> parts;
        for (const auto& part : sourceSceneRelative)
        {
            if (!part.empty() && part != ".")
                parts.push_back(part);
        }

        std::filesystem::path sourceContentRoot = definition.templateRoot;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (!IsProjectRootLikeFolderName(parts[i].string()))
                continue;

            std::filesystem::path prefix;
            for (size_t prefixIndex = 0; prefixIndex < i; ++prefixIndex)
                prefix /= parts[prefixIndex];

            if (!prefix.empty())
                sourceContentRoot /= prefix;
            return CopyTemplateDirectoryContentsToProject(sourceContentRoot, GetImplicitTemplateDestinationRoot(definition, projectRoot), definition);
        }

        return false;
    }

    static bool TryApplyProjectTemplateSceneAssets(const ProjectTemplateDefinition& definition, const std::filesystem::path& projectRoot, std::string& outStartupScenePath)
    {
        if (definition.sourceScenePath.empty())
            return false;

        bool sourceSceneCoveredByCopyRule = false;
        if (!definition.copyEntries.empty())
        {
            for (const ProjectTemplateDefinition::CopyEntry& copyEntry : definition.copyEntries)
            {
                if (!CopyTemplateEntryToProject(definition.templateRoot, copyEntry, projectRoot))
                    continue;

                const std::filesystem::path copiedSource = ResolvePathFromRoot(definition.templateRoot, copyEntry.sourcePath);
                const std::filesystem::path sourceSceneAbsolute = ResolvePathFromRoot(definition.templateRoot, definition.sourceScenePath);
                if (copiedSource == sourceSceneAbsolute || std::filesystem::is_directory(copiedSource))
                    sourceSceneCoveredByCopyRule = true;
            }
        }
        else if (TryImplicitTemplateContentCopy(definition, projectRoot))
        {
            sourceSceneCoveredByCopyRule = true;
        }

        const std::filesystem::path sourceSceneAbsolute = ResolvePathFromRoot(definition.templateRoot, definition.sourceScenePath);
        const std::filesystem::path destinationSceneAbsolute = ResolvePathFromRoot(projectRoot, definition.sourceScenePath);
        if (!sourceSceneCoveredByCopyRule)
        {
            std::error_code ec;
            std::filesystem::create_directories(destinationSceneAbsolute.parent_path(), ec);
            ec.clear();
            std::filesystem::copy_file(sourceSceneAbsolute, destinationSceneAbsolute, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
                return false;
        }

        if (!std::filesystem::exists(destinationSceneAbsolute))
            return false;

        outStartupScenePath = destinationSceneAbsolute.string();
        return LoadSceneAssetFromPath(outStartupScenePath);
    }

    static void BuildProjectTemplateScene(const std::string& projectTemplateId, const std::filesystem::path& projectRoot, std::string& outStartupScenePath)
    {
        using namespace DirectX;

        const ProjectTemplateDefinition* definition = FindProjectTemplateDefinition(projectTemplateId);
        if (!definition)
        {
            Scene::NewScene();
            ClearRuntimeWorldIfEditing("project template fallback scene");
            outStartupScenePath = (projectRoot / GetDefaultSceneDirectory() / "Main.scene").string();
            Scene::ClearSelection();
            return;
        }

        if (TryApplyProjectTemplateSceneAssets(*definition, projectRoot, outStartupScenePath))
            return;

        Scene::NewScene();
        ClearRuntimeWorldIfEditing("project template scene build");

        if (definition->hasCameraOverride)
        {
            if (SceneInstance* mainCamera = Scene::GetMainCameraInstanceMutable())
            {
                mainCamera->position = definition->cameraPosition;
                mainCamera->rotation = {
                    XMConvertToRadians(definition->cameraRotationDegrees.x),
                    XMConvertToRadians(definition->cameraRotationDegrees.y),
                    XMConvertToRadians(definition->cameraRotationDegrees.z)
                };
            }
        }

        for (const ProjectTemplateDefinition::ObjectDefinition& object : definition->objects)
        {
            Scene::CreatePrimitive(object.primitive, object.position);
            if (SceneInstance* selected = RenameSelectedInstance(object.name))
                selected->scale = object.scale;
        }

        Scene::RebuildRenderInstancesFromSceneData();
        Scene::MarkInstancesDirty();
        Scene::ClearSelection();
        outStartupScenePath = (projectRoot / GetDefaultSceneDirectory() / "Main.scene").string();
    }

    static std::string EscapeJsonString(std::string value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (char c : value)
        {
            switch (c)
            {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
            }
        }
        return escaped;
    }

    static bool TryReadJsonStringField(const std::string& content, const char* key, std::string& outValue)
    {
        const std::string needle = std::string("\"") + key + "\"";
        const size_t keyPos = content.find(needle);
        if (keyPos == std::string::npos)
            return false;

        const size_t colonPos = content.find(':', keyPos + needle.size());
        if (colonPos == std::string::npos)
            return false;

        const size_t firstQuote = content.find('"', colonPos + 1);
        if (firstQuote == std::string::npos)
            return false;

        std::string result;
        bool escape = false;
        for (size_t i = firstQuote + 1; i < content.size(); ++i)
        {
            const char c = content[i];
            if (escape)
            {
                switch (c)
                {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default: result += c; break;
                }
                escape = false;
                continue;
            }

            if (c == '\\')
            {
                escape = true;
                continue;
            }

            if (c == '"')
            {
                outValue = result;
                return true;
            }

            result += c;
        }

        return false;
    }

    static std::filesystem::path GetProjectDefaultDirectory()
    {
        if (!g_CurrentProjectRoot.empty())
            return g_CurrentProjectRoot;
        return std::filesystem::current_path();
    }

    static std::filesystem::path GetRecentProjectsFilePath()
    {
        return GetApplicationSettingsDirectory() / "recent_projects.txt";
    }

    static void SaveRecentProjects()
    {
        std::error_code ec;
        std::filesystem::create_directories(GetApplicationSettingsDirectory(), ec);

        std::ofstream file(GetRecentProjectsFilePath(), std::ios::trunc);
        if (!file.is_open())
            return;

        for (const std::string& path : g_RecentProjectPaths)
            file << path << "\n";
    }

    static void LoadRecentProjects()
    {
        if (g_RecentProjectsLoaded)
            return;

        g_RecentProjectsLoaded = true;
        g_RecentProjectPaths.clear();

        std::ifstream file(GetRecentProjectsFilePath());
        if (!file.is_open())
            return;

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;
            if (std::find(g_RecentProjectPaths.begin(), g_RecentProjectPaths.end(), line) == g_RecentProjectPaths.end())
                g_RecentProjectPaths.push_back(line);
        }
    }

    static void RememberRecentProject(const std::string& projectPath)
    {
        if (projectPath.empty())
            return;

        LoadRecentProjects();
        g_RecentProjectPaths.erase(
            std::remove(g_RecentProjectPaths.begin(), g_RecentProjectPaths.end(), projectPath),
            g_RecentProjectPaths.end());
        g_RecentProjectPaths.insert(g_RecentProjectPaths.begin(), projectPath);
        constexpr size_t kMaxRecentProjects = 8;
        if (g_RecentProjectPaths.size() > kMaxRecentProjects)
            g_RecentProjectPaths.resize(kMaxRecentProjects);
        SaveRecentProjects();
    }

    static bool ShowProjectFileDialog(bool saveDialog, std::string& inOutPath)
    {
        char fileBuffer[MAX_PATH] = {};
        std::string initialPath = inOutPath;
        if (initialPath.empty())
            initialPath = (GetProjectDefaultDirectory() / "NewProject.tfzproj").string();
        strncpy_s(fileBuffer, initialPath.c_str(), _TRUNCATE);

        const std::filesystem::path initialDir = std::filesystem::path(initialPath).parent_path();
        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Graphics::GetInstance().GetHWND();
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "TFZ Project Files (*.tfzproj)\0*.tfzproj\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        const std::string initialDirString = initialDir.string();
        ofn.lpstrInitialDir = initialDirString.empty() ? nullptr : initialDirString.c_str();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (saveDialog)
            ofn.Flags |= OFN_OVERWRITEPROMPT;
        else
            ofn.Flags |= OFN_FILEMUSTEXIST;
        ofn.lpstrDefExt = "tfzproj";

        const BOOL result = saveDialog ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
        if (!result)
            return false;

        inOutPath = fileBuffer;
        return true;
    }

    static void SyncProjectStateToEditor()
    {
        if (!g_engineInstance)
            return;

        EditorState& editor = g_engineInstance->GetEditorState();
        editor.currentProjectName = g_CurrentProjectName;
        editor.currentProjectPath = g_CurrentProjectPath;
        editor.currentProjectRoot = g_CurrentProjectRoot.string();
        editor.startupScenePath = g_ProjectStartupScenePath;
        editor.lastOpenedScenePath = g_ProjectLastOpenedScenePath;
    }

    static void SetCurrentProjectState(const std::string& projectPath, const std::string& projectName, const std::filesystem::path& projectRoot, const std::string& startupScenePath, const std::string& lastOpenedScenePath)
    {
        g_CurrentProjectPath = projectPath;
        g_CurrentProjectName = projectName;
        g_CurrentProjectRoot = projectRoot;
        g_ProjectStartupScenePath = startupScenePath;
        g_ProjectLastOpenedScenePath = lastOpenedScenePath;
        std::filesystem::current_path(projectRoot);
        SyncProjectStateToEditor();
        RememberRecentProject(projectPath);
    }

    static std::string MakePathRelativeToRoot(const std::filesystem::path& path, const std::filesystem::path& root)
    {
        if (path.empty())
            return {};

        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
        const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
        std::filesystem::path relative = std::filesystem::relative(absolutePath, absoluteRoot, ec);
        if (ec || relative.empty())
            return absolutePath.lexically_normal().string();
        return relative.lexically_normal().string();
    }

    static std::string MakePathRelativeToProjectRoot(const std::filesystem::path& path)
    {
        return MakePathRelativeToRoot(path, g_CurrentProjectRoot);
    }

    static std::filesystem::path ResolvePathFromRoot(const std::filesystem::path& root, const std::string& relativeOrAbsolute)
    {
        if (relativeOrAbsolute.empty())
            return {};
        const std::filesystem::path path(relativeOrAbsolute);
        if (path.is_absolute())
            return path;
        return (root / path).lexically_normal();
    }

    static std::filesystem::path ResolveProjectPath(const std::string& relativeOrAbsolute)
    {
        return ResolvePathFromRoot(g_CurrentProjectRoot, relativeOrAbsolute);
    }

    static void EnsureProjectDirectories(const std::filesystem::path& projectRoot)
    {
        std::filesystem::create_directories(projectRoot / "Assets");
        std::filesystem::create_directories(projectRoot / "Assets" / "Scenes");
        std::filesystem::create_directories(projectRoot / "Settings");
    }

    static bool SaveProjectFileWithPath(const std::string& projectPath)
    {
        if (projectPath.empty())
            return false;

        std::filesystem::path projectFilePath(projectPath);
        const std::filesystem::path projectRoot = projectFilePath.parent_path();
        if (projectRoot.empty())
            return false;

        EnsureProjectDirectories(projectRoot);

        const std::string projectName = projectFilePath.stem().string();
        std::string startupScenePath = g_ProjectStartupScenePath;
        if (startupScenePath.empty())
        {
            startupScenePath = MakePathRelativeToRoot(projectRoot / GetDefaultSceneDirectory() / "Main.scene", projectRoot);
        }

        std::string lastOpenedScenePath = g_CurrentScenePath;
        if (lastOpenedScenePath.empty())
        {
            const std::filesystem::path startupAbsolute = ResolvePathFromRoot(projectRoot, startupScenePath);
            lastOpenedScenePath = startupAbsolute.string();
        }

        if (!std::filesystem::exists(ResolvePathFromRoot(projectRoot, startupScenePath)))
        {
            const std::filesystem::path startupAbsolute = ResolvePathFromRoot(projectRoot, startupScenePath);
            Scene::SaveToFile(startupAbsolute.string());
            g_CurrentScenePath = startupAbsolute.string();
            lastOpenedScenePath = g_CurrentScenePath;
        }

        const std::string startupSceneRelative = MakePathRelativeToRoot(ResolvePathFromRoot(projectRoot, startupScenePath), projectRoot);
        const std::string lastOpenedSceneRelative = MakePathRelativeToRoot(lastOpenedScenePath, projectRoot);

        std::ofstream file(projectFilePath, std::ios::trunc);
        if (!file.is_open())
            return false;

        file << "{\n";
        file << "  \"projectVersion\": 1,\n";
        file << "  \"name\": \"" << EscapeJsonString(projectName) << "\",\n";
        file << "  \"projectRoot\": \".\",\n";
        file << "  \"startupScene\": \"" << EscapeJsonString(startupSceneRelative) << "\",\n";
        file << "  \"lastOpenedScene\": \"" << EscapeJsonString(lastOpenedSceneRelative) << "\"\n";
        file << "}\n";
        if (!file.good())
            return false;

        SetCurrentProjectState(projectFilePath.string(), projectName, projectRoot, startupSceneRelative, lastOpenedSceneRelative);
        return true;
    }

    static bool LoadProjectFileFromPath(const std::string& projectPath)
    {
        if (projectPath.empty())
            return false;

        std::ifstream file(projectPath);
        if (!file.is_open())
            return false;

        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string projectName;
        std::string startupSceneRelative;
        std::string lastOpenedSceneRelative;
        if (!TryReadJsonStringField(content, "name", projectName))
            projectName = std::filesystem::path(projectPath).stem().string();
        (void)TryReadJsonStringField(content, "startupScene", startupSceneRelative);
        (void)TryReadJsonStringField(content, "lastOpenedScene", lastOpenedSceneRelative);

        const std::filesystem::path projectRoot = std::filesystem::path(projectPath).parent_path();
        EnsureProjectDirectories(projectRoot);
        SetCurrentProjectState(projectPath, projectName, projectRoot, startupSceneRelative, lastOpenedSceneRelative);

        const std::string preferredSceneRelative = !lastOpenedSceneRelative.empty() ? lastOpenedSceneRelative : startupSceneRelative;
        if (!preferredSceneRelative.empty())
        {
            const std::filesystem::path scenePath = ResolveProjectPath(preferredSceneRelative);
            if (std::filesystem::exists(scenePath))
            {
                if (!LoadSceneAssetFromPath(scenePath.string()))
                    return false;
            }
            else
            {
                Scene::NewScene();
                ClearRuntimeWorldIfEditing("project scene missing fallback");
                g_CurrentScenePath.clear();
                if (g_engineInstance)
                {
                    EditorState& editor = g_engineInstance->GetEditorState();
                    editor.selection.Clear();
                    editor.ClearHistory();
                }
            }
        }
        else
        {
            Scene::NewScene();
            ClearRuntimeWorldIfEditing("project opened without preferred scene");
            g_CurrentScenePath.clear();
            if (g_engineInstance)
            {
                EditorState& editor = g_engineInstance->GetEditorState();
                editor.selection.Clear();
                editor.ClearHistory();
            }
        }

        return true;
    }

    static void DrawStartupProjectBrowser()
    {
        LoadRecentProjects();
        static ImVec2 s_LastProjectBrowserWorkSize{ 0.0f, 0.0f };

        if (!g_ShowProjectBrowser)
            return;

        if (!g_CurrentProjectPath.empty())
        {
            g_ShowProjectBrowser = false;
            return;
        }

        if (g_RequestOpenProjectBrowserPopup)
        {
            ImGui::OpenPopup("Welcome to TheFletchZone Engine##ProjectBrowser");
            g_RequestOpenProjectBrowserPopup = false;
            s_LastProjectBrowserWorkSize = {};
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport)
        {
            const ImVec2 workSize = viewport->WorkSize;
            const bool viewportSizeChanged = workSize.x != s_LastProjectBrowserWorkSize.x ||
                workSize.y != s_LastProjectBrowserWorkSize.y;
            const ImVec2 browserSize(
                (std::min)(1040.0f, (std::max)(560.0f, workSize.x - 64.0f)),
                (std::min)(720.0f, (std::max)(420.0f, workSize.y - 64.0f)));
            const ImVec2 browserCenter(
                viewport->WorkPos.x + workSize.x * 0.5f,
                viewport->WorkPos.y + workSize.y * 0.5f);

            ImGui::SetNextWindowPos(browserCenter, viewportSizeChanged ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(browserSize, ImGuiCond_Appearing);
            s_LastProjectBrowserWorkSize = workSize;
        }

        if (!ImGui::BeginPopupModal("Welcome to TheFletchZone Engine##ProjectBrowser", nullptr,
            ImGuiWindowFlags_NoSavedSettings))
            return;

        if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
        ImGui::TextUnformatted("Black Flame Project Browser");
        if (g_UIFontBold) ImGui::PopFont();
        ImGui::TextDisabled("Create, open, or reopen a project to begin.");
        ImGui::Separator();

        const auto& templates = GetProjectTemplateDefinitions();
        const ProjectTemplateDefinition* selectedTemplate = FindProjectTemplateDefinition(g_SelectedProjectTemplateId);
        const ProjectTemplateValidationResult selectedTemplateValidation = selectedTemplate
            ? ValidateProjectTemplateDefinition(*selectedTemplate)
            : ProjectTemplateValidationResult{};
        const bool hasRecentProjects = !g_RecentProjectPaths.empty();

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float panelGap = 14.0f;
        const float leftPanelWidth = (std::clamp)(availableWidth * 0.60f, 520.0f, availableWidth - 280.0f);
        const float rightPanelWidth = (std::max)(220.0f, availableWidth - leftPanelWidth - panelGap);
        const float bodyHeight = (std::max)(280.0f, availableHeight - 56.0f);

        ImGui::BeginChild("##WelcomeTemplatePane", ImVec2(leftPanelWidth, bodyHeight), true);
        ImGui::TextUnformatted("Choose a starter template");
        ImGui::TextDisabled("Select a project starter below.");
        ImGui::Separator();

        const float innerWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 cardSize((std::max)(300.0f, innerWidth - 6.0f), 156.0f);
        for (size_t templateIndex = 0; templateIndex < templates.size(); ++templateIndex)
        {
            if (DrawProjectTemplateCard(templates[templateIndex], g_SelectedProjectTemplateId == templates[templateIndex].id, cardSize))
                g_SelectedProjectTemplateId = templates[templateIndex].id;
            if (templateIndex + 1u < templates.size())
                ImGui::Dummy(ImVec2(1.0f, 6.0f));
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, panelGap);

        ImGui::BeginChild("##WelcomeDetailsPane", ImVec2(rightPanelWidth, bodyHeight), true);
        if (selectedTemplate)
        {
            if (g_UIFontBold) ImGui::PushFont(g_UIFontBold);
            ImGui::TextUnformatted(selectedTemplate->name.c_str());
            if (g_UIFontBold) ImGui::PopFont();
            ImGui::TextWrapped("%s", selectedTemplate->description.c_str());
            ImGui::Dummy(ImVec2(1.0f, 6.0f));

            ImGui::TextDisabled("Template Id: %s", selectedTemplate->id.c_str());
            ImGui::TextDisabled("Starter Objects: %d", static_cast<int>(selectedTemplate->objects.size()));
            if (!selectedTemplate->sourceScenePath.empty())
                ImGui::TextDisabled("Source Scene: %s", selectedTemplate->sourceScenePath.c_str());
            if (!selectedTemplate->copyRoot.empty())
                ImGui::TextDisabled("Implicit Copy Root: %s", selectedTemplate->copyRoot.c_str());
        }
        else
        {
            ImGui::TextDisabled("No template selected.");
        }

        if (selectedTemplate && (selectedTemplateValidation.HasErrors() || selectedTemplateValidation.HasWarnings()))
        {
            ImGui::Dummy(ImVec2(1.0f, 6.0f));
            ImGui::BeginChild("##TemplateValidation", ImVec2(0.0f, 132.0f), true);
            if (selectedTemplateValidation.HasErrors())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "Template issues:");
                for (const std::string& error : selectedTemplateValidation.errors)
                {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos();
                    ImGui::TextUnformatted(error.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            if (selectedTemplateValidation.HasWarnings())
            {
                if (selectedTemplateValidation.HasErrors())
                    ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.36f, 1.0f), "Template warnings:");
                for (const std::string& warning : selectedTemplateValidation.warnings)
                {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::PushTextWrapPos();
                    ImGui::TextUnformatted(warning.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(1.0f, 8.0f));
        ImGui::TextUnformatted("Project Actions");
        ImGui::Separator();

        if (selectedTemplateValidation.HasErrors())
            ImGui::BeginDisabled();
        if (ImGui::Button("New Project...##Welcome", ImVec2(-1.0f, 0.0f)))
        {
            if (NewProject())
            {
                g_ShowProjectBrowser = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (selectedTemplateValidation.HasErrors())
            ImGui::EndDisabled();
        if (ImGui::Button("Open Project...##Welcome", ImVec2(-1.0f, 0.0f)))
        {
            if (OpenProject())
            {
                g_ShowProjectBrowser = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!hasRecentProjects)
            ImGui::BeginDisabled();
        if (ImGui::Button("Open Last Project##Welcome", ImVec2(-1.0f, 0.0f)))
        {
            if (hasRecentProjects && LoadProjectFileFromPath(g_RecentProjectPaths.front()))
            {
                g_ShowProjectBrowser = false;
                ImGui::CloseCurrentPopup();
            }
        }
        if (!hasRecentProjects)
            ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(1.0f, 12.0f));
        ImGui::TextUnformatted("Recent Projects");
        ImGui::BeginChild("##WelcomeRecentProjects", ImVec2(0.0f, 0.0f), true);
        if (g_RecentProjectPaths.empty())
        {
            ImGui::TextDisabled("No recent projects yet.");
        }
        else
        {
            for (size_t recentIndex = 0; recentIndex < g_RecentProjectPaths.size(); ++recentIndex)
            {
                const std::filesystem::path recentPath(g_RecentProjectPaths[recentIndex]);
                const std::string projectName = recentPath.stem().string();
                const std::string buttonLabel = std::format("{}##WelcomeRecentProject{}", projectName, recentIndex);
                if (ImGui::Selectable(buttonLabel.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (LoadProjectFileFromPath(g_RecentProjectPaths[recentIndex]))
                    {
                        g_ShowProjectBrowser = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::TextDisabled("%s", g_RecentProjectPaths[recentIndex].c_str());
                if (recentIndex + 1u < g_RecentProjectPaths.size())
                    ImGui::Separator();
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(1.0f, 10.0f));
        if (ImGui::Button("Continue Without Project##Welcome", ImVec2(220.0f, 0.0f)))
        {
            g_ShowProjectBrowser = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    static std::string GetDefaultScenePathString()
    {
        return (GetDefaultSceneDirectory() / "NewScene.scene").string();
    }

    static bool ShowSceneFileDialog(bool saveDialog, std::string& inOutPath)
    {
        char fileBuffer[MAX_PATH] = {};
        const std::string initialPath = inOutPath.empty() ? GetDefaultScenePathString() : inOutPath;
        strncpy_s(fileBuffer, initialPath.c_str(), _TRUNCATE);

        const std::filesystem::path initialDir = initialPath.empty()
            ? GetDefaultSceneDirectory()
            : std::filesystem::path(initialPath).parent_path();

        OPENFILENAMEA ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Graphics::GetInstance().GetHWND();
        ofn.lpstrFile = fileBuffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "Scene Files (*.scene)\0*.scene\0JSON Scene Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        const std::string initialDirString = initialDir.string();
        ofn.lpstrInitialDir = initialDirString.empty() ? nullptr : initialDirString.c_str();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (saveDialog)
            ofn.Flags |= OFN_OVERWRITEPROMPT;
        else
            ofn.Flags |= OFN_FILEMUSTEXIST;
        ofn.lpstrDefExt = "scene";

        const BOOL result = saveDialog ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
        if (!result)
            return false;

        inOutPath = fileBuffer;
        return true;
    }

    static bool SaveSceneWithPath(const std::string& path)
    {
        if (path.empty())
            return false;
        if (!Scene::SaveToFile(path))
            return false;
        g_CurrentScenePath = path;
        return true;
    }

    static bool SaveSceneAs()
    {
        std::string path = g_CurrentScenePath.empty() ? GetDefaultScenePathString() : g_CurrentScenePath;
        if (!ShowSceneFileDialog(true, path))
            return false;
        return SaveSceneWithPath(path);
    }

    static bool LoadSceneFromDialog()
    {
        std::string path = g_CurrentScenePath.empty() ? GetDefaultScenePathString() : g_CurrentScenePath;
        if (!ShowSceneFileDialog(false, path))
            return false;
        return LoadSceneAssetFromPath(path);
    }

    bool LoadSceneAssetFromPath(const std::string& path)
    {
        if (path.empty())
            return false;
        if (!Scene::LoadFromFile(path))
            return false;
        ClearRuntimeWorldIfEditing("scene asset loaded");
        g_CurrentScenePath = path;
        if (!g_CurrentProjectPath.empty())
        {
            g_ProjectLastOpenedScenePath = MakePathRelativeToProjectRoot(path);
            SyncProjectStateToEditor();
        }
        if (g_engineInstance)
        {
            EditorState& editor = g_engineInstance->GetEditorState();
            editor.selection.Clear();
            editor.ClearHistory();
        }
        return true;
    }

    bool SaveCurrentSceneAsset()
    {
        if (g_CurrentScenePath.empty())
            return SaveSceneAs();
        return SaveSceneWithPath(g_CurrentScenePath);
    }

    bool SaveCurrentSceneAssetAs()
    {
        return SaveSceneAs();
    }

    std::string GetCurrentSceneAssetPath()
    {
        return g_CurrentScenePath;
    }

    void SetCurrentSceneAssetPath(const std::string& path)
    {
        g_CurrentScenePath = path;
        if (!g_CurrentProjectPath.empty() && !path.empty())
        {
            g_ProjectLastOpenedScenePath = MakePathRelativeToProjectRoot(path);
            SyncProjectStateToEditor();
        }
    }

    bool NewProject()
    {
        std::string path = g_CurrentProjectPath;
        if (!ShowProjectFileDialog(true, path))
            return false;

        const std::filesystem::path projectFilePath(path);
        const std::filesystem::path projectRoot = projectFilePath.parent_path();
        EnsureProjectDirectories(projectRoot);

        BuildProjectTemplateScene(g_SelectedProjectTemplateId, projectRoot, g_CurrentScenePath);
        if (!Scene::SaveToFile(g_CurrentScenePath))
            return false;

        g_ProjectStartupScenePath = MakePathRelativeToRoot(g_CurrentScenePath, projectRoot);
        g_ProjectLastOpenedScenePath = g_ProjectStartupScenePath;

        const bool saved = SaveProjectFileWithPath(path);
        if (saved && g_engineInstance)
        {
            EditorState& editor = g_engineInstance->GetEditorState();
            editor.selection.Clear();
            editor.focusedMaterialIndex = -1;
        }
        if (saved)
            g_ShowProjectBrowser = false;
        return saved;
    }

    bool OpenProject()
    {
        std::string path = g_CurrentProjectPath;
        if (!ShowProjectFileDialog(false, path))
            return false;
        const bool loaded = LoadProjectFileFromPath(path);
        if (loaded)
            g_ShowProjectBrowser = false;
        return loaded;
    }

    bool SaveCurrentProject()
    {
        if (g_CurrentProjectPath.empty())
            return SaveCurrentProjectAs();
        return SaveProjectFileWithPath(g_CurrentProjectPath);
    }

    bool SaveCurrentProjectAs()
    {
        std::string path = g_CurrentProjectPath;
        if (!ShowProjectFileDialog(true, path))
            return false;
        return SaveProjectFileWithPath(path);
    }

    std::string GetCurrentProjectPath()
    {
        return g_CurrentProjectPath;
    }

    std::string GetCurrentProjectName()
    {
        return g_CurrentProjectName;
    }

    std::string GetCurrentProjectRoot()
    {
        return g_CurrentProjectRoot.string();
    }

    bool IsImGuiDemoVisible() { return g_showImGuiDemo; }
    bool IsImGuiMetricsVisible() { return g_showImGuiMetrics; }

    static float GetCommandStripReservedHeight()
    {
        if (!g_showCommandStrip)
            return 0.0f;

        // Ensure the reserved area always fits the strip contents.
        const float minH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
        return (std::max)(g_commandStripHeight, minH);
    }

    namespace
    {
        static void LogImGuiSecondaryWindows()
        {
            ImGuiIO& io = ImGui::GetIO();
            if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
                return;

            static int s_counter = 0;
            if ((++s_counter % 120) != 0) // ~2 sec at 60fps
                return;

            ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
            ImGuiContext* ctx = ImGui::GetCurrentContext();
            const ImGuiViewport* mainVp = ImGui::GetMainViewport();
            const ImGuiID mainId = mainVp ? mainVp->ID : 0;

            Logger::Log(LogLevel::Info, std::format("🪟 ImGui Viewport Summary | Total={} Frame={}", pio.Viewports.Size, ImGui::GetFrameCount()));

            for (int i = 0; i < pio.Viewports.Size; ++i)
            {
                ImGuiViewport* vp = pio.Viewports[i];
                if (!vp)
                    continue;

                const bool isMain = (vp->ID == mainId);
                const uintptr_t hwnd = reinterpret_cast<uintptr_t>(vp->PlatformHandle);

                Logger::Log(LogLevel::Info, std::format(
                    "  - Viewport[{}] ID=0x{:X} Main={} HWND=0x{:X} Pos=({:.0f},{:.0f}) Size=({:.0f},{:.0f}) Dpi={:.2f}",
                    i, vp->ID, isMain, hwnd, vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y, vp->DpiScale));
            }

            if (ctx)
            {
                Logger::Log(LogLevel::Info, std::format("🧩 ImGui Windows | Count={} (showing top-level OS viewport assignment)", ctx->Windows.Size));
                for (int i = 0; i < ctx->Windows.Size; ++i)
                {
                    ImGuiWindow* w = ctx->Windows[i];
                    if (!w || w->Hidden)
                        continue;

                    ImGuiViewport* wvp = w->Viewport;
                    if (!wvp)
                        continue;

                    const bool isSecondary = (mainVp && wvp->ID != mainVp->ID);
                    if (!isSecondary)
                        continue;

                    Logger::Log(LogLevel::Info, std::format(
                        "    * Window '{}' -> ViewportID=0x{:X} HWND=0x{:X}",
                        w->Name, wvp->ID, reinterpret_cast<uintptr_t>(wvp->PlatformHandle)));
                }
            }
        }
    }

    void DrawEditorShell()
    {
        // Reset per-frame flags
        ++g_editorShellFrameCounter;
        g_dockspaceTouchedThisFrame = false;
        g_commandStripTouchedThisFrame = false;

        UI::BeginDockSpace();
        UI::DrawEditorPanels();
        UI::DrawOverlays();
        UI::EndDockSpaceFrame();
        DrawStartupProjectBrowser();

        if (UI::IsImGuiDemoVisible())
            ImGui::ShowDemoWindow();
        if (UI::IsImGuiMetricsVisible())
            ImGui::ShowMetricsWindow();

        // Log secondary viewport/windows mapping
        LogImGuiSecondaryWindows();

        // Diagnostic window (dockable) to confirm execution order.
        if (g_showFrameDiag)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const ImVec2 vpPos = vp ? vp->Pos : ImVec2(0, 0);
            const ImVec2 vpSize = vp ? vp->Size : ImVec2(0, 0);
            const ImVec2 workPos = vp ? vp->WorkPos : ImVec2(0, 0);
            const ImVec2 workSize = vp ? vp->WorkSize : ImVec2(0, 0);

            if (ImGui::Begin("Frame Diagnostics", &g_showFrameDiag))
            {
                ImGui::Text("Frame: %d", g_editorShellFrameCounter);
                ImGui::Text("BeginDockSpace(): %s", g_dockspaceTouchedThisFrame ? "YES" : "NO");
                ImGui::Text("DrawCommandStrip(): %s", g_commandStripTouchedThisFrame ? "YES" : "NO");
                ImGui::Separator();
                ImGui::Text("Viewport Pos: (%.0f, %.0f) Size: (%.0f, %.0f)", vpPos.x, vpPos.y, vpSize.x, vpSize.y);
                ImGui::Text("WorkPos:     (%.0f, %.0f) WorkSize: (%.0f, %.0f)", workPos.x, workPos.y, workSize.x, workSize.y);
            }
            ImGui::End();
        }
    }

	// 🗄️ ImGui .ini layout persistence
    void LoadLayoutFromDisk(const char* iniPath)
    {
        if (!iniPath || !*iniPath)
            return;

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = iniPath;

        // Safe even if the file doesn't exist: ImGui will just keep defaults.
        ImGui::LoadIniSettingsFromDisk(iniPath);
        Logger::Log(LogLevel::Info, std::string("📄 Loaded ImGui layout: ") + iniPath);
    }

	// 🗄️ ImGui .ini layout persistence
    void SaveLayoutToDisk(const char* iniPath)
    {
        if (!iniPath || !*iniPath)
            return;

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = iniPath;

        ImGui::SaveIniSettingsToDisk(iniPath);
        Logger::Log(LogLevel::Info, std::string("💾 Saved ImGui layout: ") + iniPath);
    }

    // 🎨 Synthwave theme implementation
    static void ApplySynthwaveStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        style.WindowRounding = 6.0f;
        style.FrameRounding = 5.0f;
        style.ScrollbarSize = 16.0f;

        colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.02f, 0.18f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.85f, 0.25f, 1.00f, 0.55f);
        colors[ImGuiCol_Header] = ImVec4(0.8f, 0.1f, 0.9f, 0.45f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.9f, 0.3f, 1.0f, 0.6f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.9f, 0.5f, 1.0f, 0.8f);

        colors[ImGuiCol_Button] = ImVec4(0.7f, 0.2f, 0.9f, 0.5f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.9f, 0.4f, 1.0f, 0.8f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.00f, 0.30f, 0.7f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.00f, 0.45f, 0.9f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.5f, 0.0f, 0.6f, 1.0f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.00f, 0.20f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.3f, 0.0f, 0.5f, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.00f, 0.10f, 0.75f);

        colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.6f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.8f, 0.3f, 1.0f, 0.9f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.4f, 1.0f, 1.0f);

        Logger::Log(LogLevel::Info, "🌈 Synthwave theme applied");
    }

    static void ApplyMagentaStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        style.WindowRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.ScrollbarSize = 14.0f;

        colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.00f, 0.15f, 1.00f); // Dark magenta background
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.00f, 0.22f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(1.00f, 0.25f, 0.90f, 0.45f);
        colors[ImGuiCol_Header] = ImVec4(1.0f, 0.2f, 0.8f, 0.5f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.4f, 0.9f, 0.75f);
        colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_Button] = ImVec4(0.8f, 0.1f, 0.7f, 0.6f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.3f, 0.9f, 0.85f);
        colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.30f, 0.00f, 0.40f, 0.7f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.45f, 0.00f, 0.55f, 0.9f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.6f, 0.0f, 0.7f, 1.0f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.00f, 0.25f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.4f, 0.0f, 0.5f, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.00f, 0.15f, 0.75f);

        colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.5f, 1.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.3f, 1.0f, 0.8f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.4f, 1.0f, 1.0f);

        Logger::Log(LogLevel::Info, "💖 Magenta theme applied");
    }


    // 🎨 Theme switcher
    void ApplyTheme(Theme theme) {
        currentTheme = theme;

        switch (theme) {
        case Theme::Dark:      ImGui::StyleColorsDark();    break;
        case Theme::Light:     ImGui::StyleColorsLight();   break;
        case Theme::Classic:   ImGui::StyleColorsClassic(); break;
        case Theme::Synthwave: ApplySynthwaveStyle();       break;
        case Theme::Magenta:   ApplyMagentaStyle();         break;
        }

        // 🛡️ Clamp alpha to prevent invalid values
        ImGuiStyle& style = ImGui::GetStyle();
        style.Alpha = std::clamp(style.Alpha, 0.85f, 1.0f);

        Logger::Log(LogLevel::Info, "🎨 Theme applied.");
    }

	// 🪟 Set main window size and center on screen
    void SetMainWindowSize(int width, int height)
    {
        HWND hWnd = Graphics::GetInstance().GetHWND(); // Or pass in HWND directly if preferred
        RECT rect = { 0, 0, width, height };

        // Adjust for window borders
        DWORD style = GetWindowLong(hWnd, GWL_STYLE);
        DWORD exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        // Center on screen
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;
        int posX = (screenWidth - windowWidth) / 2;
        int posY = (screenHeight - windowHeight) / 2;

        SetWindowPos(hWnd, nullptr, posX, posY, windowWidth, windowHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

	// 🎨 Get clear color based on current theme
    ImVec4 GetClearColor() {
        switch (currentTheme) {
        case Theme::Magenta:   return ImVec4(0.10f, 0.00f, 0.12f, 1.0f);
        case Theme::Synthwave: return ImVec4(0.08f, 0.04f, 0.16f, 1.0f);
        case Theme::Dark:      return ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        case Theme::Light:     return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        case Theme::Classic:   return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
        default:               return ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
        }
    }


    // 🖥️ GPU Dropdown Menu
    void ShowGPUSelectionMenu(const std::vector<std::wstring>& gpuList, int& selectedGPUIndex) {
        for (size_t i = 0; i < gpuList.size(); ++i) {
            std::string gpuName = Utils::WideStringToString(gpuList[i]);
            bool isSelected = (selectedGPUIndex == static_cast<int>(i));
            if (ImGui::MenuItem(gpuName.c_str(), nullptr, isSelected)) {
                selectedGPUIndex = static_cast<int>(i);
                GPUSelection::SelectGPU(selectedGPUIndex);
                Logger::Log(LogLevel::Info, "🎮 Switched to GPU index: " + std::to_string(i));
            }
        }
    }

    // 🛠️ Command Strip at Top
    // Refactored: command strip is rendered as a child region inside BeginDockSpace().
    void UI::DrawCommandStrip()
    {
        g_commandStripTouchedThisFrame = true;
    }

    // 🛋️ Dockspace layout management
    namespace
    {
         ImGuiID g_dockspaceID = 0;
         ImGuiID g_inspectorDockNodeID = 0;
         bool g_dockInitialized = false;
         bool g_requestResetLayout = false;
         bool g_buildLayoutNextFrame = false;

         enum class UILayoutPreset : int
         {
             Default = 0,
             Authoring,
             LevelDesign,
             Materials,
             AIBlackFlame,
             Debug,
             Minimal,
         };

         static void BuildDockLayout(UILayoutPreset preset, ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildDefaultDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildAuthoringDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildLevelDesignDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildMaterialsDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildAIBlackFlameDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildDebugDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void BuildMinimalDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport);
         static void ApplyLayoutPreset(UILayoutPreset preset);

         static ImGuiID FindDockNodeForWindow(const char* windowName)
         {
             if (!windowName || !windowName[0])
                 return 0;

             if (ImGuiWindow* window = ImGui::FindWindowByName(windowName))
                 return window->DockId;

             return 0;
         }

         static void DockPanelWithInspectorOnOpen(EditorPanel& panel, bool& wasOpenLastFrame)
         {
             if (!panel.open)
             {
                 wasOpenLastFrame = false;
                 return;
             }

             if (!wasOpenLastFrame)
             {
                 ImGuiID targetDockNodeID = FindDockNodeForWindow(EditorPanels::Inspector().name);
                 if (targetDockNodeID == 0)
                     targetDockNodeID = g_inspectorDockNodeID;
                 if (targetDockNodeID != 0)
                     ImGui::DockBuilderDockWindow(panel.name, targetDockNodeID);
             }

             wasOpenLastFrame = true;
         }

         static UILayoutPreset g_layoutPreset = UILayoutPreset::Default;

         static const char* LayoutPresetLabel(UILayoutPreset p)
         {
             switch (p)
             {
             case UILayoutPreset::Default: return "Default";
             case UILayoutPreset::Authoring: return "Authoring";
             case UILayoutPreset::LevelDesign: return "Level Design";
             case UILayoutPreset::Materials: return "Materials";
             case UILayoutPreset::AIBlackFlame: return "AI / Black Flame";
             case UILayoutPreset::Debug:   return "Debug";
             case UILayoutPreset::Minimal: return "Minimal";
             default: return "(unknown)";
             }
         }

         static const char* LayoutIniForPreset(UILayoutPreset p)
         {
             switch (p)
             {
             case UILayoutPreset::Default: return "imgui_default.ini";
             case UILayoutPreset::Authoring: return "imgui_authoring.ini";
             case UILayoutPreset::LevelDesign: return "imgui_level_design.ini";
             case UILayoutPreset::Materials: return "imgui_materials.ini";
             case UILayoutPreset::AIBlackFlame: return "imgui_ai_black_flame.ini";
             case UILayoutPreset::Debug:   return "imgui_debug.ini";
             case UILayoutPreset::Minimal: return "imgui_minimal.ini";
             default: return "imgui.ini";
             }
         }

         static void ConfigurePanelsForLayout(UILayoutPreset preset)
         {
             auto& scene = EditorPanels::Scene();
             auto& game = EditorPanels::Game();
             auto& hierarchy = EditorPanels::Hierarchy();
             auto& inspector = EditorPanels::Inspector();
             auto& assets = EditorPanels::Assets();
             auto& diagnostics = EditorPanels::Diagnostics();
             auto& logViewer = EditorPanels::LogViewer();
             auto& instancing = EditorPanels::Instancing();
             auto& materialPreview = EditorPanels::MaterialPreview();
             auto& blackFlame = EditorPanels::BlackFlame();
             auto& promptHelper = EditorPanels::PromptHelper();
             auto& prefabWorkflow = EditorPanels::PrefabWorkflow();

             scene.open = true;
             hierarchy.open = true;
             inspector.open = true;

             switch (preset)
             {
             case UILayoutPreset::Authoring:
                 game.open = false;
                 assets.open = true;
                 diagnostics.open = false;
                 logViewer.open = true;
                 instancing.open = false;
                 materialPreview.open = true;
                 blackFlame.open = true;
                 promptHelper.open = true;
                 prefabWorkflow.open = false;
                 break;
             case UILayoutPreset::LevelDesign:
                 game.open = false;
                 assets.open = true;
                 diagnostics.open = false;
                 logViewer.open = false;
                 instancing.open = false;
                 materialPreview.open = false;
                 blackFlame.open = false;
                 promptHelper.open = false;
                 prefabWorkflow.open = true;
                 break;
             case UILayoutPreset::Materials:
                 game.open = false;
                 assets.open = true;
                 diagnostics.open = false;
                 logViewer.open = false;
                 instancing.open = false;
                 materialPreview.open = true;
                 blackFlame.open = false;
                 promptHelper.open = false;
                 prefabWorkflow.open = false;
                 break;
             case UILayoutPreset::AIBlackFlame:
                 game.open = false;
                 assets.open = false;
                 diagnostics.open = false;
                 logViewer.open = true;
                 instancing.open = false;
                 materialPreview.open = false;
                 blackFlame.open = true;
                 promptHelper.open = true;
                 prefabWorkflow.open = false;
                 break;
             case UILayoutPreset::Debug:
                 game.open = true;
                 assets.open = true;
                 diagnostics.open = true;
                 logViewer.open = true;
                 instancing.open = true;
                 materialPreview.open = false;
                 blackFlame.open = true;
                 promptHelper.open = false;
                 prefabWorkflow.open = false;
                 break;
             case UILayoutPreset::Minimal:
                 game.open = false;
                 assets.open = false;
                 diagnostics.open = false;
                 logViewer.open = false;
                 instancing.open = false;
                 materialPreview.open = false;
                 blackFlame.open = false;
                 promptHelper.open = false;
                 prefabWorkflow.open = false;
                 break;
             case UILayoutPreset::Default:
             default:
                 game.open = true;
                 assets.open = true;
                 diagnostics.open = false;
                 logViewer.open = true;
                 instancing.open = false;
                 materialPreview.open = false;
                 blackFlame.open = true;
                 promptHelper.open = false;
                 prefabWorkflow.open = false;
                 break;
             }
         }

         static void DrawLayoutPresetMenuItems()
         {
             auto drawLayoutPresetItem = [](const char* label, UILayoutPreset preset, const char* description)
             {
                 if (ImGui::MenuItem(label, nullptr, g_layoutPreset == preset))
                     ApplyLayoutPreset(preset);
                 if (description && description[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                     ImGui::SetTooltip("%s", description);
             };

             ImGui::TextDisabled("Current: %s", LayoutPresetLabel(g_layoutPreset));
             ImGui::Separator();

             drawLayoutPresetItem("Default", UILayoutPreset::Default, "Balanced editor layout with Scene/Game centered, Hierarchy on the left, Inspector on the right, and supporting panels along the bottom.");
             drawLayoutPresetItem("Authoring", UILayoutPreset::Authoring, "Content-focused workspace with Black Flame, prompts, and material tools grouped beside the Inspector.");
             drawLayoutPresetItem("Level Design", UILayoutPreset::LevelDesign, "Optimized for building scenes with Hierarchy, Inspector, Prefab Workflow, and Assets kept close to the Scene view.");
             drawLayoutPresetItem("Materials", UILayoutPreset::Materials, "Surface-tuning layout with Scene, Inspector, and Material Preview emphasized for rapid material iteration.");
             drawLayoutPresetItem("AI / Black Flame", UILayoutPreset::AIBlackFlame, "AI-focused workspace that keeps Scene, Inspector, Black Flame Prompts, and Black Flame output visible together.");
             drawLayoutPresetItem("Debug", UILayoutPreset::Debug, "Engineering-oriented layout with diagnostics, logs, instancing, and runtime panels easier to monitor at once.");
             drawLayoutPresetItem("Minimal", UILayoutPreset::Minimal, "Reduced distraction layout with only the essential scene editing panels visible by default.");

             ImGui::Separator();
             if (ImGui::MenuItem("Rebuild Current Layout"))
                 ApplyLayoutPreset(g_layoutPreset);
         }

         static void ApplyLayoutPreset(UILayoutPreset preset)
         {
             g_layoutPreset = preset;
             ConfigurePanelsForLayout(preset);
             ImGui::LoadIniSettingsFromMemory("");
             g_inspectorDockNodeID = 0;
             g_dockInitialized = false;
             g_buildLayoutNextFrame = true;
         }

         static void BuildDockLayout(UILayoutPreset preset, ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             switch (preset)
             {
             case UILayoutPreset::Authoring:
                 BuildAuthoringDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::LevelDesign:
                 BuildLevelDesignDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::Materials:
                 BuildMaterialsDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::AIBlackFlame:
                 BuildAIBlackFlameDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::Debug:
                 BuildDebugDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::Minimal:
                 BuildMinimalDockLayout(dockspaceID, viewport);
                 break;
             case UILayoutPreset::Default:
             default:
                 BuildDefaultDockLayout(dockspaceID, viewport);
                 break;
             }
         }

         void BuildDefaultDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

            ImGui::DockBuilderRemoveNode(dockspaceID);
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            
            // Match the DockSpace region below the command strip inside the dockspace root.
            const float toolbarH = GetCommandStripReservedHeight();
            const float topPadding = ImGui::GetStyle().ItemSpacing.y;

            // ImGui asserts dock node sizes are non-negative. Clamp aggressively to a safe minimum.
            constexpr float kMinDockDim = 64.0f;
            const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
            const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
            const ImVec2 dockSize = ImVec2(safeW, safeH);
            ImGui::DockBuilderSetNodeSize(dockspaceID, dockSize);

            ImGuiID dock_main = dockspaceID;
            ImGuiID dock_left = 0;
            ImGuiID dock_right = 0;
            ImGuiID dock_bottom = 0;

            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, &dock_right, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, &dock_bottom, &dock_main);
            g_inspectorDockNodeID = dock_right;

            // Names must match ImGui::Begin("...") titles
            ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
            ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
            ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
            ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
            ImGui::DockBuilderDockWindow(EditorPanels::PromptHelper().name, dock_right);

            // Bottom region: tab these together by docking into the same node.
            ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);
            ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_bottom);
            ImGui::DockBuilderDockWindow(EditorPanels::Diagnostics().name, dock_bottom);
            ImGui::DockBuilderDockWindow(EditorPanels::Instancing().name, dock_bottom);
            ImGui::DockBuilderDockWindow(EditorPanels::BlackFlame().name, dock_bottom);

            ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildAuthoringDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;
             ImGuiID dock_bottom = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.18f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30f, &dock_right, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.24f, &dock_bottom, &dock_main);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PromptHelper().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::MaterialPreview().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::BlackFlame().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PrefabWorkflow().name, dock_right);

             ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::Diagnostics().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::Instancing().name, dock_bottom);

             ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildLevelDesignDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;
             ImGuiID dock_bottom = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.26f, &dock_right, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.24f, &dock_bottom, &dock_main);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PrefabWorkflow().name, dock_right);

             ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::Instancing().name, dock_bottom);

             ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildMaterialsDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;
             ImGuiID dock_right_bottom = 0;
             ImGuiID dock_bottom = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.16f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30f, &dock_right, &dock_main);
             ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.52f, &dock_right_bottom, &dock_right);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.22f, &dock_bottom, &dock_main);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::MaterialPreview().name, dock_right_bottom);

             ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_bottom);

             ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildAIBlackFlameDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;
             ImGuiID dock_right_bottom = 0;
             ImGuiID dock_bottom = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.17f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.34f, &dock_right, &dock_main);
             ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.48f, &dock_right_bottom, &dock_right);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.20f, &dock_bottom, &dock_main);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PromptHelper().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::BlackFlame().name, dock_right_bottom);

             ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::Diagnostics().name, dock_bottom);

             ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildDebugDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;
             ImGuiID dock_bottom = 0;
             ImGuiID dock_right_bottom = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.18f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.28f, &dock_right, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.28f, &dock_bottom, &dock_main);
             ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.46f, &dock_right_bottom, &dock_right);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::MaterialPreview().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PromptHelper().name, dock_right);

             ImGui::DockBuilderDockWindow(EditorPanels::Assets().name, dock_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::Instancing().name, dock_bottom);

             ImGui::DockBuilderDockWindow(EditorPanels::Diagnostics().name, dock_right_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::LogViewer().name, dock_right_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::BlackFlame().name, dock_right_bottom);
             ImGui::DockBuilderDockWindow(EditorPanels::PrefabWorkflow().name, dock_right_bottom);

             ImGui::DockBuilderFinish(dockspaceID);
         }

         void BuildMinimalDockLayout(ImGuiID dockspaceID, ImGuiViewport* viewport)
         {
             if (!viewport)
                 return;

             ImGui::DockBuilderRemoveNode(dockspaceID);
             ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);

             const float toolbarH = GetCommandStripReservedHeight();
             const float topPadding = ImGui::GetStyle().ItemSpacing.y;
             constexpr float kMinDockDim = 64.0f;
             const float safeW = (std::max)(kMinDockDim, viewport->WorkSize.x);
             const float safeH = (std::max)(kMinDockDim, viewport->WorkSize.y - toolbarH - topPadding);
             ImGui::DockBuilderSetNodeSize(dockspaceID, ImVec2(safeW, safeH));

             ImGuiID dock_main = dockspaceID;
             ImGuiID dock_left = 0;
             ImGuiID dock_right = 0;

             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.16f, &dock_left, &dock_main);
             ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, &dock_right, &dock_main);
             g_inspectorDockNodeID = dock_right;

             ImGui::DockBuilderDockWindow(EditorPanels::Scene().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Game().name, dock_main);
             ImGui::DockBuilderDockWindow(EditorPanels::Hierarchy().name, dock_left);
             ImGui::DockBuilderDockWindow(EditorPanels::Inspector().name, dock_right);
             ImGui::DockBuilderDockWindow(EditorPanels::PromptHelper().name, dock_right);

             ImGui::DockBuilderFinish(dockspaceID);
         }

        // Command strip contents
        static void DrawCommandStripContents()
         {
             const Engine::State s = Engine::GetState();
             const bool playing = (s == Engine::State::Playing);
             const bool paused = (s == Engine::State::Paused);
             const bool runtimeActive = playing || paused;
             LoadRecentProjects();

             // Left
             if (ImGui::Button(ICON_FA_BARS " Project##CmdProject"))
                 ImGui::OpenPopup("Project##CmdProjectPopup");
             if (ImGui::BeginPopup("Project##CmdProjectPopup"))
             {
                  if (!g_CurrentProjectName.empty())
                      ImGui::TextDisabled("Current: %s", g_CurrentProjectName.c_str());
                  if (ImGui::BeginMenu("New Project From Template##CmdProjectTemplate"))
                  {
                      for (const ProjectTemplateDefinition& definition : GetProjectTemplateDefinitions())
                      {
                          const bool selected = (g_SelectedProjectTemplateId == definition.id);
                          if (ImGui::MenuItem(definition.name.c_str(), nullptr, selected))
                          {
                              g_SelectedProjectTemplateId = definition.id;
                              NewProject();
                          }
                      }
                      ImGui::EndMenu();
                  }
                  if (ImGui::MenuItem("New Project..."))
                      NewProject();
                  if (ImGui::MenuItem("Open Project..."))
                      OpenProject();
                  if (ImGui::MenuItem("Save Project", nullptr, false, !g_CurrentProjectPath.empty()))
                      SaveCurrentProject();
                  if (ImGui::MenuItem("Save Project As..."))
                      SaveCurrentProjectAs();
                  if (ImGui::BeginMenu("Recent Projects##CmdProjectRecent"))
                  {
                      if (g_RecentProjectPaths.empty())
                      {
                          ImGui::MenuItem("No recent projects", nullptr, false, false);
                      }
                      else
                      {
                          for (size_t recentIndex = 0; recentIndex < g_RecentProjectPaths.size(); ++recentIndex)
                          {
                              const std::string label = std::format("{}##CmdRecentProject{}", g_RecentProjectPaths[recentIndex], recentIndex);
                              if (ImGui::MenuItem(label.c_str()))
                                  LoadProjectFileFromPath(g_RecentProjectPaths[recentIndex]);
                          }
                      }
                      ImGui::EndMenu();
                  }
                 ImGui::EndPopup();
             }
             ImGui::SameLine();
              ImGui::TextDisabled("%s", g_CurrentProjectName.empty() ? "No Project" : g_CurrentProjectName.c_str());
              ImGui::SameLine();

             if (ImGui::Button("Scene##CmdScene"))
                 ImGui::OpenPopup("Scene##CmdScenePopup");
             if (ImGui::BeginPopup("Scene##CmdScenePopup"))
             {
                 if (runtimeActive)
                     ImGui::BeginDisabled();
                 if (ImGui::MenuItem("New Scene"))
                     Engine::NewScene();
                 if (ImGui::MenuItem("Save Scene"))
                     Engine::SaveScene();
                 if (ImGui::MenuItem("Save Scene As..."))
                     SaveSceneAs();
                 if (ImGui::MenuItem("Load Scene..."))
                     LoadSceneFromDialog();
                 if (runtimeActive)
                     ImGui::EndDisabled();
                 ImGui::EndPopup();
             }
             ImGui::SameLine();

             if (ImGui::Button("Build##CmdBuild"))
             {
                 const std::filesystem::path buildRoot = std::filesystem::path("Build");
                 std::error_code ec;
                 std::filesystem::create_directories(buildRoot, ec);
                 Logger::Log(LogLevel::Info, std::format("Build stub executed at {}", buildRoot.string()), "[Build]");
             }

             // Compute layout metrics
             const ImGuiStyle& style = ImGui::GetStyle();
             const float spacingX = style.ItemSpacing.x;
             const float framePadX = style.FramePadding.x;

             auto calcButtonW = [&](const char* label) -> float
             {
                 return ImGui::CalcTextSize(label).x + framePadX * 2.0f;
             };

             // Use visible labels, but add hidden IDs to keep them unique/stable.
             const char* playLabel = ICON_FA_PLAY " Play##CmdPlay";
             const char* pauseLabel = paused ? ICON_FA_PLAY " Resume##CmdPause" : ICON_FA_PAUSE " Pause##CmdPause";
             const char* stopLabel = ICON_FA_STOP " Stop##CmdStop";
             const char* settingsLabel = ICON_FA_GEAR " Settings##CmdSettings";

             // NOTE: width calculations must use the visible portion, not the hidden IDs.
             const char* playVisible = ICON_FA_PLAY " Play";
             const char* pauseVisible = paused ? ICON_FA_PLAY " Resume" : ICON_FA_PAUSE " Pause";
             const char* stopVisible = ICON_FA_STOP " Stop";
             const char* settingsVisible = ICON_FA_GEAR " Settings";

             const float playW = calcButtonW(playVisible);
             const float pauseW = calcButtonW(pauseVisible);
             const float stopW = calcButtonW(stopVisible);
             const float centerGroupW = playW + pauseW + stopW + spacingX * 2.0f;

             const float settingsW = calcButtonW(settingsVisible);

             // Content region coordinates
             const float contentMinX = ImGui::GetWindowContentRegionMin().x;
             const float contentMaxX = ImGui::GetWindowContentRegionMax().x;
             const float contentW = (contentMaxX - contentMinX);

             // Determine where left group ends (relative to window)
             const float leftEndX = ImGui::GetCursorPosX();

             // Determine where right group should start so it is right aligned
             const float rightStartX = contentMaxX - settingsW;

             // Compute ideal center start
             float centerStartX = contentMinX + (contentW - centerGroupW) * 0.5f;

             // Clamp center group into the available middle span
             const float minCenterX = leftEndX + spacingX;
             const float maxCenterX = rightStartX - spacingX - centerGroupW;

             const bool hasRoomForCenter = (maxCenterX >= minCenterX);
             if (hasRoomForCenter)
             {
                 centerStartX = (std::max)(minCenterX, (std::min)(centerStartX, maxCenterX));
             }
             else
             {
                 // Not enough horizontal room for a centered group + right-aligned settings.
                 // Fall back: place center group right after left group; it may push towards the right,
                 // but we avoid SameLine() overlap artifacts.
                 centerStartX = minCenterX;
             }

             // Center group
             ImGui::SameLine(centerStartX);

             if (playing)
                 ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.70f, 0.20f, 1.0f));
             if (ImGui::Button(playLabel))
                  Engine::StartPlayMode();
             if (playing)
                 ImGui::PopStyleColor();

             ImGui::SameLine();

              if (!runtimeActive)
                  ImGui::BeginDisabled();
              if (paused)
                 ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.70f, 0.10f, 1.0f));
             if (ImGui::Button(pauseLabel))
                  Engine::TogglePauseMode();
             if (paused)
                 ImGui::PopStyleColor();
              if (!runtimeActive)
                  ImGui::EndDisabled();

             ImGui::SameLine();

              if (!runtimeActive)
                  ImGui::BeginDisabled();
              if (ImGui::Button(stopLabel))
                  Engine::StopPlayMode();
              if (!runtimeActive)
                  ImGui::EndDisabled();

            // Right (only right-align if it won't overlap the center group)
            const float afterCenterX = ImGui::GetCursorPosX();
            if (afterCenterX + spacingX <= rightStartX)
                ImGui::SameLine(rightStartX);
            else
                ImGui::SameLine();

            if (ImGui::Button(settingsLabel))
                ImGui::OpenPopup("Settings##CmdSettingsPopup");
            if (ImGui::BeginPopup("Settings##CmdSettingsPopup"))
            {
                EditorState* editor = g_engineInstance ? &g_engineInstance->GetEditorState() : nullptr;
                ImGui::MenuItem("Command Strip", nullptr, &g_showCommandStrip);
                if (editor)
                {
                    ImGui::Separator();
                    ImGui::MenuItem("Enable Gamepad Camera", nullptr, &editor->enableGamepadCamera);
                    ImGui::MenuItem("Editor Scene View Culling", nullptr, &editor->enableSceneViewCulling);
                    ImGui::MenuItem("Invert Look X", nullptr, &editor->invertLookX);
                    ImGui::MenuItem("Invert Look Y", nullptr, &editor->invertLookY);
                    ImGui::MenuItem("Smooth Look", nullptr, &editor->smoothLook);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Layouts"))
                {
                    DrawLayoutPresetMenuItems();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout"))
                    RequestResetLayout();
                ImGui::EndPopup();
            }
         }
     }

	// 🛋️ Dockspace Host with Menu Bar
    ImGuiID BeginDockSpace()
    {
        g_dockspaceTouchedThisFrame = true;

        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
            return 0;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return 0;

        // The dockspace host should follow the full main viewport client area.
        // Using WorkPos/WorkSize here can cause the host to shrink/shift during resize,
        // which looks like the UI is being squished/zoomed.
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        // Ensure the editor root visually owns the background.
        // (Without this, the swapchain clear color will show through.)
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_DockingEmptyBg] = style.Colors[ImGuiCol_WindowBg];

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("TFZ_DockSpaceRoot", nullptr, flags);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar())
        {
            UI::ShowMainMenu();
            ImGui::EndMenuBar();
        }

        if (g_dockspaceID == 0)
            g_dockspaceID = ImGui::GetID("TFZ_DockSpace");

        if (g_requestResetLayout)
        {
            ImGui::LoadIniSettingsFromMemory("");
            g_dockInitialized = false;
            g_buildLayoutNextFrame = true;
            g_requestResetLayout = false;
        }

        // Command strip child (sits under the menu bar).
        const float toolbarH = GetCommandStripReservedHeight();
        const float topPadding = ImGui::GetStyle().ItemSpacing.y;
        if (toolbarH > 0.0f)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            const ImVec4 bg = style.Colors[ImGuiCol_MenuBarBg];
            const ImVec4 border = style.Colors[ImGuiCol_Border];

            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
            ImGui::PushStyleColor(ImGuiCol_Border, border);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPadding);
            if (ImGui::BeginChild("TFZ_CommandStripChild", ImVec2(0.0f, toolbarH), ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar))
            {
                DrawCommandStripContents();
            }
            ImGui::EndChild();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        // Dockspace in remaining region below the command strip.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + topPadding);
        const ImVec2 dockPos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 dockSize = ImVec2((std::max)(0.0f, avail.x), (std::max)(0.0f, avail.y));

        // Constrain the dock node rectangle to the remaining region.
        ImGui::DockSpace(g_dockspaceID, dockSize, ImGuiDockNodeFlags_None);

        if (!g_dockInitialized)
        {
            g_dockInitialized = true;
            g_buildLayoutNextFrame = true;
        }

        ImGui::End();

        return g_dockspaceID;
    }

	// 🛋️ Finalize dockspace frame and apply pending layout if needed
    void EndDockSpaceFrame()
    {
        if (!g_buildLayoutNextFrame)
            return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        g_buildLayoutNextFrame = false;
        BuildDockLayout(g_layoutPreset, g_dockspaceID, viewport);
    }

	// 🔄 Request layout reset on next frame
    void RequestResetLayout()
    {
        ConfigurePanelsForLayout(g_layoutPreset);
        g_inspectorDockNodeID = 0;
        g_requestResetLayout = true;
    }

	// 🛠️ Draw all editor panels
    void UI::DrawEditorPanels()
    {
        static bool s_promptHelperWasOpenLastFrame = false;

        EditorPanels::DrawAll();

        auto& materialPreview = EditorPanels::MaterialPreview();
        if (materialPreview.open)
            materialPreview.draw();

        auto& blackFlame = EditorPanels::BlackFlame();
        if (blackFlame.open)
            blackFlame.draw();

        auto& promptHelper = EditorPanels::PromptHelper();
        if (promptHelper.open)
        {
            DockPanelWithInspectorOnOpen(promptHelper, s_promptHelperWasOpenLastFrame);
            promptHelper.draw();
        }
        else
        {
            s_promptHelperWasOpenLastFrame = false;
        }

        auto& prefabWorkflow = EditorPanels::PrefabWorkflow();
        if (prefabWorkflow.open)
            prefabWorkflow.draw();
    }

     // 🧭 Main Menu Bar
     void ShowMainMenu() {
         static bool vsyncEnabled = true;
         static int selectedGPUIndex = 0;
         static bool openAbout = false;
         static bool showCustomSizePopup = false;

        LoadRecentProjects();

         // 📁 File
         if (ImGui::BeginMenu("File")) {
              if (!g_CurrentProjectName.empty())
              {
                  ImGui::TextDisabled("Project: %s", g_CurrentProjectName.c_str());
                  ImGui::Separator();
              }
              if (ImGui::BeginMenu("New Project From Template"))
              {
                  for (const ProjectTemplateDefinition& definition : GetProjectTemplateDefinitions())
                  {
                      const bool selected = (g_SelectedProjectTemplateId == definition.id);
                      if (ImGui::MenuItem(definition.name.c_str(), nullptr, selected))
                      {
                          g_SelectedProjectTemplateId = definition.id;
                          NewProject();
                      }
                  }
                  ImGui::EndMenu();
              }
              if (ImGui::MenuItem("New Project..."))
                  NewProject();
              if (ImGui::MenuItem("Open Project..."))
                  OpenProject();
              if (ImGui::MenuItem("Save Project", nullptr, false, !g_CurrentProjectPath.empty()))
                  SaveCurrentProject();
              if (ImGui::MenuItem("Save Project As..."))
                  SaveCurrentProjectAs();
              if (ImGui::BeginMenu("Recent Projects"))
              {
                  if (g_RecentProjectPaths.empty())
                  {
                      ImGui::MenuItem("No recent projects", nullptr, false, false);
                  }
                  else
                  {
                      for (size_t recentIndex = 0; recentIndex < g_RecentProjectPaths.size(); ++recentIndex)
                      {
                          const std::string label = std::format("{}##RecentProject{}", g_RecentProjectPaths[recentIndex], recentIndex);
                          if (ImGui::MenuItem(label.c_str()))
                              LoadProjectFileFromPath(g_RecentProjectPaths[recentIndex]);
                      }
                  }
                  ImGui::EndMenu();
              }
              ImGui::Separator();
              if (ImGui::MenuItem("New Scene", "Ctrl+N"))
              {
                  PushUndoSnapshot();
                  Scene::NewScene();
                  ClearRuntimeWorldIfEditing("menu new scene");
                  g_CurrentScenePath.clear();
                  if (g_engineInstance)
                  {
                      EditorState& editor = g_engineInstance->GetEditorState();
                      editor.selection.Clear();
                      editor.ClearHistory();
                  }
              }
              if (ImGui::MenuItem("Load Scene...", "Ctrl+L"))
                  LoadSceneFromDialog();
              if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
              {
                  if (g_CurrentScenePath.empty())
                      SaveSceneAs();
                  else
                      SaveSceneWithPath(g_CurrentScenePath);
              }
              if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                  SaveSceneAs();
              ImGui::Separator();
              if (ImGui::MenuItem("Exit", "Alt+F4"))
             {
                 const HWND exitWindow = Graphics::GetInstance().GetHWND();
                 if (exitWindow)
                     PostMessage(exitWindow, WM_CLOSE, 0, 0);
             }
             ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Edit")) {
             const bool canUndo = EditorPanels::CanUndoCommand();
             const bool canRedo = EditorPanels::CanRedoCommand();
             const bool hasSelection = (Scene::GetSelectedInstance() != nullptr);
             const bool canUnpackPrefab = EditorPanels::CanUnpackPrefabSelection();
             if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                 EditorPanels::ExecuteUndoCommand();
             if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                 EditorPanels::ExecuteRedoCommand();
             ImGui::Separator();
             if (ImGui::MenuItem("Rename", nullptr, false, hasSelection))
                 EditorPanels::ExecuteRenameSelectionCommand();
             if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection))
                 EditorPanels::ExecuteDuplicateSelectionCommand();
             if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
                 EditorPanels::ExecuteDeleteSelectionCommand();
             ImGui::Separator();
             if (ImGui::MenuItem("Focus Selected", "F", false, hasSelection))
                 EditorPanels::ExecuteFocusSelectionCommand();
             if (ImGui::MenuItem("Create Empty Parent", nullptr, false, hasSelection))
                 EditorPanels::ExecuteCreateEmptyParentCommand();
             ImGui::Separator();
             if (ImGui::MenuItem("Save Prefab", nullptr, false, hasSelection))
                 EditorPanels::ExecuteSaveSelectionAsPrefabCommand();
             if (ImGui::MenuItem("Unpack Prefab", nullptr, false, canUnpackPrefab))
                 EditorPanels::ExecuteUnpackPrefabCommand();
             ImGui::EndMenu();
         }

         if (ImGui::BeginMenu("Create")) {
               if (ImGui::MenuItem("Empty Object"))
               {
                   PushUndoSnapshot();
                   Scene::CreateEmpty();
               }
               if (ImGui::MenuItem("Camera"))
               {
                   PushUndoSnapshot();
                   Scene::CreateCamera();
               }
               if (ImGui::MenuItem("Cube"))
               {
                   PushUndoSnapshot();
                   Scene::CreateCube();
               }
              if (ImGui::MenuItem("Sphere"))
              {
                  PushUndoSnapshot();
                  Scene::CreateSphere();
              }
              if (ImGui::MenuItem("Plane"))
              {
                  PushUndoSnapshot();
                  Scene::CreatePlane();
              }
              if (ImGui::MenuItem("Cylinder"))
              {
                  PushUndoSnapshot();
                  Scene::CreateCylinder();
              }
               if (ImGui::MenuItem("Capsule"))
               {
                   PushUndoSnapshot();
                   Scene::CreateCapsule();
               }
               if (ImGui::MenuItem("Torus"))
               {
                   PushUndoSnapshot();
                   Scene::CreateTorus();
               }
               if (ImGui::MenuItem("Cone"))
               {
                   PushUndoSnapshot();
                   Scene::CreateCone();
               }
              ImGui::EndMenu();
          }

         // 🖥️ Graphics
         if (ImGui::BeginMenu("Graphics")) {
             ImGui::Text("Select GPU:");
             if (!GPUSelection::gpuList.empty()) {
                 std::string currentGPU = Utils::WideStringToString(GPUSelection::gpuList[selectedGPUIndex]);
                 ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "Current: %s", currentGPU.c_str());
             }
             ShowGPUSelectionMenu(GPUSelection::gpuList, selectedGPUIndex);
             ImGui::EndMenu();
         }

         // ⚙️ Options
         if (ImGui::BeginMenu("Options")) {
             if (ImGui::BeginMenu("Layouts"))
             {
                 DrawLayoutPresetMenuItems();
                 ImGui::EndMenu();
             }

             ImGui::Separator();

             if (ImGui::MenuItem("VSync", nullptr, vsyncEnabled)) {
                 vsyncEnabled = !vsyncEnabled;
                 Logger::Log(LogLevel::Info, vsyncEnabled ? "✅ VSync Enabled" : "⛔ VSync Disabled");
             }

             // Shared editor state
             extern Engine* g_engineInstance;
             EditorState& editor = ::g_engineInstance->GetEditorState();

             auto drawStepFloat = [](const char* label, float& value, float minValue, float maxValue, float step, const char* format)
             {
                 ImGui::PushID(label);
                 ImGui::TextUnformatted(label);
                 ImGui::SameLine();
                 if (ImGui::SmallButton("-"))
                 {
                     value -= step;
                     if (value < minValue)
                         value = minValue;
                 }
                 ImGui::SameLine();
                 ImGui::Text(format, value);
                 ImGui::SameLine();
                 if (ImGui::SmallButton("+"))
                 {
                     value += step;
                     if (value > maxValue)
                         value = maxValue;
                 }
                 ImGui::PopID();
             };
              auto drawStepInt = [](const char* label, int& value, int minValue, int maxValue, int step)
              {
                  ImGui::PushID(label);
                  ImGui::TextUnformatted(label);
                  ImGui::SameLine();
                  if (ImGui::SmallButton("-"))
                  {
                      value -= step;
                      if (value < minValue)
                          value = minValue;
                  }
                  ImGui::SameLine();
                  ImGui::Text("%d", value);
                  ImGui::SameLine();
                  if (ImGui::SmallButton("+"))
                  {
                      value += step;
                      if (value > maxValue)
                          value = maxValue;
                  }
                  ImGui::PopID();
              };

             if (ImGui::BeginMenu("Lighting"))
             {
                 auto& light = Graphics::GetInstance().GetDirectionalLight();
                 drawStepFloat("Direction X", light.direction.x, -1.0f, 1.0f, 0.1f, "%.2f");
                 drawStepFloat("Direction Y", light.direction.y, -1.0f, 1.0f, 0.1f, "%.2f");
                 drawStepFloat("Direction Z", light.direction.z, -1.0f, 1.0f, 0.1f, "%.2f");
                 ImGui::Separator();
                 drawStepFloat("Color R", light.color.x, 0.0f, 4.0f, 0.05f, "%.2f");
                 drawStepFloat("Color G", light.color.y, 0.0f, 4.0f, 0.05f, "%.2f");
                 drawStepFloat("Color B", light.color.z, 0.0f, 4.0f, 0.05f, "%.2f");
                 ImGui::Separator();
                 drawStepFloat("Intensity", light.intensity, 0.0f, 8.0f, 0.1f, "%.2f");
                 drawStepFloat("Ambient", light.ambient, 0.0f, 1.0f, 0.02f, "%.2f");
                 ImGui::EndMenu();
             }

              if (ImGui::BeginMenu("Camera Navigation"))
              {
                  const bool isUnity = (editor.cameraNavMode == CameraNavMode::Unity_AltMouse);
                  const bool isUnityWASD = (editor.cameraNavMode == CameraNavMode::Unity_WASDMouse);
                  const bool isBlender = (editor.cameraNavMode == CameraNavMode::Blender_MMB);
                  const bool isTFZ = (editor.cameraNavMode == CameraNavMode::TFZ_RMB);
                  const bool isLaptopFriendly = (editor.cameraNavMode == CameraNavMode::Laptop_Friendly);

                  if (ImGui::MenuItem("Unity (Alt + Mouse)", nullptr, isUnity))
                      editor.cameraNavMode = CameraNavMode::Unity_AltMouse;
                  if (ImGui::MenuItem("Unity (WASD + Mouse)", nullptr, isUnityWASD))
                      editor.cameraNavMode = CameraNavMode::Unity_WASDMouse;
                  if (ImGui::MenuItem("Blender (MMB)", nullptr, isBlender))
                      editor.cameraNavMode = CameraNavMode::Blender_MMB;
                  if (ImGui::MenuItem("TFZ (RMB)", nullptr, isTFZ))
                      editor.cameraNavMode = CameraNavMode::TFZ_RMB;
                  if (ImGui::MenuItem("Laptop-Friendly", nullptr, isLaptopFriendly))
                      editor.cameraNavMode = CameraNavMode::Laptop_Friendly;

                  ImGui::Separator();
                  ImGui::MenuItem("Enable Gamepad Camera", nullptr, &editor.enableGamepadCamera);
                  ImGui::MenuItem("Editor Scene View Culling", nullptr, &editor.enableSceneViewCulling);
                  ImGui::MenuItem("Invert Look X", nullptr, &editor.invertLookX);
                  ImGui::MenuItem("Invert Look Y", nullptr, &editor.invertLookY);
                  ImGui::MenuItem("Smooth Look", nullptr, &editor.smoothLook);
                  drawStepFloat("Look Smoothing", editor.lookSmoothing, 0.0f, 0.90f, 0.05f, "%.2f");
                  drawStepFloat("Fly Look Speed", editor.flyLookSpeed, 0.0005f, 0.0060f, 0.0005f, "%.4f");
                  drawStepFloat("Orbit Look Speed", editor.orbitLookSpeed, 0.0010f, 0.0120f, 0.0005f, "%.4f");
                  drawStepFloat("Fly Move Speed", editor.flyMoveSpeed, 1.0f, 50.0f, 1.0f, "%.1f");
                  if (editor.enableGamepadCamera)
                  {
                      ImGui::Separator();
                      ImGui::TextUnformatted("Gamepad Camera");
                      drawStepFloat("Stick Deadzone", editor.gamepadStickDeadzone, 0.05f, 0.35f, 0.01f, "%.2f");
                      drawStepFloat("Look Sensitivity", editor.gamepadLookSensitivity, 0.25f, 3.0f, 0.05f, "%.2f");
                      drawStepFloat("Move Sensitivity", editor.gamepadMoveSensitivity, 0.25f, 3.0f, 0.05f, "%.2f");
                      drawStepFloat("Zoom Sensitivity", editor.gamepadZoomSensitivity, 0.25f, 3.0f, 0.05f, "%.2f");
                  }

                  ImGui::Separator();
                  if (ImGui::BeginMenu("Grid"))
                  {
                      ImGui::MenuItem("Enable Distance Fade", nullptr, &editor.gridFadeEnabled);
                      ImGui::MenuItem("Strengthen Major Lines", nullptr, &editor.gridMajorLinesEnabled);
                      drawStepFloat("Fade Distance", editor.gridFadeDistance, 5.0f, 200.0f, 5.0f, "%.0f");
                      drawStepFloat("Visibility", editor.gridVisibility, 0.10f, 2.00f, 0.05f, "%.2f");
                      drawStepFloat("Major Line Boost", editor.gridMajorLineBoost, 1.00f, 3.00f, 0.05f, "%.2f");
                      drawStepFloat("Axis Emphasis", editor.gridAxisEmphasis, 1.00f, 3.00f, 0.05f, "%.2f");
                      drawStepFloat("Grid Extent", editor.gridExtent, 1.0f, 200.0f, 1.0f, "%.0f");
                      drawStepInt("Grid Divisions", editor.gridDivisions, 2, 200, 2);
                      ImGui::Separator();
                      if (ImGui::MenuItem("Reset Grid Defaults"))
                      {
                          editor.gridFadeEnabled = true;
                          editor.gridMajorLinesEnabled = true;
                          editor.gridFadeDistance = 40.0f;
                          editor.gridVisibility = 1.0f;
                          editor.gridMajorLineBoost = 1.35f;
                          editor.gridAxisEmphasis = 1.25f;
                          editor.gridExtent = 10.0f;
                          editor.gridDivisions = 20;
                      }
                      ImGui::EndMenu();
                  }

                  ImGui::EndMenu();
              }

              if (ImGui::BeginMenu("View Mode"))
              {
                  auto& gfx = Graphics::GetInstance();
                  auto& sceneCamera = gfx.GetSceneCamera();
                  const bool is3D = (gfx.GetViewMode() == ViewMode::Mode3D);
                  const bool is2D = (gfx.GetViewMode() == ViewMode::Mode2D);

                  if (ImGui::MenuItem("3D", nullptr, is3D))
                  {
                      gfx.SetViewMode(ViewMode::Mode3D);
                      sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View3D);
                  }
                  if (ImGui::MenuItem("2D", nullptr, is2D))
                  {
                      gfx.SetViewMode(ViewMode::Mode2D);
                      sceneCamera.SetViewPreset(SceneCamera::ViewPreset::View2D);
                  }

                  ImGui::EndMenu();
              }

              if (ImGui::BeginMenu("Projection"))
              {
                  auto& sceneCamera = Graphics::GetInstance().GetSceneCamera();
                  const bool isPerspective = (sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Perspective);
                  const bool isOrtho = (sceneCamera.GetProjectionMode() == SceneCamera::ProjectionMode::Orthographic);

                  if (ImGui::MenuItem("Perspective", nullptr, isPerspective, !sceneCamera.Is2DMode()))
                     sceneCamera.SetProjectionMode(SceneCamera::ProjectionMode::Perspective);
                  if (ImGui::MenuItem("Orthographic", nullptr, isOrtho))
                     sceneCamera.SetProjectionMode(SceneCamera::ProjectionMode::Orthographic);

                  ImGui::EndMenu();
              }

              if (ImGui::BeginMenu("Debug Views"))
              {
                  const bool isLit = (editor.sceneDebugViewMode == SceneDebugViewMode::Lit);
                  const bool isAlbedo = (editor.sceneDebugViewMode == SceneDebugViewMode::Albedo);
                  const bool isNormals = (editor.sceneDebugViewMode == SceneDebugViewMode::Normals);
                  const bool isMetallic = (editor.sceneDebugViewMode == SceneDebugViewMode::Metallic);
                  const bool isRoughness = (editor.sceneDebugViewMode == SceneDebugViewMode::Roughness);
                  const bool isLightingOnly = (editor.sceneDebugViewMode == SceneDebugViewMode::LightingOnly);
                  const bool isAmbientOnly = (editor.sceneDebugViewMode == SceneDebugViewMode::AmbientOnly);
                  const bool isSpecularOnly = (editor.sceneDebugViewMode == SceneDebugViewMode::SpecularOnly);
                  const bool isSelectionMask = (editor.sceneDebugViewMode == SceneDebugViewMode::SelectionMask);
                  const bool isDepth = (editor.sceneDebugViewMode == SceneDebugViewMode::Depth);
                  const bool isLinearDepth = (editor.sceneDebugViewMode == SceneDebugViewMode::LinearDepth);
                  const bool isWorldPosition = (editor.sceneDebugViewMode == SceneDebugViewMode::WorldPosition);
                  const bool isUVs = (editor.sceneDebugViewMode == SceneDebugViewMode::UVs);
                  const bool isLightDirection = (editor.sceneDebugViewMode == SceneDebugViewMode::LightDirection);

                  if (ImGui::MenuItem("Lit", nullptr, isLit))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Lit;
                  if (ImGui::MenuItem("Albedo", nullptr, isAlbedo))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Albedo;
                  if (ImGui::MenuItem("Normals", nullptr, isNormals))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Normals;
                  if (ImGui::MenuItem("Metallic", nullptr, isMetallic))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Metallic;
                  if (ImGui::MenuItem("Roughness", nullptr, isRoughness))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Roughness;
                  if (ImGui::MenuItem("Lighting Only", nullptr, isLightingOnly))
                      editor.sceneDebugViewMode = SceneDebugViewMode::LightingOnly;
                  if (ImGui::MenuItem("Ambient Only", nullptr, isAmbientOnly))
                      editor.sceneDebugViewMode = SceneDebugViewMode::AmbientOnly;
                  if (ImGui::MenuItem("Specular Only", nullptr, isSpecularOnly))
                      editor.sceneDebugViewMode = SceneDebugViewMode::SpecularOnly;
                  if (ImGui::MenuItem("Selection Mask", nullptr, isSelectionMask))
                      editor.sceneDebugViewMode = SceneDebugViewMode::SelectionMask;
                  if (ImGui::MenuItem("Depth", nullptr, isDepth))
                      editor.sceneDebugViewMode = SceneDebugViewMode::Depth;
                  if (ImGui::MenuItem("Linear Depth", nullptr, isLinearDepth))
                      editor.sceneDebugViewMode = SceneDebugViewMode::LinearDepth;
                  if (ImGui::MenuItem("World Position", nullptr, isWorldPosition))
                      editor.sceneDebugViewMode = SceneDebugViewMode::WorldPosition;
                  if (ImGui::MenuItem("UVs", nullptr, isUVs))
                      editor.sceneDebugViewMode = SceneDebugViewMode::UVs;
                  if (ImGui::MenuItem("Light Direction", nullptr, isLightDirection))
                      editor.sceneDebugViewMode = SceneDebugViewMode::LightDirection;

                  ImGui::EndMenu();
              }

              if (ImGui::BeginMenu("Theme")) {
                  if (ImGui::MenuItem("Dark", nullptr, currentTheme == Theme::Dark))         ApplyTheme(Theme::Dark);
                  if (ImGui::MenuItem("Light", nullptr, currentTheme == Theme::Light))       ApplyTheme(Theme::Light);
                  if (ImGui::MenuItem("Classic", nullptr, currentTheme == Theme::Classic))   ApplyTheme(Theme::Classic);
                  if (ImGui::MenuItem("Synthwave", nullptr, currentTheme == Theme::Synthwave)) ApplyTheme(Theme::Synthwave);
                  if (ImGui::MenuItem("Magenta", nullptr, currentTheme == Theme::Magenta))   ApplyTheme(Theme::Magenta);
                  ImGui::EndMenu();
              }

              if (ImGui::BeginMenu("Panels")) {
                  auto& scene = EditorPanels::Scene();
                  auto& game = EditorPanels::Game();
                  auto& hierarchy = EditorPanels::Hierarchy();
                  auto& inspector = EditorPanels::Inspector();
                  auto& assets = EditorPanels::Assets();
                  auto& instancing = EditorPanels::Instancing();
                  auto& materialPreview = EditorPanels::MaterialPreview();
                  auto& blackFlame = EditorPanels::BlackFlame();
                  auto& promptHelper = EditorPanels::PromptHelper();
                  auto& prefabWorkflow = EditorPanels::PrefabWorkflow();
                  auto& debugOverlay = EditorPanels::DebugOverlay();
                  auto& diagnostics = EditorPanels::Diagnostics();
                  auto& logViewer = EditorPanels::LogViewer();

                  ImGui::MenuItem(scene.name, nullptr, &scene.open);
                  ImGui::MenuItem(game.name, nullptr, &game.open);
                  ImGui::MenuItem(hierarchy.name, nullptr, &hierarchy.open);
                  ImGui::MenuItem(inspector.name, nullptr, &inspector.open);
                  ImGui::MenuItem(assets.name, nullptr, &assets.open);
                  ImGui::MenuItem(instancing.name, nullptr, &instancing.open);
                  ImGui::MenuItem(materialPreview.name, nullptr, &materialPreview.open);
                  ImGui::MenuItem(blackFlame.name, nullptr, &blackFlame.open);
                  ImGui::MenuItem(promptHelper.name, nullptr, &promptHelper.open);
                  ImGui::MenuItem(prefabWorkflow.name, nullptr, &prefabWorkflow.open);
                  ImGui::Separator();
                  ImGui::MenuItem(debugOverlay.name, nullptr, &debugOverlay.open);
                  ImGui::MenuItem(diagnostics.name, nullptr, &diagnostics.open);
                  ImGui::MenuItem(logViewer.name, nullptr, &logViewer.open);
                  ImGui::Separator();
                  ImGui::MenuItem("Command Strip", nullptr, &g_showCommandStrip);
                  ImGui::MenuItem("Frame Diagnostics", nullptr, &g_showFrameDiag);
                  ImGui::EndMenu();
               }

               EditorPanels::DrawPrefabOptionsMenu();

               ImGui::Separator();
               if (ImGui::MenuItem("Reset Layout")) {
                   UI::RequestResetLayout();
               }
               if (ImGui::MenuItem("Save Layout")) {
                   UI::SaveLayoutToDisk("imgui.ini");
               }

               // Custom Font Size
               static float fontSize = 16.0f;
               if (ImGui::BeginMenu("Font Size")) {
                   if (ImGui::MenuItem("Small (14px)", nullptr, fontSize == 14.0f)) {
                       fontSize = 14.0f;
                        ApplyEditorFontSize(fontSize);
                   }
                   if (ImGui::MenuItem("Medium (16px)", nullptr, fontSize == 16.0f)) {
                       fontSize = 16.0f;
                        ApplyEditorFontSize(fontSize);
                   }
                   if (ImGui::MenuItem("Large (18px)", nullptr, fontSize == 18.0f)) {
                       fontSize = 18.0f;
                        ApplyEditorFontSize(fontSize);
                   }
                   if (ImGui::MenuItem("Extra Large (20px)", nullptr, fontSize == 20.0f)) {
                       fontSize = 20.0f;
                        ApplyEditorFontSize(fontSize);
                   }
                   ImGui::EndMenu();
               }

               // Custom Window Size
               if (ImGui::BeginMenu("Window Size")) {
                   if (ImGui::MenuItem("1280 x 720")) {
                       SetWindowPos(GetActiveWindow(), 0, 100, 100, 1280, 720, SWP_NOZORDER | SWP_SHOWWINDOW);
                       Logger::Log(LogLevel::Info, "📐 Resolution set to 1280x720");
                   }
                   if (ImGui::MenuItem("1600 x 900")) {
                       SetWindowPos(GetActiveWindow(), 0, 100, 100, 1600, 900, SWP_NOZORDER | SWP_SHOWWINDOW);
                       Logger::Log(LogLevel::Info, "📐 Resolution set to 1600x900");
                   }
                   if (ImGui::MenuItem("1920 x 1080")) {
                       SetWindowPos(GetActiveWindow(), 0, 100, 100, 1920, 1080, SWP_NOZORDER | SWP_SHOWWINDOW);
                       Logger::Log(LogLevel::Info, "📐 Resolution set to 1920x1080");
                   }
                   if (ImGui::MenuItem("2560 x 1440")) {
                       SetWindowPos(GetActiveWindow(), 0, 100, 100, 2560, 1440, SWP_NOZORDER | SWP_SHOWWINDOW);
                       Logger::Log(LogLevel::Info, "📐 Resolution set to 2560x1440");
                   }
                   if (ImGui::MenuItem("Custom...")) {
                       ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing, ImVec2(0.0f, 0.0f));
                       showCustomSizePopup = true;
                   }
                   ImGui::EndMenu();
               }

               ImGui::EndMenu();
           }

           // ❓ Help
           if (ImGui::BeginMenu("Help")) {
               ImGui::MenuItem("ImGui Demo", nullptr, &g_showImGuiDemo);
               ImGui::MenuItem("ImGui Metrics/Debugger", nullptr, &g_showImGuiMetrics);
               ImGui::Separator();
               if (ImGui::MenuItem("About")) {
                   openAbout = true;
               }
               ImGui::EndMenu();
           }

           // ✅ Defer popup open outside the menu
           if (openAbout) {
               ImGui::OpenPopup("AboutPopup");
               openAbout = false;
           }

           if (showCustomSizePopup) {
               ImGui::OpenPopup("Custom Size");
               showCustomSizePopup = false;
           }

           // 📦 About Modal
           if (ImGui::BeginPopupModal("AboutPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
               ImGui::Text("TheFletchZone Game Engine");
               ImGui::Text("Version 1.0");
               ImGui::Spacing();
               ImGui::Text("Built with love and DirectX 12!");

               if (ImGui::Button("Close")) {
                   ImGui::CloseCurrentPopup();
               }
               ImGui::EndPopup();
           }

           // Custom Size Modal
           static int customWidth = 1280;
           static int customHeight = 720;
           if (ImGui::BeginPopupModal("Custom Size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
               ImGui::InputInt("Width", &customWidth);
               ImGui::InputInt("Height", &customHeight);
               ImGui::Spacing();

               if (ImGui::Button("Apply")) {
                   SetMainWindowSize(customWidth, customHeight);
                   Logger::Log(LogLevel::Info, "📐 Custom resolution set to " + std::to_string(customWidth) + "x" + std::to_string(customHeight));
                   ImGui::CloseCurrentPopup();
               }
               ImGui::SameLine();
               if (ImGui::Button("Cancel")) {
                   ImGui::CloseCurrentPopup();
               }

               ImGui::EndPopup();
           }
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

       void DrawMainMenuBar()
       {
       }

       // 🔍 Optional Debug Overlay
       void DrawOverlays() {
            if (!g_engineInstance)
                return;

            if (Engine::GetState() == Engine::State::Editing)
                return;

            const Game& game = g_engineInstance->GetGame();
            const VaultMissionState missionState = game.GetVaultMissionState();

            ImGuiViewport* viewport = ImGui::GetMainViewport();

            // End-state and presentation overlays are drawn inside the editor Game panel
            // so they stay over the game viewport instead of the full editor viewport.
       }

       void DrawSplashOverlay()
       {
         SplashScreen::DrawSplash();
       }
    }
