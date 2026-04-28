#include "imgui.h"
#include "EditorPanels.h"
#include "EditorState.h"
#include "Scene.h"
#include "MaterialManager.h"
#include "Graphics.h"
#include "Engine.h"
#include "Logger.h"
#include "BlackFlameAI.h"
#include "TextureManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

extern Engine* g_engineInstance;

namespace EditorPanels
{
    namespace
    {
        static char g_BlackFlamePromptBuffer[2048] = {};
        static bool g_BlackFlameConfirmApply = false;
        static bool g_MaterialPreviewAutoRotate = true;
        static float g_MaterialPreviewSize = 210.0f;
        static int g_MaterialPreviewQuality = 56;
        static float g_MaterialPreviewRotation = 0.0f;
        static char g_PrefabLibrarySearchBuffer[128] = {};
        static char g_PrefabLibraryRenameBuffer[128] = {};
        static char g_PrefabLibraryCategoryBuffer[128] = {};
        static char g_PrefabLibraryTagsBuffer[256] = {};
        static char g_PrefabRebaseSearchBuffer[128] = {};
        static std::filesystem::path g_PendingPrefabRenamePath;
        static std::filesystem::path g_PendingPrefabDeletePath;
        static std::filesystem::path g_PendingPrefabCategoryPath;
        static std::filesystem::path g_PendingPrefabTagsPath;
        static std::filesystem::path g_PendingPrefabRebaseTargetPath;
        static bool g_RequestOpenPrefabRenamePopup = false;
        static bool g_RequestOpenPrefabDeletePopup = false;
        static bool g_RequestOpenPrefabCategoryPopup = false;
        static bool g_RequestOpenPrefabTagsPopup = false;
        static bool g_RequestOpenPrefabRebasePopup = false;
        static std::string g_PrefabLibraryCategoryFilter = "All Categories";
        static std::string g_PrefabLibraryTagFilter = "All Tags";

        enum class PrefabLibrarySortMode : uint8_t
        {
            NameAsc = 0,
            NameDesc,
            InstancesDesc,
            NewestFirst,
        };

        enum class PrefabLibraryTypeFilter : uint8_t
        {
            All = 0,
            BaseOnly,
            VariantsOnly,
        };

        static PrefabLibrarySortMode g_PrefabLibrarySortMode = PrefabLibrarySortMode::NameAsc;
        static PrefabLibraryTypeFilter g_PrefabLibraryTypeFilter = PrefabLibraryTypeFilter::All;

        static ImTextureID GetTexturePreviewID(const std::string& texturePath)
        {
            if (texturePath.empty())
                return 0;
            if (Texture* texture = TextureManager::GetInstance().LoadTexture(texturePath))
            {
                if (texture->srvGPU.ptr != 0)
                    return (ImTextureID)texture->srvGPU.ptr;
            }
            return 0;
        }

        static bool DrawPreviewTickFloat(const char* label, float& value, float minValue, float maxValue, float step, const char* format = "%.2f")
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
            ImGui::SetNextItemWidth(120.0f);
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

        static bool DrawPreviewTickInt(const char* label, int& value, int minValue, int maxValue, int step)
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
            ImGui::SetNextItemWidth(120.0f);
            changed = ImGui::DragInt("##value", &value, (float)step, minValue, maxValue) || changed;
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

        static ImTextureID GetPrefabThumbnailID(const std::filesystem::path& prefabPath)
        {
            const std::filesystem::path prefabStem = prefabPath.stem().stem();
            const std::filesystem::path prefabDir = prefabPath.parent_path();
            const std::filesystem::path thumbnailCandidates[] =
            {
                prefabDir / (prefabStem.string() + ".png"),
                prefabDir / (prefabStem.string() + ".jpg"),
                prefabDir / (prefabStem.string() + ".jpeg"),
                std::filesystem::path("Assets") / "Textures" / (prefabStem.string() + ".png"),
                std::filesystem::path("Assets") / "Textures" / (prefabStem.string() + ".jpg"),
                std::filesystem::path("Assets") / "Textures" / "crate.png",
                std::filesystem::path("Assets") / "Icons" / "The_Fletch_Zone_Icon.png",
            };

            for (const auto& candidate : thumbnailCandidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && !ec)
                {
                    if (ImTextureID preview = GetTexturePreviewID(candidate.string()))
                        return preview;
                }
            }

            return 0;
        }

        static const char* PrefabTypeFilterLabel(PrefabLibraryTypeFilter filter)
        {
            switch (filter)
            {
            case PrefabLibraryTypeFilter::BaseOnly: return "Base Prefabs";
            case PrefabLibraryTypeFilter::VariantsOnly: return "Variants";
            case PrefabLibraryTypeFilter::All:
            default: return "All Prefabs";
            }
        }

        static void PushUndoSnapshot(EditorState& editor)
        {
            SceneHistoryEntry snapshot{};
            snapshot.sceneSnapshot = Scene::SerializeToString();
            snapshot.activeSelectedInstanceId = Scene::GetSelectedInstanceId();
            snapshot.selectedInstanceIds = Scene::GetSelectedInstanceIds();
            if (!editor.undoSceneSnapshots.empty() &&
                editor.undoSceneSnapshots.back().sceneSnapshot == snapshot.sceneSnapshot &&
                editor.undoSceneSnapshots.back().activeSelectedInstanceId == snapshot.activeSelectedInstanceId &&
                editor.undoSceneSnapshots.back().selectedInstanceIds == snapshot.selectedInstanceIds)
                return;

            editor.undoSceneSnapshots.push_back(std::move(snapshot));
            editor.redoSceneSnapshots.clear();
        }

        struct BlackFlamePromptEntry
        {
            std::string Label;
            std::string Prompt;
            bool IsRecommended = false;
            float Score = 0.0f;
            size_t SuggestionIndex = static_cast<size_t>(-1);
            std::vector<BlackFlameCommand> ProposedCommands;
        };

        static std::string InferPromptFromCommands(const std::vector<BlackFlameCommand>& commands)
        {
            if (commands.empty())
                return {};

            const BlackFlameCommand& cmd = commands.front();
            switch (cmd.Type)
            {
            case BlackFlameCommandType::CreateEntity:
                return "create " + (cmd.StringValue.empty() ? std::string("an object") : cmd.StringValue);
            case BlackFlameCommandType::SetMaterialProperty:
                if (cmd.StringValue == "BaseColor")
                    return "change the base color";
                if (!cmd.StringValue.empty())
                    return "adjust " + cmd.StringValue;
                return "adjust the material";
            case BlackFlameCommandType::CreateLight:
                return "create a light";
            case BlackFlameCommandType::ResetAllocatorSafely:
                return "safely reset the allocator";
            case BlackFlameCommandType::Unknown:
            default:
                return {};
            }
        }

        struct PromptScoreFactors
        {
            float Base = 0.0f;
            float Context = 0.0f;
            float Recency = 0.0f;
            float Severity = 0.0f;
            float SuggestionBoost = 0.0f;
        };

        static float ComputeScore(const PromptScoreFactors& factors)
        {
            return std::clamp(factors.Base + factors.Context + factors.Recency + factors.Severity + factors.SuggestionBoost, 0.0f, 100.0f);
        }

        static int CountRecentMemoryMatches(const BlackFlameDebugContext& ctx, const std::string& token)
        {
            if (token.empty())
                return 0;

            std::string loweredToken = token;
            std::transform(loweredToken.begin(), loweredToken.end(), loweredToken.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            int matches = 0;
            for (const std::string& memory : ctx.RecentMemories)
            {
                std::string loweredMemory = memory;
                std::transform(loweredMemory.begin(), loweredMemory.end(), loweredMemory.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (loweredMemory.find(loweredToken) != std::string::npos)
                    ++matches;
            }
            return matches;
        }

        static BlackFlamePromptEntry FromSuggestion(const BlackFlameSuggestion& suggestion, size_t suggestionIndex)
        {
            BlackFlamePromptEntry entry{};
            entry.Label = suggestion.Message;
            entry.Prompt = InferPromptFromCommands(suggestion.ProposedCommands);
            if (entry.Prompt.empty())
                entry.Prompt = suggestion.Message;
            entry.IsRecommended = true;
            entry.Score = 100.0f;
            entry.SuggestionIndex = suggestionIndex;
            entry.ProposedCommands = suggestion.ProposedCommands;
            return entry;
        }

        static std::vector<BlackFlamePromptEntry> BuildDynamicPrompts(const BlackFlameDebugContext& ctx, const Material* selectedMaterial, BlackFlameMode mode, const std::vector<BlackFlameSuggestion>& suggestions)
        {
            std::vector<BlackFlamePromptEntry> prompts;
            auto addPrompt = [&](std::string label, std::string prompt, const PromptScoreFactors& factors)
            {
                BlackFlamePromptEntry entry{};
                entry.Label = std::move(label);
                entry.Prompt = std::move(prompt);
                entry.Score = ComputeScore(factors);
                entry.IsRecommended = entry.Score >= 80.0f;
                prompts.push_back(std::move(entry));
            };

            for (size_t i = 0; i < suggestions.size(); ++i)
                prompts.push_back(FromSuggestion(suggestions[i], i));

            const bool recentCreate = !ctx.LastAction.empty() && ctx.LastAction.find("Created ") != std::string::npos;
            const int roughnessMentions = CountRecentMemoryMatches(ctx, "Roughness");

            if (!ctx.HasSelection)
            {
                addPrompt("Create Cube", "create a cube", { 60.0f, 8.0f, recentCreate ? 10.0f : 0.0f, 10.0f, 0.0f });
                addPrompt("Add Light", "create a light", { 56.0f, (ctx.LightCount == 0) ? 16.0f : 4.0f, 0.0f, (ctx.LightCount == 0) ? 14.0f : 0.0f, 0.0f });
                addPrompt("Start Building", mode == BlackFlameMode::Conversation ? "what should i build first" : "start building a scene", { 42.0f, 6.0f, 0.0f, 0.0f, 0.0f });
                addPrompt("What should I work on?", "what should i work on next", { 38.0f, 4.0f, 6.0f, 0.0f, 0.0f });
            }
            else
            {
                addPrompt("Make it red", "make it red", { 38.0f, 18.0f + (ctx.HasMaterial ? 10.0f : 0.0f), 0.0f, 0.0f, 0.0f });
                addPrompt("Make it metallic", "make it metallic", { 36.0f, 16.0f + ((ctx.HasMaterial && ctx.Metallic < 0.1f) ? 10.0f : 0.0f), 0.0f, 0.0f, 0.0f });
                addPrompt("Improve this", mode == BlackFlameMode::Conversation ? "how can this look better" : "improve this", { 32.0f, 16.0f, 0.0f, 0.0f, 0.0f });

                if (ctx.HasMaterial)
                    addPrompt("Adjust material", mode == BlackFlameMode::Conversation ? "what material changes would help" : "adjust the material", { 28.0f, 20.0f, 0.0f, 0.0f, 0.0f });
                else
                    addPrompt("Add material", "add a material", { 30.0f, 10.0f, 0.0f, 50.0f, 0.0f });

                if (selectedMaterial)
                {
                    if (ctx.Roughness < 0.10f)
                        addPrompt("Fix overly shiny surface", mode == BlackFlameMode::Conversation ? "why is this so reflective" : "make this less shiny", { 26.0f, 20.0f, (roughnessMentions > 2) ? 15.0f : 0.0f, 40.0f, 0.0f });
                    else if (ctx.Roughness < 0.20f)
                        addPrompt("Soften reflections", "make it less shiny", { 24.0f, 20.0f, 0.0f, 28.0f, 0.0f });
                    else if (ctx.Roughness > 0.90f)
                        addPrompt("Make it glossier", mode == BlackFlameMode::Conversation ? "why does this look flat" : "reduce roughness slightly", { 24.0f, 20.0f, 0.0f, 20.0f, 0.0f });
                    else if (ctx.Roughness > 0.75f)
                        addPrompt("Make it glossier", mode == BlackFlameMode::Conversation ? "why does this look flat" : "reduce roughness slightly", { 24.0f, 16.0f, 0.0f, 10.0f, 0.0f });

                    if (ctx.Metallic < 0.1f)
                        addPrompt("Push metallic look", mode == BlackFlameMode::Conversation ? "how would metallic change this" : "make it metallic", { 26.0f, 20.0f, 0.0f, 10.0f, 0.0f });

                    const float brightness = (selectedMaterial->baseColor.x + selectedMaterial->baseColor.y + selectedMaterial->baseColor.z) / 3.0f;
                    if (brightness > 0.75f)
                        addPrompt("Tone down brightness", "make this darker", { 28.0f, 16.0f, 0.0f, 32.0f, 0.0f });
                    else if (brightness < 0.25f)
                        addPrompt("Brighten it up", mode == BlackFlameMode::Conversation ? "how could this read better" : "make this brighter", { 28.0f, 16.0f, 0.0f, 26.0f, 0.0f });
                }
                else
                {
                    addPrompt("What is missing here?", "what is missing from this object", { 30.0f, 14.0f, 0.0f, 0.0f, 0.0f });
                }
            }

            if (recentCreate)
                addPrompt("Create another one", "make another one", { 44.0f, 10.0f, 20.0f, 8.0f, 0.0f });
            if (!ctx.LastAction.empty() && ctx.LastAction.find("material") != std::string::npos)
                addPrompt("Refine material again", "refine this material", { 34.0f, 12.0f, 15.0f, 0.0f, 0.0f });
            if (ctx.LightCount > 3)
                addPrompt("Reduce lighting intensity", "make the scene less bright", { 30.0f, 8.0f, 0.0f, 30.0f, 0.0f });
            else if (!ctx.LastAction.empty() && ctx.LastAction.find("light") != std::string::npos)
                addPrompt("Reduce lighting intensity", "make the scene less bright", { 32.0f, 8.0f, 12.0f, 18.0f, 0.0f });

            std::unordered_set<std::string> seenKeys;
            std::vector<BlackFlamePromptEntry> deduped;
            deduped.reserve(prompts.size());
            for (const auto& entry : prompts)
            {
                const std::string key = !entry.Prompt.empty() ? entry.Prompt : entry.Label;
                if (key.empty())
                    continue;
                if (!seenKeys.insert(key).second)
                    continue;
                deduped.push_back(entry);
            }

            std::stable_sort(deduped.begin(), deduped.end(), [](const BlackFlamePromptEntry& a, const BlackFlamePromptEntry& b)
            {
                if (a.Score != b.Score)
                    return a.Score > b.Score;
                if (a.IsRecommended != b.IsRecommended)
                    return a.IsRecommended > b.IsRecommended;
                return a.Label < b.Label;
            });

            for (auto& entry : deduped)
                entry.IsRecommended = entry.Score >= 80.0f;

            if (deduped.size() > 6)
                deduped.resize(6);

            return deduped;
        }

        static float GetPromptConfidence(const BlackFlamePromptEntry& entry)
        {
            return std::clamp(entry.Score / 100.0f, 0.0f, 1.0f);
        }

        static int GetPromptConfidenceStars(float confidence)
        {
            if (confidence > 0.85f)
                return 3;
            if (confidence > 0.60f)
                return 2;
            return 1;
        }

        static std::string GetPromptConfidenceStarsText(float confidence)
        {
            const int stars = GetPromptConfidenceStars(confidence);
            return stars > 0 ? std::string((size_t)stars, '*') : std::string("-");
        }

        static bool DrawDynamicPromptButton(const BlackFlamePromptEntry& entry)
        {
            const float confidence = GetPromptConfidence(entry);
            const int stars = GetPromptConfidenceStars(confidence);
            const std::string starText = stars > 0 ? std::string((size_t)stars, '*') : std::string("-");

            const ImVec4 recommendedColor = ImVec4(0.52f, 0.24f, 0.08f, 1.0f);
            const ImVec4 normalColor = ImVec4(0.26f, 0.20f, 0.18f, 1.0f);
            const ImVec4 hoverColor = entry.IsRecommended ? ImVec4(0.68f, 0.32f, 0.10f, 1.0f) : ImVec4(0.38f, 0.28f, 0.24f, 1.0f);
            const ImVec4 activeColor = entry.IsRecommended ? ImVec4(0.78f, 0.40f, 0.12f, 1.0f) : ImVec4(0.48f, 0.34f, 0.28f, 1.0f);

            ImGui::PushID(entry.Label.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, entry.IsRecommended ? recommendedColor : normalColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            const bool pressed = ImGui::Button(entry.Label.c_str(), ImVec2(-1.0f, 0.0f));
            ImGui::PopStyleColor(3);

            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.22f, 1.0f), "Confidence: %s", starText.c_str());
            ImGui::ProgressBar(confidence, ImVec2(-1.0f, 4.0f), "");
            ImGui::Spacing();
            ImGui::PopID();
            return pressed;
        }

        static void SetBlackFlamePromptText(const std::string& prompt)
        {
            ::strncpy_s(g_BlackFlamePromptBuffer, prompt.c_str(), sizeof(g_BlackFlamePromptBuffer) - 1u);
            g_BlackFlamePromptBuffer[sizeof(g_BlackFlamePromptBuffer) - 1u] = '\0';
            g_BlackFlameConfirmApply = false;
        }

        static void InvokeBlackFlamePrompt(BlackFlameAI& ai, const std::string& prompt)
        {
            if (prompt.empty())
                return;

            SetBlackFlamePromptText(prompt);
            ai.SubmitPrompt(prompt);
            BlackFlame().open = true;
        }

        static const char* BlackFlamePrimitiveLabel(ScenePrimitive primitive)
        {
            switch (primitive)
            {
            case ScenePrimitive::Sphere: return "Sphere";
            case ScenePrimitive::Plane: return "Plane";
            case ScenePrimitive::Cylinder: return "Cylinder";
            case ScenePrimitive::Cube:
            default:
                return "Cube";
            }
        }

        static const char* BlackFlameAccessLevelLabel(BlackFlameAccessLevel access)
        {
            return access == BlackFlameAccessLevel::Admin ? "Admin" : "User";
        }

        static const char* BlackFlameCommandTypeLabel(BlackFlameCommandType type)
        {
            switch (type)
            {
            case BlackFlameCommandType::ResetAllocatorSafely: return "ResetAllocatorSafely";
            case BlackFlameCommandType::CreateEntity: return "CreateEntity";
            case BlackFlameCommandType::SetMaterialProperty: return "SetMaterialProperty";
            case BlackFlameCommandType::CreateLight: return "CreateLight";
            case BlackFlameCommandType::Unknown:
            default:
                return "Unknown";
            }
        }

        static const char* BlackFlameStateLabel(BlackFlameState state)
        {
            switch (state)
            {
            case BlackFlameState::Thinking: return "Thinking";
            case BlackFlameState::Ready: return "Ready";
            case BlackFlameState::Executing: return "Executing";
            case BlackFlameState::Error: return "Error";
            case BlackFlameState::Idle:
            default:
                return "Idle";
            }
        }

        static ImVec4 BlackFlameStateColor(BlackFlameState state)
        {
            switch (state)
            {
            case BlackFlameState::Thinking: return ImVec4(1.00f, 0.52f, 0.18f, 1.0f);
            case BlackFlameState::Ready: return ImVec4(1.00f, 0.68f, 0.24f, 1.0f);
            case BlackFlameState::Executing: return ImVec4(1.00f, 0.28f, 0.12f, 1.0f);
            case BlackFlameState::Error: return ImVec4(0.68f, 0.10f, 0.12f, 1.0f);
            case BlackFlameState::Idle:
            default:
                return ImVec4(0.78f, 0.30f, 0.16f, 1.0f);
            }
        }

        static std::vector<std::filesystem::path> EnumerateAlbedoTextureAssets()
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
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
                    result.push_back(entry.path());
            }

            std::sort(result.begin(), result.end());
            return result;
        }

        static std::vector<std::filesystem::path> EnumeratePrefabAssets();

        struct PrefabLibraryEntry
        {
            std::filesystem::path Path;
            std::string Label;
            std::string Category;
            std::vector<std::string> Tags;
            std::string BasePrefabPath;
            std::string BasePrefabLabel;
            bool MissingBasePrefab = false;
            bool HasCircularInheritance = false;
            std::string SearchText;
            std::filesystem::file_time_type LastWriteTime{};
            uintmax_t FileSize = 0;
            size_t InstanceCount = 0;
            size_t RootCount = 0;
            size_t CameraCount = 0;
            std::string PrimitiveSummary;
        };

        static std::string SanitizePrefabAssetName(std::string name)
        {
            for (char& c : name)
            {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
                    c = '_';
            }
            if (name.empty())
                name = "Prefab";
            return name;
        }

        static const char* PrefabSortModeLabel(PrefabLibrarySortMode mode)
        {
            switch (mode)
            {
            case PrefabLibrarySortMode::NameDesc: return "Name (Z-A)";
            case PrefabLibrarySortMode::InstancesDesc: return "Most Instances";
            case PrefabLibrarySortMode::NewestFirst: return "Newest";
            case PrefabLibrarySortMode::NameAsc:
            default: return "Name (A-Z)";
            }
        }

        static const char* PrefabPrimitiveLabel(ScenePrimitive primitive)
        {
            switch (primitive)
            {
            case ScenePrimitive::Sphere: return "Sphere";
            case ScenePrimitive::Plane: return "Plane";
            case ScenePrimitive::Cylinder: return "Cylinder";
            case ScenePrimitive::Empty: return "Empty";
            case ScenePrimitive::Capsule: return "Capsule";
            case ScenePrimitive::Torus: return "Torus";
            case ScenePrimitive::Cone: return "Cone";
            case ScenePrimitive::Cube:
            default: return "Cube";
            }
        }

        static std::string FormatPrefabFileSize(uintmax_t fileSize)
        {
            if (fileSize >= 1024ull * 1024ull)
                return std::format("{:.1f} MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));
            if (fileSize >= 1024ull)
                return std::format("{:.1f} KB", static_cast<double>(fileSize) / 1024.0);
            return std::format("{} B", fileSize);
        }

        static std::string BuildPrefabPrimitiveSummary(const std::vector<SceneInstance>& instances)
        {
            struct PrimitiveCount
            {
                ScenePrimitive Primitive = ScenePrimitive::Cube;
                size_t Count = 0;
            };

            std::vector<PrimitiveCount> counts;
            for (const SceneInstance& instance : instances)
            {
                auto it = std::find_if(counts.begin(), counts.end(), [&](const PrimitiveCount& count)
                {
                    return count.Primitive == instance.primitive;
                });
                if (it == counts.end())
                    counts.push_back({ instance.primitive, 1u });
                else
                    ++it->Count;
            }

            std::sort(counts.begin(), counts.end(), [](const PrimitiveCount& a, const PrimitiveCount& b)
            {
                if (a.Count != b.Count)
                    return a.Count > b.Count;
                return static_cast<int>(a.Primitive) < static_cast<int>(b.Primitive);
            });

            std::string summary;
            const size_t limit = (std::min)(counts.size(), size_t(3));
            for (size_t i = 0; i < limit; ++i)
            {
                if (!summary.empty())
                    summary += ", ";
                summary += std::format("{} {}", counts[i].Count, PrefabPrimitiveLabel(counts[i].Primitive));
            }
            if (counts.size() > limit)
                summary += std::format(" +{} more", counts.size() - limit);
            return summary.empty() ? std::string("No primitives") : summary;
        }

        static std::vector<std::string> BuildPrefabLibraryCategories(const std::vector<PrefabLibraryEntry>& entries)
        {
            std::vector<std::string> categories;
            categories.push_back("All Categories");
            for (const PrefabLibraryEntry& entry : entries)
            {
                if (std::find(categories.begin(), categories.end(), entry.Category) == categories.end())
                    categories.push_back(entry.Category);
            }
            std::sort(categories.begin() + 1, categories.end());
            return categories;
        }

        static std::vector<std::string> BuildPrefabLibraryTags(const std::vector<PrefabLibraryEntry>& entries)
        {
            std::vector<std::string> tags;
            tags.push_back("All Tags");
            for (const PrefabLibraryEntry& entry : entries)
            {
                for (const std::string& tag : entry.Tags)
                {
                    if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                        tags.push_back(tag);
                }
            }
            std::sort(tags.begin() + 1, tags.end());
            return tags;
        }

        static std::string JoinPrefabTags(const std::vector<std::string>& tags)
        {
            std::string result;
            for (size_t i = 0; i < tags.size(); ++i)
            {
                if (i > 0)
                    result += ", ";
                result += tags[i];
            }
            return result;
        }

        static std::string ToLowerAsciiCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        static std::string FormatFloat3(const DirectX::XMFLOAT3& value)
        {
            return std::format("({:.2f}, {:.2f}, {:.2f})", value.x, value.y, value.z);
        }

        struct PrefabVariantDiffItem
        {
            std::string Label;
            std::string BaseValue;
            std::string VariantValue;
        };

        static std::vector<std::string> BuildPrefabInheritanceChain(const std::string& prefabPath, bool& outMissingBase, bool& outCircular, std::string* outIssuePath = nullptr)
        {
            outMissingBase = false;
            outCircular = false;
            if (outIssuePath)
                outIssuePath->clear();

            std::vector<std::string> chain;
            if (prefabPath.empty())
                return chain;

            std::unordered_set<std::string> visited;
            std::string currentPath = std::filesystem::path(prefabPath).lexically_normal().string();
            while (!currentPath.empty())
            {
                chain.push_back(currentPath);
                if (!visited.insert(currentPath).second)
                {
                    outCircular = true;
                    if (outIssuePath)
                        *outIssuePath = currentPath;
                    break;
                }

                std::string nextPath;
                if (!Scene::GetPrefabAssetBasePrefab(currentPath, nextPath) || nextPath.empty())
                    break;
                nextPath = std::filesystem::path(nextPath).lexically_normal().string();
                if (!std::filesystem::exists(nextPath))
                {
                    chain.push_back(nextPath);
                    outMissingBase = true;
                    if (outIssuePath)
                        *outIssuePath = nextPath;
                    break;
                }
                currentPath = nextPath;
            }

            return chain;
        }

        static std::vector<std::string> ParsePrefabTagsBuffer(const char* buffer)
        {
            std::vector<std::string> tags;
            if (!buffer || buffer[0] == '\0')
                return tags;

            std::string text = buffer;
            size_t start = 0;
            while (start < text.size())
            {
                const size_t comma = text.find(',', start);
                const size_t length = (comma == std::string::npos) ? (text.size() - start) : (comma - start);
                tags.push_back(text.substr(start, length));
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }

            std::vector<std::string> normalized;
            std::unordered_set<std::string> seen;
            for (std::string& tag : tags)
            {
                const auto first = std::find_if(tag.begin(), tag.end(), [](unsigned char c) { return !std::isspace(c); });
                const auto last = std::find_if(tag.rbegin(), tag.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
                if (first >= last)
                    continue;
                std::string cleaned(first, last);
                std::string lowered = cleaned;
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!seen.insert(lowered).second)
                    continue;
                normalized.push_back(std::move(cleaned));
            }
            return normalized;
        }

        static const SceneInstance* FindMatchingPrefabInstanceForDiff(const std::vector<SceneInstance>& instances, const SceneInstance& target)
        {
            if (target.prefabLocalInstanceId != 0)
            {
                auto it = std::find_if(instances.begin(), instances.end(), [&](const SceneInstance& instance)
                {
                    return instance.instanceId == target.prefabLocalInstanceId;
                });
                if (it != instances.end())
                    return &(*it);
            }

            auto it = std::find_if(instances.begin(), instances.end(), [&](const SceneInstance& instance)
            {
                return instance.name == target.name &&
                    instance.primitive == target.primitive &&
                    instance.vaultType == target.vaultType;
            });
            if (it != instances.end())
                return &(*it);

            if (instances.size() == 1)
                return &instances.front();
            return nullptr;
        }

        static std::vector<PrefabVariantDiffItem> BuildVariantDiffItems(const SceneInstance& variantInstance, const std::string& basePrefabPath)
        {
            std::vector<PrefabVariantDiffItem> diffs;
            if (basePrefabPath.empty())
                return diffs;

            std::vector<SceneInstance> baseInstances;
            if (!Scene::LoadPrefabAssetInstances(basePrefabPath, baseInstances) || baseInstances.empty())
                return diffs;

            const SceneInstance* baseInstance = FindMatchingPrefabInstanceForDiff(baseInstances, variantInstance);
            if (!baseInstance)
                return diffs;

            auto addDiff = [&](const char* label, const std::string& baseValue, const std::string& variantValue)
            {
                if (baseValue == variantValue)
                    return;
                diffs.push_back({ label, baseValue, variantValue });
            };

            addDiff("Name", baseInstance->name, variantInstance.name);
            addDiff("Position", FormatFloat3(baseInstance->position), FormatFloat3(variantInstance.position));
            addDiff("Rotation", FormatFloat3(baseInstance->rotation), FormatFloat3(variantInstance.rotation));
            addDiff("Scale", FormatFloat3(baseInstance->scale), FormatFloat3(variantInstance.scale));
            addDiff("Visible", baseInstance->visible ? "true" : "false", variantInstance.visible ? "true" : "false");
            addDiff("Material", std::to_string(baseInstance->materialIndex), std::to_string(variantInstance.materialIndex));
            addDiff("Vault Type", std::to_string(static_cast<int>(baseInstance->vaultType)), std::to_string(static_cast<int>(variantInstance.vaultType)));
            return diffs;
        }

        static std::vector<const PrefabLibraryEntry*> BuildPrefabRepairSuggestions(const std::string& missingBasePath, const std::vector<PrefabLibraryEntry>& entries)
        {
            std::vector<std::pair<int, const PrefabLibraryEntry*>> scored;
            const std::string missingLabel = ToLowerAsciiCopy(std::filesystem::path(missingBasePath).stem().stem().string());
            for (const PrefabLibraryEntry& entry : entries)
            {
                if (entry.MissingBasePrefab || entry.HasCircularInheritance)
                    continue;

                int score = 0;
                const std::string label = ToLowerAsciiCopy(entry.Label);
                if (!missingLabel.empty() && label == missingLabel)
                    score += 100;
                else if (!missingLabel.empty() && label.find(missingLabel) != std::string::npos)
                    score += 60;
                else if (!missingLabel.empty() && missingLabel.find(label) != std::string::npos)
                    score += 40;

                if (score > 0)
                    scored.push_back({ score, &entry });
            }

            std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b)
            {
                if (a.first != b.first)
                    return a.first > b.first;
                return a.second->Label < b.second->Label;
            });

            std::vector<const PrefabLibraryEntry*> result;
            const size_t limit = (std::min)(scored.size(), size_t(3));
            for (size_t i = 0; i < limit; ++i)
                result.push_back(scored[i].second);
            return result;
        }

        static std::vector<PrefabLibraryEntry> BuildPrefabLibraryEntries(bool applyFilters = true)
        {
            std::vector<PrefabLibraryEntry> entries;
            for (const auto& prefabPath : EnumeratePrefabAssets())
            {
                PrefabLibraryEntry entry{};
                entry.Path = prefabPath;
                entry.Label = prefabPath.stem().stem().string();
                Scene::GetPrefabAssetCategory(prefabPath.string(), entry.Category);
                Scene::GetPrefabAssetTags(prefabPath.string(), entry.Tags);
                Scene::GetPrefabAssetBasePrefab(prefabPath.string(), entry.BasePrefabPath);
                if (!entry.BasePrefabPath.empty())
                {
                    entry.BasePrefabLabel = std::filesystem::path(entry.BasePrefabPath).stem().stem().string();
                    entry.MissingBasePrefab = !std::filesystem::exists(entry.BasePrefabPath);
                }
                entry.HasCircularInheritance = !Scene::HasCircularPrefabInheritance(prefabPath.string());
                entry.SearchText = entry.Label + " " + prefabPath.string() + " " + entry.Category + " " + JoinPrefabTags(entry.Tags) + " " + entry.BasePrefabLabel;

                try
                {
                    entry.LastWriteTime = std::filesystem::last_write_time(prefabPath);
                    entry.FileSize = std::filesystem::file_size(prefabPath);
                }
                catch (...)
                {
                    entry.LastWriteTime = {};
                    entry.FileSize = 0;
                }

                std::vector<SceneInstance> instances;
                if (Scene::LoadPrefabAssetInstances(prefabPath.string(), instances))
                {
                    entry.InstanceCount = instances.size();
                    entry.RootCount = std::count_if(instances.begin(), instances.end(), [](const SceneInstance& instance)
                    {
                        return instance.parentInstanceId == 0;
                    });
                    entry.CameraCount = std::count_if(instances.begin(), instances.end(), [](const SceneInstance& instance)
                    {
                        return instance.camera.enabled;
                    });
                    entry.PrimitiveSummary = BuildPrefabPrimitiveSummary(instances);
                }
                else
                {
                    entry.PrimitiveSummary = "Unreadable prefab";
                }

                entries.push_back(std::move(entry));
            }

            if (!applyFilters)
                return entries;

            std::string filter = g_PrefabLibrarySearchBuffer;
            std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!filter.empty())
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](PrefabLibraryEntry& entry)
                {
                    std::string haystack = entry.SearchText;
                    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    return haystack.find(filter) == std::string::npos;
                }), entries.end());
            }

            if (!g_PrefabLibraryCategoryFilter.empty() && g_PrefabLibraryCategoryFilter != "All Categories")
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), [](const PrefabLibraryEntry& entry)
                {
                    return entry.Category != g_PrefabLibraryCategoryFilter;
                }), entries.end());
            }

            if (g_PrefabLibraryTypeFilter != PrefabLibraryTypeFilter::All)
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), [](const PrefabLibraryEntry& entry)
                {
                    const bool isVariant = !entry.BasePrefabPath.empty();
                    return g_PrefabLibraryTypeFilter == PrefabLibraryTypeFilter::BaseOnly ? isVariant : !isVariant;
                }), entries.end());
            }

            if (!g_PrefabLibraryTagFilter.empty() && g_PrefabLibraryTagFilter != "All Tags")
            {
                entries.erase(std::remove_if(entries.begin(), entries.end(), [](const PrefabLibraryEntry& entry)
                {
                    return std::find(entry.Tags.begin(), entry.Tags.end(), g_PrefabLibraryTagFilter) == entry.Tags.end();
                }), entries.end());
            }

            std::sort(entries.begin(), entries.end(), [](const PrefabLibraryEntry& a, const PrefabLibraryEntry& b)
            {
                switch (g_PrefabLibrarySortMode)
                {
                case PrefabLibrarySortMode::NameDesc:
                    return a.Label > b.Label;
                case PrefabLibrarySortMode::InstancesDesc:
                    if (a.InstanceCount != b.InstanceCount)
                        return a.InstanceCount > b.InstanceCount;
                    return a.Label < b.Label;
                case PrefabLibrarySortMode::NewestFirst:
                    if (a.LastWriteTime != b.LastWriteTime)
                        return a.LastWriteTime > b.LastWriteTime;
                    return a.Label < b.Label;
                case PrefabLibrarySortMode::NameAsc:
                default:
                    return a.Label < b.Label;
                }
            });

            return entries;
        }

        static void TryCopyPrefabThumbnailCompanion(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath)
        {
            const std::filesystem::path sourceStem = sourcePath.stem().stem();
            const std::filesystem::path targetStem = targetPath.stem().stem();
            const char* exts[] = { ".png", ".jpg", ".jpeg" };
            for (const char* ext : exts)
            {
                const std::filesystem::path sourceThumb = sourcePath.parent_path() / (sourceStem.string() + ext);
                if (!std::filesystem::exists(sourceThumb))
                    continue;
                const std::filesystem::path targetThumb = targetPath.parent_path() / (targetStem.string() + ext);
                std::error_code ec;
                std::filesystem::copy_file(sourceThumb, targetThumb, std::filesystem::copy_options::overwrite_existing, ec);
                break;
            }
        }

        static void TryRenamePrefabThumbnailCompanion(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath)
        {
            const std::filesystem::path sourceStem = sourcePath.stem().stem();
            const std::filesystem::path targetStem = targetPath.stem().stem();
            const char* exts[] = { ".png", ".jpg", ".jpeg" };
            for (const char* ext : exts)
            {
                const std::filesystem::path sourceThumb = sourcePath.parent_path() / (sourceStem.string() + ext);
                if (!std::filesystem::exists(sourceThumb))
                    continue;
                const std::filesystem::path targetThumb = targetPath.parent_path() / (targetStem.string() + ext);
                std::error_code ec;
                std::filesystem::rename(sourceThumb, targetThumb, ec);
                break;
            }
        }

        static bool DuplicatePrefabAsset(const std::filesystem::path& sourcePath, std::filesystem::path& outPath)
        {
            if (!std::filesystem::exists(sourcePath))
                return false;

            const std::string baseName = sourcePath.stem().stem().string();
            for (int suffix = 1; suffix <= 128; ++suffix)
            {
                const std::string candidateName = suffix == 1
                    ? std::format("{}_Copy", baseName)
                    : std::format("{}_Copy{}", baseName, suffix);
                const std::filesystem::path candidatePath = sourcePath.parent_path() / (candidateName + ".prefab.json");
                if (std::filesystem::exists(candidatePath))
                    continue;

                std::error_code ec;
                std::filesystem::copy_file(sourcePath, candidatePath, std::filesystem::copy_options::none, ec);
                if (ec)
                    return false;
                TryCopyPrefabThumbnailCompanion(sourcePath, candidatePath);
                outPath = candidatePath;
                return true;
            }

            return false;
        }

        static bool RenamePrefabAsset(const std::filesystem::path& sourcePath, const std::string& requestedName, std::filesystem::path& outPath)
        {
            const std::string sanitizedName = SanitizePrefabAssetName(requestedName);
            const std::filesystem::path targetPath = sourcePath.parent_path() / (sanitizedName + ".prefab.json");
            if (targetPath == sourcePath || std::filesystem::exists(targetPath))
                return false;

            std::error_code ec;
            std::filesystem::rename(sourcePath, targetPath, ec);
            if (ec)
                return false;

            TryRenamePrefabThumbnailCompanion(sourcePath, targetPath);
            outPath = targetPath;
            return true;
        }

        static bool DeletePrefabAsset(const std::filesystem::path& path)
        {
            std::error_code ec;
            return std::filesystem::remove(path, ec) && !ec;
        }

        static std::vector<std::filesystem::path> EnumeratePrefabAssets()
        {
            std::vector<std::filesystem::path> result;
            const std::filesystem::path prefabDir = std::filesystem::path("Assets") / "Prefabs";
            if (!std::filesystem::exists(prefabDir))
                return result;

            for (const auto& entry : std::filesystem::directory_iterator(prefabDir))
            {
                if (!entry.is_regular_file())
                    continue;
                const std::string filename = entry.path().filename().string();
                if (filename.size() >= 12 && filename.find(".prefab.json") != std::string::npos)
                    result.push_back(entry.path());
            }

            std::sort(result.begin(), result.end());
            return result;
        }

        static Texture* GetBlackFlameCardTexture()
        {
            static Texture* s_blackFlameTexture = nullptr;
            static bool s_attemptedLoad = false;
            if (!s_attemptedLoad)
            {
                s_attemptedLoad = true;
                s_blackFlameTexture = TextureManager::GetInstance().LoadTexture("Assets/Icons/The_Fletch_Zone_Icon.png");
                if (!s_blackFlameTexture)
                    Logger::Log(LogLevel::Warning, "[BlackFlame] Failed to load flame card texture. Falling back to procedural avatar.", "BlackFlame");
            }
            return s_blackFlameTexture;
        }

        static void DrawBlackFlameAvatar(BlackFlameState state, ImVec2 size)
        {
            const ImVec2 clampedSize((std::max)(size.x, 120.0f), (std::max)(size.y, 120.0f));
            ImGui::InvisibleButton("##BlackFlameAvatar", clampedSize);
            const bool hovered = ImGui::IsItemHovered();

            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const float time = (float)ImGui::GetTime();
            const float pulseSpeed = (state == BlackFlameState::Thinking) ? 5.0f : ((state == BlackFlameState::Executing) ? 9.0f : 2.2f);
            const float pulse = 0.5f + 0.5f * std::sin(time * pulseSpeed);
            const float hoverBoost = hovered ? 1.08f : 1.0f;
            const float radius = (std::min)(clampedSize.x, clampedSize.y) * 0.24f * hoverBoost;

            const ImVec4 stateColor = BlackFlameStateColor(state);
            const ImU32 bgColor = IM_COL32(16, 12, 16, 255);
            const ImU32 ringColor = ImGui::GetColorU32(ImVec4(stateColor.x, stateColor.y, stateColor.z, 0.35f + pulse * 0.25f));
            const ImU32 glowColor = ImGui::GetColorU32(ImVec4(stateColor.x, stateColor.y * 0.72f, stateColor.z * 0.50f, 0.18f + pulse * 0.18f));
            const ImU32 emberColor = ImGui::GetColorU32(ImVec4(1.0f, 0.82f, 0.42f, 0.95f));
            const ImU32 accentColor = ImGui::GetColorU32(ImVec4(stateColor.x, stateColor.y, stateColor.z, 0.96f));

            draw->AddRectFilled(min, max, bgColor, 14.0f);
            draw->AddRect(min, max, IM_COL32(255, 255, 255, 18), 14.0f, 0, 1.0f);
            draw->AddCircleFilled(center, radius * (1.65f + pulse * 0.18f), glowColor, 48);
            draw->AddCircle(center, radius * (1.15f + pulse * 0.08f), ringColor, 48, 2.0f);

            const ImTextureID fxTexture = Graphics::GetInstance().GetBlackFlameEffectTextureID();
            Texture* flameTexture = GetBlackFlameCardTexture();
            const bool hasFxTexture = fxTexture != (ImTextureID)0;
            const bool hasTexture = flameTexture && flameTexture->srvGPU.ptr != 0;
            if (hasFxTexture || hasTexture)
            {
                const float imageScale = hasFxTexture ? (1.96f + pulse * 0.12f) : (1.82f + pulse * 0.08f);
                const ImVec2 imageSize(radius * imageScale, radius * imageScale);
                const ImVec2 imageMin(center.x - imageSize.x * 0.5f, center.y - imageSize.y * 0.5f);
                const ImVec2 imageMax(center.x + imageSize.x * 0.5f, center.y + imageSize.y * 0.5f);
                const ImTextureID image = hasFxTexture ? fxTexture : (ImTextureID)flameTexture->srvGPU.ptr;
                const ImVec4 tint(
                    (std::min)(1.0f, stateColor.x + 0.18f),
                    (std::min)(1.0f, stateColor.y + 0.16f),
                    (std::min)(1.0f, stateColor.z + 0.12f),
                    0.80f + pulse * 0.14f);
                draw->AddImage(image, imageMin, imageMax, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(tint));
                draw->AddRect(imageMin, imageMax, ImGui::GetColorU32(ImVec4(stateColor.x, stateColor.y, stateColor.z, 0.14f + pulse * 0.06f)), 8.0f, 0, 1.0f);
                return;
            }

            ImVec2 flame[7] =
            {
                ImVec2(center.x, center.y - radius * 1.36f),
                ImVec2(center.x + radius * 0.46f, center.y - radius * 0.56f),
                ImVec2(center.x + radius * 0.28f, center.y + radius * 0.18f),
                ImVec2(center.x + radius * 0.56f, center.y + radius * 0.72f),
                ImVec2(center.x, center.y + radius * 1.18f),
                ImVec2(center.x - radius * 0.56f, center.y + radius * 0.72f),
                ImVec2(center.x - radius * 0.28f, center.y + radius * 0.18f)
            };
            draw->AddConvexPolyFilled(flame, IM_ARRAYSIZE(flame), accentColor);

            ImVec2 innerFlame[5] =
            {
                ImVec2(center.x, center.y - radius * 0.84f),
                ImVec2(center.x + radius * 0.22f, center.y - radius * 0.18f),
                ImVec2(center.x + radius * 0.12f, center.y + radius * 0.40f),
                ImVec2(center.x, center.y + radius * 0.78f),
                ImVec2(center.x - radius * 0.12f, center.y + radius * 0.40f)
            };
            draw->AddConvexPolyFilled(innerFlame, IM_ARRAYSIZE(innerFlame), emberColor);
        }

        static void DrawBlackFlameSectionTitle(const char* title, const ImVec4& color = ImVec4(1.0f, 0.62f, 0.20f, 1.0f))
        {
            ImGui::Spacing();
            ImGui::TextColored(color, "%s", title);
            ImGui::Separator();
        }

        static const char* BlackFlameModeTagline(BlackFlameMode mode)
        {
            switch (mode)
            {
            case BlackFlameMode::Conversation: return "Speak freely with the flame...";
            case BlackFlameMode::Hybrid: return "Ask the flame to explain, propose, and await your command...";
            case BlackFlameMode::Engine:
            default: return "Whisper a precise instruction to the flame...";
            }
        }

        struct BlackFlameExecutionContext
        {
            EditorState* Editor = nullptr;
            Graphics* GraphicsSystem = nullptr;
        };

        struct BlackFlameApplyResult
        {
            bool success = false;
            bool accessDenied = false;
        };

        static bool ExecuteBlackFlameCommand(const BlackFlameCommand& cmd, BlackFlameAccessLevel currentAccess, BlackFlameExecutionContext& context, bool& accessDenied)
        {
            accessDenied = false;
            if ((int)cmd.RequiredAccess > (int)currentAccess)
            {
                accessDenied = true;
                return false;
            }

            switch (cmd.Type)
            {
            case BlackFlameCommandType::ResetAllocatorSafely:
                Logger::Log(LogLevel::Info, "[BlackFlame] ResetAllocatorSafely remains staged/log-only.", "BlackFlame");
                return true;
            case BlackFlameCommandType::CreateEntity:
                if (!context.Editor)
                    return false;
                PushUndoSnapshot(*context.Editor);
                if (cmd.StringValue == "Empty" || cmd.StringValue == "Cube") { Scene::CreateCube(); return true; }
                if (cmd.StringValue == "Sphere") { Scene::CreateSphere(); return true; }
                if (cmd.StringValue == "Plane") { Scene::CreatePlane(); return true; }
                return false;
            case BlackFlameCommandType::SetMaterialProperty:
            {
                SceneInstance* selected = Scene::GetSelectedInstance();
                if (!selected)
                    return false;
                Material* material = MaterialManager::GetInstance().GetMaterialByIndex(selected->materialIndex);
                if (!material)
                    return false;
                if (cmd.StringValue == "Metallic") material->SetFloat("metallic", std::clamp(cmd.FloatValue, 0.0f, 1.0f));
                else if (cmd.StringValue == "Roughness") material->SetFloat("roughness", std::clamp(cmd.FloatValue, 0.0f, 1.0f));
                else if (cmd.StringValue == "BaseColor") material->SetBaseColor({ std::clamp(cmd.FloatValue, 0.0f, 1.0f), std::clamp(cmd.FloatValue2, 0.0f, 1.0f), std::clamp(cmd.FloatValue3, 0.0f, 1.0f) });
                else if (cmd.StringValue == "NormalFlipY")
                {
                    if (!context.GraphicsSystem)
                        return false;
                    context.GraphicsSystem->GetFlipNormalGreenChannel() = (cmd.FloatValue >= 0.5f);
                }
                else return false;

                if (context.Editor)
                {
                    SceneEvent evt{};
                    evt.Type = SceneEventType::MaterialChanged;
                    evt.Entity = selected;
                    evt.Material = material;
                    evt.ColorBias = material->baseColor;
                    evt.EventStrength = 0.8f;
                    context.Editor->sceneEvents.Emit(evt);
                }
                return true;
            }
            case BlackFlameCommandType::CreateLight:
                if (!context.GraphicsSystem)
                    return false;
                if (cmd.StringValue == "Directional")
                {
                    auto& light = context.GraphicsSystem->GetDirectionalLight();
                    light.intensity = cmd.FloatValue > 0.0f ? cmd.FloatValue : 1.25f;
                    return true;
                }
                return false;
            case BlackFlameCommandType::Unknown:
            default:
                return false;
            }
        }

        static BlackFlameApplyResult ApplyBlackFlameChange(const BlackFlameResponse& response, BlackFlameAccessLevel access, BlackFlameExecutionContext& context)
        {
            BlackFlameApplyResult result{};
            bool anyExecuted = false;
            bool hadFailure = false;
            for (const BlackFlameCommand& cmd : response.Commands)
            {
                if (!BlackFlameAI::ValidateCommand(cmd))
                {
                    hadFailure = true;
                    continue;
                }
                bool accessDenied = false;
                const bool executed = ExecuteBlackFlameCommand(cmd, access, context, accessDenied);
                anyExecuted = anyExecuted || executed;
                result.accessDenied = result.accessDenied || accessDenied;
                hadFailure = hadFailure || (!executed && !accessDenied);
            }
            result.success = anyExecuted && !hadFailure && !result.accessDenied;
            return result;
        }

        static EditorPanel g_materialPreview{
            "Material Preview",
            false,
            []()
            {
                auto& panel = MaterialPreview();
                if (!panel.open)
                    return;

                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.06f, 0.98f));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.09f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.42f, 0.16f, 0.10f, 0.55f));
                if (ImGui::Begin(panel.name, &panel.open))
                {
                    DrawBlackFlameAvatar(BlackFlameState::Ready, ImVec2(ImGui::GetContentRegionAvail().x, 124.0f));
                    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.20f, 1.0f), "Material Vision");
                    ImGui::TextWrapped("A manual preview of the selected surface, framed in the language of the flame.");

                    EditorState* editor = g_engineInstance ? &g_engineInstance->GetEditorState() : nullptr;
                    SceneInstance* selected = Scene::GetSelectedInstance();
                    Material* material = nullptr;
                    const char* previewSourceLabel = nullptr;
                    if (selected)
                    {
                        material = MaterialManager::GetInstance().GetMaterialByIndex(selected->materialIndex);
                        previewSourceLabel = "Selection";
                    }
                    else if (editor && editor->focusedMaterialIndex >= 0)
                    {
                        material = MaterialManager::GetInstance().GetMaterialByIndex(editor->focusedMaterialIndex);
                        previewSourceLabel = "Focused Material Asset";
                    }

                    if (!material)
                    {
                        DrawBlackFlameSectionTitle("Preview");
                        ImGui::TextUnformatted("Select an object or focus a material asset to preview its material.");
                    }
                    else
                    {
                        static constexpr float kTwoPi = 6.28318530718f;
                        g_MaterialPreviewQuality = std::clamp(g_MaterialPreviewQuality, 24, 96);
                        g_MaterialPreviewSize = std::clamp(g_MaterialPreviewSize, 160.0f, 360.0f);
                        if (g_MaterialPreviewAutoRotate)
                        {
                            g_MaterialPreviewRotation = std::fmod(g_MaterialPreviewRotation + ImGui::GetIO().DeltaTime * 0.9f, kTwoPi);
                            if (g_MaterialPreviewRotation < 0.0f)
                                g_MaterialPreviewRotation += kTwoPi;
                        }

                        DrawBlackFlameSectionTitle(previewSourceLabel, ImVec4(0.95f, 0.72f, 0.34f, 1.0f));
                        if (selected)
                            ImGui::Text("Object: %s", selected->name.empty() ? "(unnamed)" : selected->name.c_str());
                        ImGui::Text("Material: %s", material->name.c_str());
                        if (!selected && editor && editor->focusedMaterialIndex >= 0 && ImGui::SmallButton("Clear Material Focus"))
                            editor->focusedMaterialIndex = -1;

                        DrawBlackFlameSectionTitle("Preview Controls", ImVec4(1.0f, 0.54f, 0.24f, 1.0f));
                        ImGui::Checkbox("Auto Rotate", &g_MaterialPreviewAutoRotate);
                        DrawPreviewTickFloat("Size", g_MaterialPreviewSize, 160.0f, 360.0f, 10.0f, "%.0f");
                        DrawPreviewTickInt("Quality", g_MaterialPreviewQuality, 24, 96, 4);
                        DrawPreviewTickFloat("Rotation", g_MaterialPreviewRotation, 0.0f, 6.2831853f, 0.10f, "%.2f rad");

                        DrawBlackFlameSectionTitle("Surface Read", ImVec4(1.0f, 0.58f, 0.22f, 1.0f));
                        const bool hasAlbedo = material->albedo && !material->albedo->sourcePath.empty();
                        ImGui::Text("Albedo: %s", hasAlbedo ? std::filesystem::path(material->albedo->sourcePath).filename().string().c_str() : "<None>");
                        ImGui::Text("Metallic / Roughness: %.2f / %.2f", material->metallic, material->roughness);

                        if (hasAlbedo)
                        {
                            DrawBlackFlameSectionTitle("Live Albedo", ImVec4(1.0f, 0.48f, 0.22f, 1.0f));
                            if (material->albedo->srvGPU.ptr != 0)
                            {
                                ImGui::Image((ImTextureID)material->albedo->srvGPU.ptr, ImVec2(112.0f, 112.0f), ImVec2(0, 0), ImVec2(1, 1));
                                ImGui::SameLine();
                            }
                            ImGui::BeginGroup();
                            ImGui::TextWrapped("%s", material->albedo->sourcePath.c_str());
                            ImGui::Text("Resolution: %d x %d", material->albedo->width, material->albedo->height);
                            ImGui::EndGroup();
                        }

                        const float previewExtent = (std::min)(ImGui::GetContentRegionAvail().x, g_MaterialPreviewSize);
                        const ImVec2 canvasSize(previewExtent, g_MaterialPreviewSize);
                        ImGui::InvisibleButton("##MaterialPreviewCanvas", canvasSize);
                        ImDrawList* draw = ImGui::GetWindowDrawList();
                        const ImVec2 min = ImGui::GetItemRectMin();
                        const ImVec2 max = ImGui::GetItemRectMax();
                        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f - 6.0f);
                        const float radius = (std::min)(canvasSize.x, canvasSize.y) * 0.32f;
                        const float gloss = (1.0f - std::clamp(material->roughness, 0.0f, 1.0f));
                        const float metal = std::clamp(material->metallic, 0.0f, 1.0f);
                        const float rotation = g_MaterialPreviewRotation;
                        const float lightX = std::cos(rotation) * radius * 0.30f;
                        const float lightY = std::sin(rotation) * radius * 0.18f;
                        const ImU32 bgA = IM_COL32(10, 10, 12, 255);
                        const ImU32 bgB = IM_COL32(18, 12, 16, 255);
                        const ImU32 fill = ImGui::GetColorU32(ImVec4(material->baseColor.x, material->baseColor.y, material->baseColor.z, 1.0f));
                        const ImU32 rim = IM_COL32(255, 170, 90, 62);
                        const ImU32 frame = IM_COL32(255, 255, 255, 18);

                        draw->AddRectFilledMultiColor(min, max, bgA, bgA, bgB, bgB);
                        draw->AddRect(min, max, frame, 12.0f, 0, 1.0f);
                        for (int y = 0; y < 8; ++y)
                        {
                            for (int x = 0; x < 10; ++x)
                            {
                                const float tileW = canvasSize.x / 10.0f;
                                const float tileH = canvasSize.y / 8.0f;
                                const ImU32 tile = ((x + y) % 2 == 0) ? IM_COL32(40, 28, 24, 55) : IM_COL32(22, 18, 22, 55);
                                draw->AddRectFilled(
                                    ImVec2(min.x + x * tileW, min.y + y * tileH),
                                    ImVec2(min.x + (x + 1) * tileW, min.y + (y + 1) * tileH),
                                    tile);
                            }
                        }

                        draw->AddCircleFilled(center, radius * 1.55f, IM_COL32(255, 92, 34, 16 + (int)(gloss * 24.0f)), g_MaterialPreviewQuality);
                        draw->AddCircleFilled(center, radius, fill, g_MaterialPreviewQuality);
                        draw->AddCircle(center, radius, rim, g_MaterialPreviewQuality, 2.0f);
                        draw->AddCircleFilled(ImVec2(center.x - lightX, center.y - radius * 0.34f - lightY), radius * (0.16f + gloss * 0.18f), IM_COL32(255, 236, 214, (int)(70 + gloss * 140)), g_MaterialPreviewQuality / 2);
                        draw->AddCircleFilled(ImVec2(center.x + radius * 0.18f + lightX * 0.45f, center.y + radius * 0.22f + lightY), radius * 0.82f, IM_COL32(0, 0, 0, 26 + (int)(material->roughness * 96.0f)), g_MaterialPreviewQuality / 2);

                        const float barLeft = min.x + 14.0f;
                        const float barRight = max.x - 14.0f;
                        const float barWidth = barRight - barLeft;
                        draw->AddText(ImVec2(barLeft, max.y - 42.0f), IM_COL32(240, 222, 196, 255), "Metallic");
                        draw->AddRectFilled(ImVec2(barLeft, max.y - 26.0f), ImVec2(barLeft + barWidth * metal, max.y - 16.0f), IM_COL32(255, 170, 90, 220), 4.0f);
                        draw->AddRect(ImVec2(barLeft, max.y - 26.0f), ImVec2(barRight, max.y - 16.0f), IM_COL32(255, 255, 255, 28), 4.0f);
                        draw->AddText(ImVec2(barLeft, max.y - 14.0f), IM_COL32(240, 222, 196, 255), "Roughness");
                        draw->AddRectFilled(ImVec2(barLeft, max.y - 2.0f), ImVec2(barLeft + barWidth * std::clamp(material->roughness, 0.0f, 1.0f), max.y + 8.0f), IM_COL32(255, 110, 48, 215), 4.0f);
                        draw->AddRect(ImVec2(barLeft, max.y - 2.0f), ImVec2(barRight, max.y + 8.0f), IM_COL32(255, 255, 255, 28), 4.0f);

                        DrawBlackFlameSectionTitle("Available Albedo Textures", ImVec4(1.0f, 0.48f, 0.22f, 1.0f));
                        if (ImGui::Button("Clear Albedo Texture"))
                            MaterialManager::GetInstance().SetAlbedoTexture(material, nullptr);

                        const auto albedoTextures = EnumerateAlbedoTextureAssets();
                        static std::string s_MaterialPreviewAlbedoFilter;
                        char albedoFilterBuffer[128] = {};
                        strncpy_s(albedoFilterBuffer, s_MaterialPreviewAlbedoFilter.c_str(), sizeof(albedoFilterBuffer) - 1u);
                        if (ImGui::InputTextWithHint("##MaterialPreviewAlbedoFilter", "Filter textures...", albedoFilterBuffer, IM_ARRAYSIZE(albedoFilterBuffer)))
                            s_MaterialPreviewAlbedoFilter = albedoFilterBuffer;

                        auto matchesAlbedoFilter = [&](const std::string& textureName)
                        {
                            if (s_MaterialPreviewAlbedoFilter.empty())
                                return true;
                            std::string lowerName = textureName;
                            std::string lowerFilter = s_MaterialPreviewAlbedoFilter;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                            return lowerName.find(lowerFilter) != std::string::npos;
                        };

                        if (albedoTextures.empty())
                        {
                            ImGui::TextDisabled("No textures were found in Assets.");
                        }
                        else
                        {
                            const std::filesystem::path assetRoot = std::filesystem::path("Assets");
                            ImGui::BeginChild("##BlackFlameAlbedoList", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Border);
                            for (const auto& texturePath : albedoTextures)
                            {
                                const std::string texturePathString = texturePath.string();
                                const std::string textureLabelText = texturePath.lexically_relative(assetRoot).generic_string();
                                if (!matchesAlbedoFilter(textureLabelText))
                                    continue;
                                const bool isCurrent = material->albedo && material->albedo->sourcePath == texturePathString;
                                ImGui::PushID(texturePathString.c_str());
                                const ImTextureID previewId = GetTexturePreviewID(texturePathString);
                                if (previewId)
                                {
                                    ImGui::Image(previewId, ImVec2(40.0f, 40.0f), ImVec2(0, 0), ImVec2(1, 1));
                                    ImGui::SameLine();
                                }
                                if (ImGui::Selectable(textureLabelText.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 40.0f)))
                                    MaterialManager::GetInstance().LoadAlbedoTexture(material, texturePathString);
                                ImGui::PopID();
                            }
                            ImGui::EndChild();
                        }

                        DrawBlackFlameSectionTitle("Material Values", ImVec4(1.0f, 0.48f, 0.22f, 1.0f));
                        ImGui::Text("Base Color: %.2f %.2f %.2f", material->baseColor.x, material->baseColor.y, material->baseColor.z);
                        ImGui::Text("Metallic: %.2f", material->metallic);
                        ImGui::Text("Roughness: %.2f", material->roughness);
                        ImGui::Text("Texture Bound: %s", hasAlbedo ? "Yes" : "No");
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(3);
            }
        };

        static EditorPanel g_blackFlame{
            "The Black Flame",
            true,
            []()
            {
                auto& panel = BlackFlame();
                if (!panel.open || !g_engineInstance)
                    return;

                EditorState& editor = g_engineInstance->GetEditorState();
                BlackFlameAI& ai = editor.blackFlameAI;

                if (ImGui::Begin(panel.name, &panel.open))
                {
                    SceneInstance* selectedInstance = Scene::GetSelectedInstance();
                    std::string selectedName;
                    std::string selectedType;
                    std::string selectedMaterialName;
                    bool selectedHasMaterial = false;
                    float selectedMaterialRoughness = 1.0f;
                    float selectedMaterialMetallic = 0.0f;
                    if (selectedInstance)
                    {
                        selectedName = selectedInstance->name;
                        selectedType = BlackFlamePrimitiveLabel(selectedInstance->primitive);
                        if (Material* selectedMaterial = MaterialManager::GetInstance().GetMaterialByIndex(selectedInstance->materialIndex))
                        {
                            selectedMaterialName = selectedMaterial->name;
                            selectedHasMaterial = true;
                            selectedMaterialRoughness = selectedMaterial->roughness;
                            selectedMaterialMetallic = selectedMaterial->metallic;
                        }
                    }

                    const int lightCount = Graphics::GetInstance().GetDirectionalLight().intensity > 0.0f ? 1 : 0;
                    ai.SetEditorContext(selectedInstance != nullptr, selectedName, selectedType, selectedMaterialName, editor.currentBlackFlameAccess, selectedHasMaterial, selectedMaterialRoughness, selectedMaterialMetallic, lightCount);

                    const BlackFlameState state = ai.GetState();
                    const BlackFlameMode mode = ai.GetMode();
                    const BlackFlameDebugContext debugContext = ai.GetDebugContext();

                    DrawBlackFlameAvatar(state, ImVec2(ImGui::GetContentRegionAvail().x, 150.0f));
                    ImGui::TextColored(BlackFlameStateColor(state), "State: %s", BlackFlameStateLabel(state));
                    ImGui::SameLine();
                    ImGui::TextDisabled("| Access: %s", BlackFlameAccessLevelLabel(editor.currentBlackFlameAccess));
                    ImGui::Text("Mode: %s", mode == BlackFlameMode::Conversation ? "Conversation" : (mode == BlackFlameMode::Hybrid ? "Hybrid" : "Engine"));
                    ImGui::TextWrapped("%s", BlackFlameModeTagline(mode));

                    if (ImGui::Button("Engine##Mode")) { ai.SetMode(BlackFlameMode::Engine); g_BlackFlameConfirmApply = false; }
                    ImGui::SameLine();
                    if (ImGui::Button("Conversation##Mode")) { ai.SetMode(BlackFlameMode::Conversation); g_BlackFlameConfirmApply = false; }
                    ImGui::SameLine();
                    if (ImGui::Button("Hybrid##Mode")) { ai.SetMode(BlackFlameMode::Hybrid); g_BlackFlameConfirmApply = false; }

                    DrawBlackFlameSectionTitle("Invocation");
                    ImGui::InputTextMultiline("##BlackFlamePrompt", g_BlackFlamePromptBuffer, sizeof(g_BlackFlamePromptBuffer), ImVec2(-1.0f, 110.0f));
                    if (ImGui::Button("Invoke") && g_BlackFlamePromptBuffer[0] != '\0')
                    {
                        ai.SubmitPrompt(g_BlackFlamePromptBuffer);
                        g_BlackFlameConfirmApply = false;
                    }

                    DrawBlackFlameSectionTitle("Memory / Context", ImVec4(0.95f, 0.72f, 0.34f, 1.0f));
                    ImGui::Text("Selection: %s", debugContext.HasSelection ? (debugContext.SelectedName.empty() ? "(unnamed)" : debugContext.SelectedName.c_str()) : "None");
                    if (debugContext.HasSelection && !debugContext.SelectedType.empty())
                        ImGui::Text("Type: %s", debugContext.SelectedType.c_str());
                    if (debugContext.HasSelection && !debugContext.MaterialName.empty())
                        ImGui::Text("Material: %s", debugContext.MaterialName.c_str());
                    ImGui::TextWrapped("Hint: %s", debugContext.ContextSummary.empty() ? "No context available." : debugContext.ContextSummary.c_str());

                    DrawBlackFlameSectionTitle("Response", ImVec4(1.0f, 0.58f, 0.22f, 1.0f));
                    if (!ai.HasResponse())
                    {
                        ImGui::TextWrapped(state == BlackFlameState::Thinking ? "The flame intensifies and awakens..." : "No invocation yet.");
                    }
                    else
                    {
                        const BlackFlameResponse& response = ai.GetLastResponse();
                        if (response.IsConversation)
                        {
                            ImGui::TextWrapped("%s", response.ChatMessage.c_str());
                        }
                        else
                        {
                            if (mode == BlackFlameMode::Hybrid && !response.ProposalExplanation.empty())
                                ImGui::TextWrapped("%s", response.ProposalExplanation.c_str());
                            else if (!response.Explanation.empty())
                                ImGui::TextWrapped("%s", response.Explanation.c_str());

                            if (!response.Commands.empty())
                            {
                                ImGui::Spacing();
                                ImGui::TextUnformatted(mode == BlackFlameMode::Hybrid ? "Proposed Actions" : "Commands");
                                for (const BlackFlameCommand& cmd : response.Commands)
                                {
                                    std::string details;
                                    if (!cmd.StringValue.empty())
                                        details = std::format(" [{}]", cmd.StringValue);
                                    if (cmd.Type == BlackFlameCommandType::SetMaterialProperty)
                                    {
                                        if (cmd.StringValue == "BaseColor")
                                            details += std::format(" = ({:.2f}, {:.2f}, {:.2f})", cmd.FloatValue, cmd.FloatValue2, cmd.FloatValue3);
                                        else
                                            details += std::format(" = {:.2f}", cmd.FloatValue);
                                    }
                                    ImGui::BulletText("%s%s (Requires: %s)", BlackFlameCommandTypeLabel(cmd.Type), details.c_str(), BlackFlameAccessLevelLabel(cmd.RequiredAccess));
                                }

                                ImGui::Spacing();
                                if (!g_BlackFlameConfirmApply)
                                {
                                    if (ImGui::Button(mode == BlackFlameMode::Hybrid ? "Apply Proposed Changes" : "Apply"))
                                        g_BlackFlameConfirmApply = true;
                                }
                                else
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.54f, 0.18f, 1.0f), "Confirm Apply?");
                                    if (ImGui::Button("Confirm Apply"))
                                    {
                                        BlackFlameExecutionContext context{};
                                        context.Editor = &editor;
                                        context.GraphicsSystem = &Graphics::GetInstance();
                                        bool hasAdminCommand = false;
                                        for (const BlackFlameCommand& cmd : response.Commands)
                                        {
                                            if (cmd.RequiredAccess == BlackFlameAccessLevel::Admin)
                                            {
                                                hasAdminCommand = true;
                                                break;
                                            }
                                        }
                                        ai.BeginExecution(hasAdminCommand || editor.currentBlackFlameAccess == BlackFlameAccessLevel::Admin);
                                        const BlackFlameApplyResult applyResult = ApplyBlackFlameChange(response, editor.currentBlackFlameAccess, context);
                                        ai.NotifyExecutionResult(applyResult.success, applyResult.accessDenied);
                                        ai.RememberExecution(response, applyResult.success, applyResult.accessDenied);
                                        g_BlackFlameConfirmApply = false;
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("Cancel Apply"))
                                        g_BlackFlameConfirmApply = false;
                                }
                            }
                        }
                    }

                    const auto& suggestions = ai.GetActiveSuggestions();
                    if (!suggestions.empty())
                    {
                        DrawBlackFlameSectionTitle("Suggestions", ImVec4(1.0f, 0.48f, 0.22f, 1.0f));
                        for (size_t i = 0; i < suggestions.size(); ++i)
                        {
                            const BlackFlameSuggestion& suggestion = suggestions[i];
                            ImGui::TextColored(suggestion.IsWarning ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(1.0f, 0.64f, 0.24f, 1.0f), "%s", suggestion.Message.c_str());
                            if (!suggestion.ProposedCommands.empty())
                            {
                                const std::string applyId = std::format("Apply Suggestion##{}", i);
                                if (ImGui::Button(applyId.c_str()))
                                {
                                    BlackFlameExecutionContext context{};
                                    context.Editor = &editor;
                                    context.GraphicsSystem = &Graphics::GetInstance();
                                    BlackFlameResponse suggestionResponse{};
                                    suggestionResponse.Commands = suggestion.ProposedCommands;
                                    suggestionResponse.Explanation = suggestion.Message;
                                    const BlackFlameApplyResult applyResult = ApplyBlackFlameChange(suggestionResponse, editor.currentBlackFlameAccess, context);
                                    ai.NotifyExecutionResult(applyResult.success, applyResult.accessDenied);
                                    ai.RememberExecution(suggestionResponse, applyResult.success, applyResult.accessDenied);
                                    ai.RemoveSuggestion(i);
                                    break;
                                }
                            }
                            ImGui::Separator();
                        }
                    }
                }
                ImGui::End();
            }
        };

        static EditorPanel g_promptHelper{
            "Black Flame Prompts",
            true,
            []()
            {
                auto& panel = PromptHelper();
                if (!panel.open || !g_engineInstance)
                    return;

                EditorState& editor = g_engineInstance->GetEditorState();
                BlackFlameAI& ai = editor.blackFlameAI;
                const BlackFlameMode mode = ai.GetMode();
                const BlackFlameDebugContext ctx = ai.GetDebugContext();

                if (ImGui::Begin(panel.name, &panel.open))
                {
                    DrawBlackFlameAvatar(BlackFlameState::Ready, ImVec2(ImGui::GetContentRegionAvail().x, 124.0f));
                    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.20f, 1.0f), "Prompt Forge");
                    ImGui::SameLine();
                    ImGui::TextDisabled("| Mode: %s", mode == BlackFlameMode::Conversation ? "Conversation" : (mode == BlackFlameMode::Hybrid ? "Hybrid" : "Engine"));
                    ImGui::TextWrapped("%s", BlackFlameModeTagline(mode));

                    SceneInstance* selected = Scene::GetSelectedInstance();
                    Material* selectedMaterial = selected ? MaterialManager::GetInstance().GetMaterialByIndex(selected->materialIndex) : nullptr;
                    const auto& suggestions = ai.GetActiveSuggestions();
                    const std::vector<BlackFlamePromptEntry> dynamicPrompts = BuildDynamicPrompts(ctx, selectedMaterial, mode, suggestions);
                    if (!dynamicPrompts.empty())
                        ai.NotifySuggestionConfidence(!dynamicPrompts.front().Prompt.empty() ? dynamicPrompts.front().Prompt : dynamicPrompts.front().Label, dynamicPrompts.front().Score);

                    DrawBlackFlameSectionTitle("Recommended", ImVec4(1.0f, 0.76f, 0.30f, 1.0f));
                    bool drewRecommended = false;
                    for (const BlackFlamePromptEntry& entry : dynamicPrompts)
                    {
                        if (!entry.IsRecommended)
                            continue;
                        drewRecommended = true;
                        if (DrawDynamicPromptButton(entry))
                        {
                            if (!entry.ProposedCommands.empty() && entry.SuggestionIndex != static_cast<size_t>(-1))
                            {
                                BlackFlameExecutionContext context{};
                                context.Editor = &editor;
                                context.GraphicsSystem = &Graphics::GetInstance();
                                BlackFlameResponse response{};
                                response.Commands = entry.ProposedCommands;
                                response.Explanation = entry.Label;
                                ai.BeginExecution(editor.currentBlackFlameAccess == BlackFlameAccessLevel::Admin);
                                const BlackFlameApplyResult applyResult = ApplyBlackFlameChange(response, editor.currentBlackFlameAccess, context);
                                ai.NotifyExecutionResult(applyResult.success, applyResult.accessDenied);
                                ai.RememberExecution(response, applyResult.success, applyResult.accessDenied);
                                ai.RemoveSuggestion(entry.SuggestionIndex);
                            }
                            else
                            {
                                InvokeBlackFlamePrompt(ai, entry.Prompt);
                            }
                        }
                    }
                    if (!drewRecommended)
                        ImGui::TextDisabled("No recommendations yet.");

                    DrawBlackFlameSectionTitle("Quick Actions", ImVec4(0.95f, 0.72f, 0.34f, 1.0f));
                    bool drewQuickAction = false;
                    for (const BlackFlamePromptEntry& entry : dynamicPrompts)
                    {
                        if (entry.IsRecommended)
                            continue;
                        drewQuickAction = true;
                        if (DrawDynamicPromptButton(entry))
                            InvokeBlackFlamePrompt(ai, entry.Prompt);
                    }
                    if (!drewQuickAction)
                        ImGui::TextDisabled("No quick actions available.");

                    DrawBlackFlameSectionTitle("Context", ImVec4(0.95f, 0.72f, 0.34f, 1.0f));
                    if (ctx.HasSelection)
                    {
                        ImGui::Text("Selection: %s", ctx.SelectedName.empty() ? "(unnamed)" : ctx.SelectedName.c_str());
                        if (!ctx.SelectedType.empty())
                            ImGui::Text("Type: %s", ctx.SelectedType.c_str());
                        if (!ctx.MaterialName.empty())
                            ImGui::Text("Material: %s", ctx.MaterialName.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("No selection.");
                    }
                    if (!ctx.LastAction.empty())
                        ImGui::TextWrapped("Last Action: %s", ctx.LastAction.c_str());

                    DrawBlackFlameSectionTitle("Draft Invocation", ImVec4(1.0f, 0.58f, 0.22f, 1.0f));
                    ImGui::TextWrapped("Draft: %s", g_BlackFlamePromptBuffer[0] != '\0' ? g_BlackFlamePromptBuffer : "<empty>");
                    if (ImGui::Button("Invoke Draft") && g_BlackFlamePromptBuffer[0] != '\0')
                    {
                        InvokeBlackFlamePrompt(ai, g_BlackFlamePromptBuffer);
                    }
                }
                ImGui::End();
            }
        };

        static EditorPanel g_prefabWorkflow{
            "Prefab Workflow",
            false,
            []()
            {
                auto& panel = PrefabWorkflow();
                if (!panel.open || !g_engineInstance)
                    return;

                EditorState& editor = g_engineInstance->GetEditorState();
                SceneInstance* selected = Scene::GetSelectedInstance();
                const bool hasSelection = selected != nullptr;
                const bool isPrefabInstance = hasSelection && !selected->prefabSourcePath.empty();
                std::string selectedBasePrefabPath;
                const bool isVariantInstance = isPrefabInstance && Scene::GetPrefabAssetBasePrefab(selected->prefabSourcePath, selectedBasePrefabPath) && !selectedBasePrefabPath.empty();
                const bool selectedVariantBaseMissing = isVariantInstance && !std::filesystem::exists(selectedBasePrefabPath);
                std::string selectedVariantCyclePath;
                const bool selectedVariantHasCircularInheritance = isVariantInstance && !Scene::HasCircularPrefabInheritance(selected->prefabSourcePath, &selectedVariantCyclePath);
                PrefabOverrideState overrides{};
                const bool hasOverrideState = isPrefabInstance && Scene::TryGetPrefabOverrideState(selected->instanceId, overrides);

                auto drawActionButtonPair = [](const char* leftLabel, const char* rightLabel, const std::function<void()>& leftAction, const std::function<void()>& rightAction, bool enabled)
                {
                    if (!enabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Button(leftLabel))
                        leftAction();
                    ImGui::SameLine();
                    if (ImGui::Button(rightLabel))
                        rightAction();
                    if (!enabled)
                        ImGui::EndDisabled();
                };

                if (ImGui::Begin(panel.name, &panel.open))
                {
                    DrawBlackFlameAvatar(isPrefabInstance ? BlackFlameState::Ready : BlackFlameState::Idle, ImVec2(ImGui::GetContentRegionAvail().x, 132.0f));
                    ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.20f, 1.0f), "Prefab Foundry");
                    ImGui::TextWrapped("Forge reusable scene pieces, inspect override state, and spawn prefab content with the same visual language as the flame.");

                    if (g_RequestOpenPrefabRenamePopup)
                    {
                        ImGui::OpenPopup("Rename Prefab##PrefabLibrary");
                        g_RequestOpenPrefabRenamePopup = false;
                    }
                    if (g_RequestOpenPrefabDeletePopup)
                    {
                        ImGui::OpenPopup("Delete Prefab##PrefabLibrary");
                        g_RequestOpenPrefabDeletePopup = false;
                    }
                    if (g_RequestOpenPrefabCategoryPopup)
                    {
                        ImGui::OpenPopup("Edit Prefab Category##PrefabLibrary");
                        g_RequestOpenPrefabCategoryPopup = false;
                    }
                    if (g_RequestOpenPrefabTagsPopup)
                    {
                        ImGui::OpenPopup("Edit Prefab Tags##PrefabLibrary");
                        g_RequestOpenPrefabTagsPopup = false;
                    }
                    if (g_RequestOpenPrefabRebasePopup)
                    {
                        ImGui::OpenPopup("Rebase Prefab Variant##PrefabLibrary");
                        g_RequestOpenPrefabRebasePopup = false;
                    }

                    if (ImGui::BeginPopupModal("Rename Prefab##PrefabLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::TextUnformatted("Rename prefab asset");
                        ImGui::Separator();
                        ImGui::InputText("Name", g_PrefabLibraryRenameBuffer, IM_ARRAYSIZE(g_PrefabLibraryRenameBuffer));
                        if (ImGui::Button("Rename##PrefabLibrary") && g_PendingPrefabRenamePath != std::filesystem::path{})
                        {
                            std::filesystem::path renamedPath;
                            if (RenamePrefabAsset(g_PendingPrefabRenamePath, g_PrefabLibraryRenameBuffer, renamedPath))
                                Logger::Log(LogLevel::Info, std::format("Renamed prefab to {}", renamedPath.string()), "[Prefab]");
                            else
                                Logger::Log(LogLevel::Warning, "Failed to rename prefab asset.", "[Prefab]");
                            g_PendingPrefabRenamePath.clear();
                            g_PrefabLibraryRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel##PrefabLibraryRename"))
                        {
                            g_PendingPrefabRenamePath.clear();
                            g_PrefabLibraryRenameBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopupModal("Rebase Prefab Variant##PrefabLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::TextUnformatted("Choose a new base or variant prefab");
                        ImGui::Separator();
                        ImGui::InputTextWithHint("Search", "Filter prefabs...", g_PrefabRebaseSearchBuffer, IM_ARRAYSIZE(g_PrefabRebaseSearchBuffer));

                        const std::vector<PrefabLibraryEntry> rebaseEntries = BuildPrefabLibraryEntries(false);
                        std::string rebaseFilter = g_PrefabRebaseSearchBuffer;
                        std::transform(rebaseFilter.begin(), rebaseFilter.end(), rebaseFilter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                        ImGui::BeginChild("##PrefabRebaseCandidates", ImVec2(620.0f, 280.0f), ImGuiChildFlags_Border);
                        bool foundCandidate = false;
                        for (const PrefabLibraryEntry& entry : rebaseEntries)
                        {
                            if (entry.Path == g_PendingPrefabRebaseTargetPath)
                                continue;

                            std::string haystack = entry.SearchText;
                            std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (!rebaseFilter.empty() && haystack.find(rebaseFilter) == std::string::npos)
                                continue;

                            const bool candidateInvalid = entry.MissingBasePrefab || entry.HasCircularInheritance;
                            ImGui::PushID(entry.Path.string().c_str());
                            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                            const ImVec2 cardSize(ImGui::GetContentRegionAvail().x, 86.0f);
                            ImGui::InvisibleButton("##RebaseCard", cardSize);
                            const bool hovered = ImGui::IsItemHovered();
                            const ImVec2 cardMax = ImGui::GetItemRectMax();
                            ImDrawList* draw = ImGui::GetWindowDrawList();
                            draw->AddRectFilled(cardMin, cardMax, hovered ? IM_COL32(36, 28, 30, 236) : IM_COL32(22, 18, 20, 220), 10.0f);
                            draw->AddRect(cardMin, cardMax, candidateInvalid ? IM_COL32(255, 96, 78, 96) : hovered ? IM_COL32(255, 190, 120, 64) : IM_COL32(255, 255, 255, 18), 10.0f, 0, hovered ? 1.5f : 1.0f);

                            ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, cardMin.y + 10.0f));
                            if (ImTextureID thumb = GetPrefabThumbnailID(entry.Path))
                                ImGui::Image(thumb, ImVec2(56.0f, 56.0f), ImVec2(0, 0), ImVec2(1, 1));
                            else
                                ImGui::Dummy(ImVec2(56.0f, 56.0f));

                            ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 78.0f, cardMin.y + 10.0f));
                            ImGui::BeginGroup();
                            ImGui::TextUnformatted(entry.Label.c_str());
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.34f, 1.0f), "[%s]", entry.Category.c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", entry.BasePrefabPath.empty() ? "Base Prefab" : std::format("Variant of {}", entry.BasePrefabLabel).c_str());
                            ImGui::TextDisabled("%zu instances • %zu roots • %s", entry.InstanceCount, entry.RootCount, FormatPrefabFileSize(entry.FileSize).c_str());
                            ImGui::TextWrapped("%s", entry.Tags.empty() ? "Tags: <none>" : std::format("Tags: {}", JoinPrefabTags(entry.Tags)).c_str());
                            if (candidateInvalid)
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "%s", entry.MissingBasePrefab ? "Cannot use: missing base." : "Cannot use: circular inheritance.");
                            }
                            else
                            {
                                if (ImGui::SmallButton("Use As Base"))
                                {
                                    PushUndoSnapshot(editor);
                                    if (!Scene::SaveSelectedAsPrefabVariant(g_PendingPrefabRebaseTargetPath.string(), entry.Path.string()))
                                        Logger::Log(LogLevel::Warning, "Failed to rebase selected variant.", "[Prefab]");
                                    else
                                        Logger::Log(LogLevel::Info, std::format("Rebased variant {} to {}", g_PendingPrefabRebaseTargetPath.string(), entry.Path.string()), "[Prefab]");
                                    g_PendingPrefabRebaseTargetPath.clear();
                                    g_PrefabRebaseSearchBuffer[0] = '\0';
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                            ImGui::EndGroup();
                            ImGui::PopID();
                            ImGui::Spacing();
                            foundCandidate = true;
                        }
                        if (!foundCandidate)
                            ImGui::TextDisabled("No valid prefab candidates match the current filter.");
                        ImGui::EndChild();

                        if (ImGui::Button("Cancel##PrefabRebase"))
                        {
                            g_PendingPrefabRebaseTargetPath.clear();
                            g_PrefabRebaseSearchBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopupModal("Edit Prefab Tags##PrefabLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::TextUnformatted("Edit prefab tags");
                        ImGui::Separator();
                        ImGui::InputTextWithHint("Tags", "comma, separated, tags", g_PrefabLibraryTagsBuffer, IM_ARRAYSIZE(g_PrefabLibraryTagsBuffer));
                        if (ImGui::Button("Save##PrefabTags") && g_PendingPrefabTagsPath != std::filesystem::path{})
                        {
                            const std::vector<std::string> tags = ParsePrefabTagsBuffer(g_PrefabLibraryTagsBuffer);
                            if (Scene::SetPrefabAssetTags(g_PendingPrefabTagsPath.string(), tags))
                                Logger::Log(LogLevel::Info, std::format("Updated prefab tags for {}", g_PendingPrefabTagsPath.string()), "[Prefab]");
                            else
                                Logger::Log(LogLevel::Warning, "Failed to update prefab tags.", "[Prefab]");
                            g_PendingPrefabTagsPath.clear();
                            g_PrefabLibraryTagsBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel##PrefabTags"))
                        {
                            g_PendingPrefabTagsPath.clear();
                            g_PrefabLibraryTagsBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::TextDisabled("Tags are optional. Use commas to separate them.");
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopupModal("Edit Prefab Category##PrefabLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::TextUnformatted("Edit prefab category");
                        ImGui::Separator();
                        ImGui::InputText("Category", g_PrefabLibraryCategoryBuffer, IM_ARRAYSIZE(g_PrefabLibraryCategoryBuffer));
                        if (ImGui::Button("Save##PrefabCategory") && g_PendingPrefabCategoryPath != std::filesystem::path{})
                        {
                            if (Scene::SetPrefabAssetCategory(g_PendingPrefabCategoryPath.string(), g_PrefabLibraryCategoryBuffer))
                                Logger::Log(LogLevel::Info, std::format("Updated prefab category for {}", g_PendingPrefabCategoryPath.string()), "[Prefab]");
                            else
                                Logger::Log(LogLevel::Warning, "Failed to update prefab category.", "[Prefab]");
                            g_PendingPrefabCategoryPath.clear();
                            g_PrefabLibraryCategoryBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel##PrefabCategory"))
                        {
                            g_PendingPrefabCategoryPath.clear();
                            g_PrefabLibraryCategoryBuffer[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
                    if (ImGui::BeginPopupModal("Delete Prefab##PrefabLibrary", nullptr, ImGuiWindowFlags_NoResize))
                    {
                        const std::string pendingPrefabName = g_PendingPrefabDeletePath.empty() ? std::string("(unknown)") : g_PendingPrefabDeletePath.filename().string();
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 430.0f);
                        ImGui::Text("Delete prefab asset '%s'?", pendingPrefabName.c_str());
                        ImGui::Spacing();
                        ImGui::TextWrapped("Existing scene instances will keep their current scene data, but the prefab source file will be removed from the library.");
                        ImGui::PopTextWrapPos();
                        ImGui::Separator();
                        if (ImGui::Button("Delete##PrefabLibraryConfirm") && g_PendingPrefabDeletePath != std::filesystem::path{})
                        {
                            if (DeletePrefabAsset(g_PendingPrefabDeletePath))
                                Logger::Log(LogLevel::Info, std::format("Deleted prefab {}", g_PendingPrefabDeletePath.string()), "[Prefab]");
                            else
                                Logger::Log(LogLevel::Warning, "Failed to delete prefab asset.", "[Prefab]");
                            g_PendingPrefabDeletePath.clear();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel##PrefabLibraryDelete"))
                        {
                            g_PendingPrefabDeletePath.clear();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    DrawBlackFlameSectionTitle("Selected Object", ImVec4(0.95f, 0.72f, 0.34f, 1.0f));
                    if (!hasSelection)
                    {
                        ImGui::TextDisabled("No object selected.");
                    }
                    else
                    {
                        ImGui::Text("Name: %s", selected->name.empty() ? "(unnamed)" : selected->name.c_str());
                        ImGui::Text("Instance ID: %u", selected->instanceId);
                        ImGui::Text("Prefab Source: %s", isPrefabInstance ? selected->prefabSourcePath.c_str() : "None");
                        if (isVariantInstance)
                        {
                            ImGui::Text("Variant Of: %s", selectedBasePrefabPath.c_str());
                            if (ImGui::SmallButton("Open Base Prefab"))
                                RevealAssetInBrowser(selectedBasePrefabPath);
                            ImGui::SameLine();
                            if (ImGui::SmallButton(selectedVariantBaseMissing ? "Repair Broken Base..." : "Rebase Variant..."))
                            {
                                g_PendingPrefabRebaseTargetPath = selected->prefabSourcePath;
                                g_PrefabRebaseSearchBuffer[0] = '\0';
                                g_RequestOpenPrefabRebasePopup = true;
                            }
                            if (selectedVariantBaseMissing)
                            {
                                ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Warning: base prefab is missing.");
                                const std::vector<PrefabLibraryEntry> repairEntries = BuildPrefabLibraryEntries(false);
                                const std::vector<const PrefabLibraryEntry*> repairSuggestions = BuildPrefabRepairSuggestions(selectedBasePrefabPath, repairEntries);
                                if (!repairSuggestions.empty())
                                {
                                    ImGui::TextDisabled("Repair suggestions:");
                                    for (size_t suggestionIndex = 0; suggestionIndex < repairSuggestions.size(); ++suggestionIndex)
                                    {
                                        const PrefabLibraryEntry* suggestion = repairSuggestions[suggestionIndex];
                                        if (!suggestion)
                                            continue;
                                        if (suggestionIndex > 0)
                                            ImGui::SameLine();
                                        const std::string repairLabel = std::format("Repair -> {}##RepairSuggestion{}", suggestion->Label, suggestionIndex);
                                        if (ImGui::SmallButton(repairLabel.c_str()))
                                        {
                                            PushUndoSnapshot(editor);
                                            if (!Scene::SaveSelectedAsPrefabVariant(selected->prefabSourcePath, suggestion->Path.string()))
                                                Logger::Log(LogLevel::Warning, "Failed to repair broken base prefab reference.", "[Prefab]");
                                        }
                                    }
                                }
                            }
                            else if (selectedVariantHasCircularInheritance)
                                ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Warning: circular inheritance detected at %s", selectedVariantCyclePath.c_str());
                        }

                        if (isPrefabInstance)
                        {
                            bool chainMissingBase = false;
                            bool chainCircular = false;
                            std::string chainIssuePath;
                            const std::vector<std::string> inheritanceChain = BuildPrefabInheritanceChain(selected->prefabSourcePath, chainMissingBase, chainCircular, &chainIssuePath);
                            if (!inheritanceChain.empty())
                            {
                                DrawBlackFlameSectionTitle("Inheritance Chain", ImVec4(0.84f, 0.76f, 1.0f, 1.0f));
                                for (size_t chainIndex = 0; chainIndex < inheritanceChain.size(); ++chainIndex)
                                {
                                    const std::filesystem::path chainPath = std::filesystem::path(inheritanceChain[chainIndex]);
                                    const std::string chainLabel = chainPath.stem().stem().string();
                                    const char* role = chainIndex == 0 ? "Current" : "Base";
                                    ImGui::BulletText("%s %zu: %s", role, chainIndex, chainLabel.empty() ? chainPath.filename().string().c_str() : chainLabel.c_str());
                                    ImGui::SameLine();
                                    const std::string revealLabel = std::format("Show##Chain{}", chainIndex);
                                    if (ImGui::SmallButton(revealLabel.c_str()))
                                        RevealAssetInBrowser(inheritanceChain[chainIndex]);
                                }
                                if (chainMissingBase)
                                    ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Broken link: %s", chainIssuePath.c_str());
                                else if (chainCircular)
                                    ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Cycle detected at: %s", chainIssuePath.c_str());
                            }
                        }

                        if (hasOverrideState)
                        {
                            std::string overrideList;
                            auto appendOverride = [&overrideList](const char* label)
                            {
                                if (!overrideList.empty())
                                    overrideList += ", ";
                                overrideList += label;
                            };
                            if (overrides.name) appendOverride("Name");
                            if (overrides.rotation) appendOverride("Rotation");
                            if (overrides.scale) appendOverride("Scale");
                            if (overrides.visible) appendOverride("Visible");
                            if (overrides.material) appendOverride("Material");
                            ImGui::Text("Overrides: %s", overrideList.empty() ? "None" : overrideList.c_str());
                        }

                        if (hasOverrideState && overrides.Any())
                        {
                            DrawBlackFlameSectionTitle("Active Overrides", ImVec4(1.0f, 0.54f, 0.20f, 1.0f));
                            auto drawOverrideChip = [&](const char* label, PrefabProperty property, bool enabled)
                            {
                                if (!enabled)
                                    return;
                                ImGui::BulletText("%s", label);
                                ImGui::SameLine();
                                const std::string revertLabel = std::format("Revert {}##ActiveOverride", label);
                                if (ImGui::SmallButton(revertLabel.c_str()))
                                {
                                    PushUndoSnapshot(editor);
                                    Scene::RevertSelectedPrefabProperty(property);
                                }
                            };

                            drawOverrideChip("Name", PrefabProperty::Name, overrides.name);
                            drawOverrideChip("Rotation", PrefabProperty::Rotation, overrides.rotation);
                            drawOverrideChip("Scale", PrefabProperty::Scale, overrides.scale);
                            drawOverrideChip("Visible", PrefabProperty::Visible, overrides.visible);
                            drawOverrideChip("Material", PrefabProperty::Material, overrides.material);
                            drawOverrideChip("Vault Type", PrefabProperty::VaultType, overrides.vaultType);
                        }

                        if (ImGui::Button("Save Selected As Prefab"))
                        {
                            PushUndoSnapshot(editor);
                            const std::filesystem::path prefabPath = std::filesystem::path("Assets") / "Prefabs" /
                                (SanitizePrefabAssetName(selected->name) + ".prefab.json");
                            if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
                                Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
                        }
                        if (!isPrefabInstance || isVariantInstance)
                            ImGui::BeginDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Create Variant"))
                        {
                            PushUndoSnapshot(editor);
                            const std::filesystem::path variantPath = std::filesystem::path("Assets") / "Prefabs" /
                                (SanitizePrefabAssetName(selected->name) + "_Variant.prefab.json");
                            if (!Scene::SaveSelectedAsPrefabVariant(variantPath.string(), selected->prefabSourcePath))
                                Logger::Log(LogLevel::Error, std::format("Failed to create prefab variant: {}", variantPath.string()), "[Editor]");
                        }
                        if (!isPrefabInstance || isVariantInstance)
                            ImGui::EndDisabled();

                        drawActionButtonPair(
                            "Apply All Overrides",
                            "Revert All Overrides",
                            [&]()
                            {
                                if (!Scene::ApplySelectedToPrefab())
                                    Logger::Log(LogLevel::Error, "Failed to apply selected prefab instance.", "[Editor]");
                            },
                            [&]()
                            {
                                PushUndoSnapshot(editor);
                                if (!Scene::RevertSelectedToPrefab())
                                    Logger::Log(LogLevel::Error, "Failed to revert selected prefab instance.", "[Editor]");
                            },
                            isPrefabInstance);
                    }

                    DrawBlackFlameSectionTitle("Override Controls", ImVec4(1.0f, 0.58f, 0.22f, 1.0f));
                    if (!isPrefabInstance)
                    {
                        ImGui::TextDisabled("Select a prefab instance to manage property overrides.");
                    }
                    else
                    {
                        struct OverrideButton
                        {
                            const char* Label;
                            PrefabProperty Property;
                            bool Enabled;
                        } overrideButtons[] =
                        {
                            { "Name", PrefabProperty::Name, hasOverrideState && overrides.name },
                            { "Rotation", PrefabProperty::Rotation, hasOverrideState && overrides.rotation },
                            { "Scale", PrefabProperty::Scale, hasOverrideState && overrides.scale },
                            { "Visible", PrefabProperty::Visible, hasOverrideState && overrides.visible },
                            { "Material", PrefabProperty::Material, hasOverrideState && overrides.material },
                        };

                        for (const OverrideButton& button : overrideButtons)
                        {
                            if (!button.Enabled)
                                ImGui::BeginDisabled();
                            if (ImGui::Button(std::format("Apply {}##Prefab", button.Label).c_str()))
                                Scene::ApplySelectedPrefabProperty(button.Property);
                            ImGui::SameLine();
                            if (ImGui::Button(std::format("Revert {}##Prefab", button.Label).c_str()))
                            {
                                PushUndoSnapshot(editor);
                                Scene::RevertSelectedPrefabProperty(button.Property);
                            }
                            if (!button.Enabled)
                                ImGui::EndDisabled();
                        }
                    }

                    DrawBlackFlameSectionTitle("Prefab Library", ImVec4(1.0f, 0.48f, 0.22f, 1.0f));
                    const std::vector<PrefabLibraryEntry> allPrefabEntries = BuildPrefabLibraryEntries(false);
                    const std::vector<std::string> prefabCategories = BuildPrefabLibraryCategories(allPrefabEntries);
                    const std::vector<std::string> prefabTags = BuildPrefabLibraryTags(allPrefabEntries);
                    if (std::find(prefabCategories.begin(), prefabCategories.end(), g_PrefabLibraryCategoryFilter) == prefabCategories.end())
                        g_PrefabLibraryCategoryFilter = prefabCategories.empty() ? std::string("All Categories") : prefabCategories.front();
                    if (std::find(prefabTags.begin(), prefabTags.end(), g_PrefabLibraryTagFilter) == prefabTags.end())
                        g_PrefabLibraryTagFilter = prefabTags.empty() ? std::string("All Tags") : prefabTags.front();

                    ImGui::InputTextWithHint("##PrefabLibrarySearch", "Search prefabs...", g_PrefabLibrarySearchBuffer, IM_ARRAYSIZE(g_PrefabLibrarySearchBuffer));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::BeginCombo("##PrefabLibraryCategoryFilter", g_PrefabLibraryCategoryFilter.c_str()))
                    {
                        for (const std::string& category : prefabCategories)
                        {
                            const bool isSelected = (g_PrefabLibraryCategoryFilter == category);
                            if (ImGui::Selectable(category.c_str(), isSelected))
                                g_PrefabLibraryCategoryFilter = category;
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::BeginCombo("##PrefabLibraryTagFilter", g_PrefabLibraryTagFilter.c_str()))
                    {
                        for (const std::string& tag : prefabTags)
                        {
                            const bool isSelected = (g_PrefabLibraryTagFilter == tag);
                            if (ImGui::Selectable(tag.c_str(), isSelected))
                                g_PrefabLibraryTagFilter = tag;
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(130.0f);
                    if (ImGui::BeginCombo("##PrefabLibraryTypeFilter", PrefabTypeFilterLabel(g_PrefabLibraryTypeFilter)))
                    {
                        const PrefabLibraryTypeFilter filters[] =
                        {
                            PrefabLibraryTypeFilter::All,
                            PrefabLibraryTypeFilter::BaseOnly,
                            PrefabLibraryTypeFilter::VariantsOnly,
                        };
                        for (PrefabLibraryTypeFilter filter : filters)
                        {
                            const bool isSelected = (g_PrefabLibraryTypeFilter == filter);
                            if (ImGui::Selectable(PrefabTypeFilterLabel(filter), isSelected))
                                g_PrefabLibraryTypeFilter = filter;
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(170.0f);
                    if (ImGui::BeginCombo("##PrefabLibrarySort", PrefabSortModeLabel(g_PrefabLibrarySortMode)))
                    {
                        const PrefabLibrarySortMode modes[] =
                        {
                            PrefabLibrarySortMode::NameAsc,
                            PrefabLibrarySortMode::NameDesc,
                            PrefabLibrarySortMode::InstancesDesc,
                            PrefabLibrarySortMode::NewestFirst,
                        };
                        for (PrefabLibrarySortMode mode : modes)
                        {
                            const bool isSelected = (g_PrefabLibrarySortMode == mode);
                            if (ImGui::Selectable(PrefabSortModeLabel(mode), isSelected))
                                g_PrefabLibrarySortMode = mode;
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    const std::vector<PrefabLibraryEntry> prefabEntries = BuildPrefabLibraryEntries();
                    ImGui::TextDisabled("%zu prefab%s • Type: %s • Category: %s • Tag: %s", prefabEntries.size(), prefabEntries.size() == 1 ? "" : "s", PrefabTypeFilterLabel(g_PrefabLibraryTypeFilter), g_PrefabLibraryCategoryFilter.c_str(), g_PrefabLibraryTagFilter.c_str());
                    if (prefabEntries.empty())
                    {
                        ImGui::TextDisabled((g_PrefabLibrarySearchBuffer[0] != '\0' || g_PrefabLibraryCategoryFilter != "All Categories" || g_PrefabLibraryTagFilter != "All Tags" || g_PrefabLibraryTypeFilter != PrefabLibraryTypeFilter::All) ? "No prefabs match the current filters." : "No prefabs saved yet.");
                    }
                    else
                    {
                        ImGui::BeginChild("##PrefabLibraryList", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Border);
                        for (const PrefabLibraryEntry& entry : prefabEntries)
                        {
                            const std::string prefabPathString = entry.Path.string();
                            const std::string prefabLabel = entry.Label;
                            const ImTextureID prefabThumb = GetPrefabThumbnailID(entry.Path);
                            const DirectX::XMFLOAT3 spawnPosition = hasSelection ? Scene::GetSelectionCenterOrActivePosition() : DirectX::XMFLOAT3{ 0.0f, 0.5f, 0.0f };

                            ImGui::PushID(prefabPathString.c_str());
                            ImGui::BeginGroup();
                            ImGui::InvisibleButton("##PrefabCard", ImVec2(ImGui::GetContentRegionAvail().x, 104.0f));
                            const bool cardHovered = ImGui::IsItemHovered();
                            const ImVec2 cardMin = ImGui::GetItemRectMin();
                            const ImVec2 cardMax = ImGui::GetItemRectMax();
                            ImDrawList* draw = ImGui::GetWindowDrawList();
                            draw->AddRectFilled(cardMin, cardMax, cardHovered ? IM_COL32(34, 24, 26, 232) : IM_COL32(24, 18, 20, 220), 10.0f);
                            draw->AddRect(cardMin, cardMax, cardHovered ? IM_COL32(255, 190, 120, 52) : IM_COL32(255, 255, 255, 20), 10.0f, 0, cardHovered ? 1.5f : 1.0f);

                            if (cardHovered && !entry.BasePrefabPath.empty())
                            {
                                bool chainMissingBase = false;
                                bool chainCircular = false;
                                std::string chainIssuePath;
                                const std::vector<std::string> chain = BuildPrefabInheritanceChain(prefabPathString, chainMissingBase, chainCircular, &chainIssuePath);
                                if (!chain.empty())
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::TextUnformatted("Inheritance Chain");
                                    ImGui::Separator();
                                    for (size_t chainIndex = 0; chainIndex < chain.size(); ++chainIndex)
                                        ImGui::BulletText("%s", chain[chainIndex].c_str());
                                    if (chainMissingBase)
                                        ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Broken link: %s", chainIssuePath.c_str());
                                    else if (chainCircular)
                                        ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Cycle detected at: %s", chainIssuePath.c_str());
                                    ImGui::EndTooltip();
                                }
                            }

                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                            {
                                ImGui::SetDragDropPayload("SCENE_CREATE_PREFAB", prefabPathString.c_str(), prefabPathString.size() + 1u);
                                ImGui::TextUnformatted(prefabLabel.c_str());
                                ImGui::TextDisabled("Drop into Scene view to instantiate");
                                ImGui::EndDragDropSource();
                            }

                            ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, cardMin.y + 10.0f));
                            if (prefabThumb)
                            {
                                ImGui::Image(prefabThumb, ImVec2(56.0f, 56.0f), ImVec2(0, 0), ImVec2(1, 1));
                            }
                            else
                            {
                                ImGui::Dummy(ImVec2(56.0f, 56.0f));
                            }

                            ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 76.0f, cardMin.y + 10.0f));
                            ImGui::BeginGroup();
                            ImGui::TextUnformatted(prefabLabel.c_str());
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.34f, 1.0f), "[%s]", entry.Category.c_str());
                            if (!entry.BasePrefabLabel.empty())
                            {
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.72f, 0.82f, 1.0f, 1.0f), "Variant of %s", entry.BasePrefabLabel.c_str());
                            }
                            ImGui::TextDisabled("%s", prefabPathString.c_str());
                            ImGui::TextDisabled("%zu instances • %zu roots • %s", entry.InstanceCount, entry.RootCount, FormatPrefabFileSize(entry.FileSize).c_str());
                            const std::string cameraSuffix = entry.CameraCount > 0 ? std::format(" • {} camera{}", entry.CameraCount, entry.CameraCount == 1 ? "" : "s") : std::string{};
                            ImGui::TextWrapped("%s%s", entry.PrimitiveSummary.c_str(), cameraSuffix.c_str());
                            ImGui::TextWrapped("Tags: %s", entry.Tags.empty() ? "<none>" : JoinPrefabTags(entry.Tags).c_str());
                            if (entry.MissingBasePrefab)
                                ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Missing base prefab");
                            else if (entry.HasCircularInheritance)
                                ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.32f, 1.0f), "Circular inheritance detected");
                            if (ImGui::Button("Instantiate"))
                            {
                                PushUndoSnapshot(editor);
                                Scene::InstantiatePrefab(prefabPathString, spawnPosition);
                                editor.selection.position = Scene::GetSelectionCenterOrActivePosition();
                            }
                            ImGui::SameLine();
                            if (!entry.BasePrefabPath.empty())
                            {
                                if (ImGui::SmallButton("Open Base"))
                                    RevealAssetInBrowser(entry.BasePrefabPath);
                            }
                            else
                            {
                                ImGui::BeginDisabled();
                                ImGui::SmallButton("Open Base");
                                ImGui::EndDisabled();
                            }
                            ImGui::SameLine();
                            if (isVariantInstance && entry.BasePrefabPath.empty() && entry.Path != selected->prefabSourcePath)
                            {
                                if (ImGui::SmallButton("Rebase Here"))
                                {
                                    PushUndoSnapshot(editor);
                                    if (!Scene::SaveSelectedAsPrefabVariant(selected->prefabSourcePath, entry.Path.string()))
                                        Logger::Log(LogLevel::Warning, "Failed to rebase selected variant.", "[Prefab]");
                                }
                            }
                            else
                            {
                                ImGui::BeginDisabled();
                                ImGui::SmallButton("Rebase Here");
                                ImGui::EndDisabled();
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Category"))
                            {
                                g_PendingPrefabCategoryPath = entry.Path;
                                strncpy_s(g_PrefabLibraryCategoryBuffer, entry.Category.c_str(), _TRUNCATE);
                                g_RequestOpenPrefabCategoryPopup = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Tags"))
                            {
                                g_PendingPrefabTagsPath = entry.Path;
                                const std::string tagText = JoinPrefabTags(entry.Tags);
                                strncpy_s(g_PrefabLibraryTagsBuffer, tagText.c_str(), _TRUNCATE);
                                g_RequestOpenPrefabTagsPopup = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Duplicate"))
                            {
                                std::filesystem::path duplicatedPath;
                                if (DuplicatePrefabAsset(entry.Path, duplicatedPath))
                                    Logger::Log(LogLevel::Info, std::format("Duplicated prefab to {}", duplicatedPath.string()), "[Prefab]");
                                else
                                    Logger::Log(LogLevel::Warning, "Failed to duplicate prefab asset.", "[Prefab]");
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Rename"))
                            {
                                g_PendingPrefabRenamePath = entry.Path;
                                strncpy_s(g_PrefabLibraryRenameBuffer, prefabLabel.c_str(), _TRUNCATE);
                                g_RequestOpenPrefabRenamePopup = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Delete"))
                            {
                                g_PendingPrefabDeletePath = entry.Path;
                                g_RequestOpenPrefabDeletePopup = true;
                            }
                            ImGui::EndGroup();
                            ImGui::EndGroup();
                            ImGui::Spacing();
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::End();
            }
        };
    }

    void DrawPrefabOptionsMenu()
    {
        if (!ImGui::BeginMenu("Prefab"))
            return;

        SceneInstance* selected = Scene::GetSelectedInstance();
        const bool hasSelection = selected != nullptr;
        const bool isPrefabInstance = hasSelection && !selected->prefabSourcePath.empty();
        std::string selectedBasePrefabPath;
        const bool isVariantInstance = isPrefabInstance && Scene::GetPrefabAssetBasePrefab(selected->prefabSourcePath, selectedBasePrefabPath) && !selectedBasePrefabPath.empty();
        PrefabOverrideState overrides{};
        const bool hasOverrideState = isPrefabInstance && Scene::TryGetPrefabOverrideState(selected->instanceId, overrides);

        auto sanitizePrefabName = [](std::string name)
        {
            for (char& c : name)
            {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
                    c = '_';
            }
            if (name.empty())
                name = "Prefab";
            return name;
        };

        if (!hasSelection)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem("Save Selected As Prefab"))
        {
            const std::filesystem::path prefabPath = std::filesystem::path("Assets") / "Prefabs" /
                (sanitizePrefabName(selected->name) + ".prefab.json");
            if (!Scene::SaveSelectedAsPrefab(prefabPath.string()))
                Logger::Log(LogLevel::Error, std::format("Failed to save prefab: {}", prefabPath.string()), "[Editor]");
        }
        if (ImGui::MenuItem("Create Variant From Selected", nullptr, false, isPrefabInstance && !isVariantInstance))
        {
            const std::filesystem::path variantPath = std::filesystem::path("Assets") / "Prefabs" /
                (sanitizePrefabName(selected->name) + "_Variant.prefab.json");
            if (!Scene::SaveSelectedAsPrefabVariant(variantPath.string(), selected->prefabSourcePath))
                Logger::Log(LogLevel::Error, std::format("Failed to create prefab variant: {}", variantPath.string()), "[Editor]");
        }
        if (ImGui::MenuItem(isVariantInstance ? "Rebase Variant..." : "Rebase Variant...", nullptr, false, isVariantInstance))
        {
            g_PendingPrefabRebaseTargetPath = selected->prefabSourcePath;
            g_PrefabRebaseSearchBuffer[0] = '\0';
            g_RequestOpenPrefabRebasePopup = true;
            PrefabWorkflow().open = true;
        }
        if (ImGui::MenuItem("Open Base Prefab", nullptr, false, isVariantInstance))
            RevealAssetInBrowser(selectedBasePrefabPath);
        if (!hasSelection)
            ImGui::EndDisabled();

        ImGui::Separator();

        if (!isPrefabInstance)
            ImGui::BeginDisabled();
        if (ImGui::MenuItem("Apply Selected To Prefab"))
        {
            if (!Scene::ApplySelectedToPrefab())
                Logger::Log(LogLevel::Error, "Failed to apply selected prefab instance.", "[Editor]");
        }
        if (ImGui::MenuItem("Revert Selected To Prefab"))
        {
            if (!Scene::RevertSelectedToPrefab())
                Logger::Log(LogLevel::Error, "Failed to revert selected prefab instance.", "[Editor]");
        }

        if (ImGui::BeginMenu("Apply Override Property"))
        {
            const bool canApplyName = hasOverrideState && overrides.name;
            const bool canApplyRotation = hasOverrideState && overrides.rotation;
            const bool canApplyScale = hasOverrideState && overrides.scale;
            const bool canApplyVisible = hasOverrideState && overrides.visible;
            const bool canApplyMaterial = hasOverrideState && overrides.material;

            if (ImGui::MenuItem("Name", nullptr, false, canApplyName))
                Scene::ApplySelectedPrefabProperty(PrefabProperty::Name);
            if (ImGui::MenuItem("Rotation", nullptr, false, canApplyRotation))
                Scene::ApplySelectedPrefabProperty(PrefabProperty::Rotation);
            if (ImGui::MenuItem("Scale", nullptr, false, canApplyScale))
                Scene::ApplySelectedPrefabProperty(PrefabProperty::Scale);
            if (ImGui::MenuItem("Visible", nullptr, false, canApplyVisible))
                Scene::ApplySelectedPrefabProperty(PrefabProperty::Visible);
            if (ImGui::MenuItem("Material", nullptr, false, canApplyMaterial))
                Scene::ApplySelectedPrefabProperty(PrefabProperty::Material);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Revert Override Property"))
        {
            const bool canRevertName = hasOverrideState && overrides.name;
            const bool canRevertRotation = hasOverrideState && overrides.rotation;
            const bool canRevertScale = hasOverrideState && overrides.scale;
            const bool canRevertVisible = hasOverrideState && overrides.visible;
            const bool canRevertMaterial = hasOverrideState && overrides.material;

            if (ImGui::MenuItem("Name", nullptr, false, canRevertName))
                Scene::RevertSelectedPrefabProperty(PrefabProperty::Name);
            if (ImGui::MenuItem("Rotation", nullptr, false, canRevertRotation))
                Scene::RevertSelectedPrefabProperty(PrefabProperty::Rotation);
            if (ImGui::MenuItem("Scale", nullptr, false, canRevertScale))
                Scene::RevertSelectedPrefabProperty(PrefabProperty::Scale);
            if (ImGui::MenuItem("Visible", nullptr, false, canRevertVisible))
                Scene::RevertSelectedPrefabProperty(PrefabProperty::Visible);
            if (ImGui::MenuItem("Material", nullptr, false, canRevertMaterial))
                Scene::RevertSelectedPrefabProperty(PrefabProperty::Material);
            ImGui::EndMenu();
        }
        if (!isPrefabInstance)
            ImGui::EndDisabled();

        ImGui::EndMenu();
    }

    EditorPanel& MaterialPreview() { return g_materialPreview; }
    EditorPanel& BlackFlame() { return g_blackFlame; }
    EditorPanel& PromptHelper() { return g_promptHelper; }
    EditorPanel& PrefabWorkflow() { return g_prefabWorkflow; }
}
