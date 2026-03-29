#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "BlackFlameAudio.h"
#include <DirectXMath.h>

struct SceneInstance;
struct Material;
struct SceneEvent;

enum class BlackFlameState : uint8_t
{
    Idle = 0,
    Thinking,
    Ready,
    Executing,
    Error
};

enum class BlackFlameVisualProfile : uint8_t
{
    Normal = 0,
    Admin,
    Denied
};

struct BlackFlameVisualState
{
    float ExecPulse = 0.0f;
    float DenyPulse = 0.0f;
    float AdminPulse = 0.0f;
    float FocusPulse = 0.0f;
    float GlitchIntensity = 0.0f;
    float Stability = 1.0f;
    DirectX::XMFLOAT3 ColorBias{ 1.0f, 1.0f, 1.0f };
    float LightInfluence = 0.0f;

    void Reset()
    {
        ExecPulse = 0.0f;
        DenyPulse = 0.0f;
        AdminPulse = 0.0f;
        FocusPulse = 0.0f;
        GlitchIntensity = 0.0f;
        Stability = 1.0f;
        ColorBias = { 1.0f, 1.0f, 1.0f };
        LightInfluence = 0.0f;
    }
};

enum class BlackFlameAccessLevel : uint8_t
{
    User = 0,
    Admin = 1
};

enum class BlackFlameMode : uint8_t
{
    Engine = 0,
    Conversation,
    Hybrid
};

enum class BlackFlameCommandType : uint8_t
{
    Unknown = 0,
    ResetAllocatorSafely,
    CreateEntity,
    SetMaterialProperty,
    CreateLight
};

enum class SceneEventType : uint8_t
{
    EntityCreated = 0,
    EntitySelected,
    MaterialChanged,
    LightCreated
};

enum class EngineEventType : uint8_t
{
    DeviceLost = 0,
    DeviceRecovered,
    AllocatorResetWarning,
    FenceSyncIssue,
    ResourceBarrierMismatch,
    GPUCrash,
    NormalFrame
};

struct SceneEvent
{
    SceneEventType Type = SceneEventType::EntityCreated;
    SceneInstance* Entity = nullptr;
    Material* Material = nullptr;
    float EventStrength = 1.0f;
    DirectX::XMFLOAT3 ColorBias{ 1.0f, 1.0f, 1.0f };
    float LightInfluence = 0.0f;
};

class SceneEventDispatcher
{
public:
    void Emit(const SceneEvent& evt) const
    {
        if (Callback)
            Callback(evt);
    }

    std::function<void(const SceneEvent&)> Callback;
};

struct BlackFlameCommand
{
    BlackFlameCommandType Type = BlackFlameCommandType::Unknown;
    BlackFlameAccessLevel RequiredAccess = BlackFlameAccessLevel::User;
    std::string StringValue;
    std::string StringValue2;
    float FloatValue = 0.0f;
    float FloatValue2 = 0.0f;
    float FloatValue3 = 0.0f;
    float FloatValue4 = 0.0f;
    int IntValue = 0;
};

struct BlackFlameResponse
{
    std::string GeneratedCode;
    std::vector<std::string> Changes;
    std::string Explanation;
    std::string OriginalCode;
    std::string DiffView;
    std::vector<BlackFlameCommand> Commands;
    bool IsConversation = false;
    std::string ChatMessage;
    bool HasProposedCommands = false;
    std::string ProposalExplanation;
};

struct BlackFlameChatResponse
{
    std::string Message;
};

struct BlackFlameSuggestion
{
    std::string Message;
    std::vector<BlackFlameCommand> ProposedCommands;
    float Lifetime = 5.0f;
    float TimeAlive = 0.0f;
    bool IsWarning = false;
};

struct BlackFlameDebugContext
{
    bool HasSelection = false;
    bool HasMaterial = false;
    std::string SelectedName;
    std::string SelectedType;
    std::string MaterialName;
    std::string LastAction;
    std::string LastCreatedType;
    BlackFlameAccessLevel AccessLevel = BlackFlameAccessLevel::User;
    std::string ContextSummary;
    std::vector<std::string> RecentMemories;
    float Roughness = 1.0f;
    float Metallic = 0.0f;
    int LightCount = 0;
};

class BlackFlameAI
{
public:
    void Initialize()
    {
        currentPrompt.clear();
        pendingPrompt.clear();
        lastResponse = {};
        hasResponse = false;
        state = BlackFlameState::Idle;
        visualProfile = BlackFlameVisualProfile::Normal;
        visualState.Reset();
        lastVisualUpdate = std::chrono::steady_clock::now();
        stateUntil = {};
        thinkingStartedAt = {};
        thinkingDuration = std::chrono::milliseconds(350);
        contextHasSelection = false;
        contextSelectionName.clear();
        contextSelectionType.clear();
        contextMaterialName.clear();
        recentMemories.clear();
        lastCreatedEntityType.clear();
        contextAccessLevel = BlackFlameAccessLevel::Admin;
        currentMode = BlackFlameMode::Engine;
        selectedHasMaterial = false;
        selectedMaterialRoughness = 1.0f;
        selectedMaterialMetallic = 0.0f;
        contextLightCount = 0;
        activeSuggestions.clear();
        suggestionCooldown = 0.0f;
        lastRecommendationNudgeAt = {};
    }

    void Shutdown()
    {
        Initialize();
    }

    void SetMode(BlackFlameMode mode)
    {
        currentMode = mode;
    }

    BlackFlameMode GetMode() const
    {
        return currentMode;
    }

    void SetEditorContext(bool hasSelection, const std::string& selectionName, const std::string& selectionType, const std::string& materialName, BlackFlameAccessLevel accessLevel, bool hasMaterial, float materialRoughness, float materialMetallic, int lightCount)
    {
        contextHasSelection = hasSelection;
        contextSelectionName = selectionName;
        contextSelectionType = selectionType;
        contextMaterialName = materialName;
        contextAccessLevel = accessLevel;
        selectedHasMaterial = hasMaterial;
        selectedMaterialRoughness = materialRoughness;
        selectedMaterialMetallic = materialMetallic;
        contextLightCount = lightCount;
    }

    void SubmitPrompt(const std::string& prompt)
    {
        currentPrompt = prompt;
        pendingPrompt = prompt;
        lastResponse = {};
        hasResponse = false;

        if (currentPrompt.empty())
        {
            SetState(BlackFlameState::Idle);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        const int jitterMs = static_cast<int>(nowMs % 151);
        SetState(BlackFlameState::Thinking, std::chrono::milliseconds(250 + jitterMs));
    }

    void Update()
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastVisualUpdate).count();
        OnEngineEvent(EngineEventType::NormalFrame);

        if (suggestionCooldown > 0.0f)
            suggestionCooldown = (std::max)(0.0f, suggestionCooldown - (std::max)(0.0f, dt));

        for (auto& suggestion : activeSuggestions)
            suggestion.TimeAlive += (std::max)(0.0f, dt);

        activeSuggestions.erase(
            std::remove_if(activeSuggestions.begin(), activeSuggestions.end(), [](const BlackFlameSuggestion& suggestion)
                {
                    return suggestion.TimeAlive >= suggestion.Lifetime;
                }),
            activeSuggestions.end());

        if (suggestionCooldown <= 0.0f)
            EvaluateSuggestions();

        DecayVisualState(now);
        BlackFlameAudio::Get().UpdateReactiveMix(
            visualState.ExecPulse,
            visualState.AdminPulse,
            visualState.DenyPulse,
            visualState.FocusPulse,
            visualState.GlitchIntensity,
            visualState.Stability,
            (std::max)(dt, 0.0f));

        if (state == BlackFlameState::Thinking && thinkingStartedAt != std::chrono::steady_clock::time_point{})
        {
            const float thinkSeconds = (std::max)(0.001f, thinkingDuration.count() / 1000.0f);
            const float elapsedSeconds = std::chrono::duration<float>(now - thinkingStartedAt).count();
            const float t = std::clamp(elapsedSeconds / thinkSeconds, 0.0f, 1.0f);
            visualState.ExecPulse = (std::max)(visualState.ExecPulse, t * 0.20f);
            visualState.FocusPulse = (std::max)(visualState.FocusPulse, t * 0.10f);
        }

        if (state == BlackFlameState::Thinking && stateUntil != std::chrono::steady_clock::time_point{} && now >= stateUntil)
        {
            if (currentMode == BlackFlameMode::Engine)
            {
                const std::string fullPrompt = BuildBlackFlamePrompt(pendingPrompt, BuildRuntimeContextBlock());
                const std::string jsonResponse = SendToAI_Engine(fullPrompt, pendingPrompt);
                hasResponse = ParseResponse(jsonResponse);

                if (!hasResponse)
                {
                    lastResponse = {};
                    lastResponse.Explanation = "The Black Flame rejected an invalid backend response.";
                    lastResponse.Changes = { "Structured response validation failed" };
                    TriggerDenyPulse();
                    SetState(BlackFlameState::Error, std::chrono::milliseconds(850));
                }
                else
                {
                    SetState(BlackFlameState::Ready);
                }
            }
            else if (currentMode == BlackFlameMode::Conversation)
            {
                const BlackFlameChatResponse chat = SendToAI_Conversation(pendingPrompt);
                lastResponse = {};
                lastResponse.IsConversation = true;
                lastResponse.ChatMessage = chat.Message;
                lastResponse.Explanation = chat.Message;
                hasResponse = !chat.Message.empty();
                SetState(hasResponse ? BlackFlameState::Ready : BlackFlameState::Error,
                    hasResponse ? std::chrono::milliseconds(0) : std::chrono::milliseconds(850));
            }
            else
            {
                lastResponse = BuildHybridResponse(pendingPrompt);
                hasResponse = !lastResponse.ProposalExplanation.empty() || !lastResponse.Commands.empty() || !lastResponse.ChatMessage.empty();
                SetState(hasResponse ? BlackFlameState::Ready : BlackFlameState::Error,
                    hasResponse ? std::chrono::milliseconds(0) : std::chrono::milliseconds(850));
            }

            pendingPrompt.clear();
        }
        else if ((state == BlackFlameState::Executing || state == BlackFlameState::Error) &&
            stateUntil != std::chrono::steady_clock::time_point{} && now >= stateUntil)
        {
            SetState(hasResponse ? BlackFlameState::Ready : BlackFlameState::Idle);
        }
    }

    void RememberExecution(const BlackFlameResponse& response, bool success, bool accessDenied = false)
    {
        if (accessDenied)
            PushMemory("Access denied for requested action.");
        if (!success)
        {
            PushMemory("Last invocation failed.");
            return;
        }

        for (const BlackFlameCommand& cmd : response.Commands)
        {
            switch (cmd.Type)
            {
            case BlackFlameCommandType::CreateEntity:
                if (!cmd.StringValue.empty())
                {
                    lastCreatedEntityType = cmd.StringValue;
                    PushMemory("Created " + cmd.StringValue + " entity.");
                }
                break;
            case BlackFlameCommandType::SetMaterialProperty:
            {
                std::string target = BuildCurrentTargetDescription();
                if (target.empty())
                    target = "current selection";
                PushMemory("Updated " + cmd.StringValue + " on " + target + ".");
                break;
            }
            case BlackFlameCommandType::CreateLight:
                PushMemory("Adjusted " + cmd.StringValue + " light.");
                break;
            case BlackFlameCommandType::ResetAllocatorSafely:
                PushMemory("Requested safe allocator reset.");
                break;
            case BlackFlameCommandType::Unknown:
            default:
                break;
            }
        }
    }

    const BlackFlameResponse& GetLastResponse() const { return lastResponse; }
    bool HasResponse() const { return hasResponse; }
    BlackFlameState GetState() const { return state; }
    BlackFlameVisualProfile GetVisualProfile() const { return visualProfile; }
    const BlackFlameVisualState& GetVisualState() const { return visualState; }
    const std::vector<BlackFlameSuggestion>& GetActiveSuggestions() const { return activeSuggestions; }
    void RemoveSuggestion(size_t index)
    {
        if (index < activeSuggestions.size())
            activeSuggestions.erase(activeSuggestions.begin() + index);
    }

    void BeginExecution(bool adminMode = false)
    {
        if (adminMode)
            TriggerAdminPulse();
        BlackFlameAudio::Get().StopLoop();
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Execute, adminMode ? BlackFlameSoundStyle::Arcane : BlackFlameSoundStyle::Warm, adminMode ? 0.95f : 0.75f);
        visualProfile = adminMode ? BlackFlameVisualProfile::Admin : BlackFlameVisualProfile::Normal;
        SetState(BlackFlameState::Executing, std::chrono::milliseconds(650));
    }

    void NotifyExecutionResult(bool success, bool accessDenied = false)
    {
        if (success)
            TriggerExecPulse();
        if (accessDenied)
        {
            TriggerDenyPulse();
            BlackFlameAudio::Get().StopLoop();
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 1.0f);
        }
        visualProfile = accessDenied ? BlackFlameVisualProfile::Denied : BlackFlameVisualProfile::Normal;
        SetState(success ? BlackFlameState::Ready : BlackFlameState::Error,
            success ? std::chrono::milliseconds(300) : std::chrono::milliseconds(900));
    }

    void NotifySuggestionConfidence(float topScore)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto cooldown = std::chrono::milliseconds(1800);
        if (lastRecommendationNudgeAt != std::chrono::steady_clock::time_point{} && now - lastRecommendationNudgeAt < cooldown)
            return;

        if (topScore > 85.0f)
        {
            AccumulatePulse(visualState.AdminPulse, 0.12f);
            visualState.ColorBias = { 1.0f, 0.80f, 0.40f };
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::HighConfidence, BlackFlameSoundStyle::Arcane, topScore / 100.0f);
            lastRecommendationNudgeAt = now;
        }
        else if (topScore > 60.0f)
        {
            AccumulatePulse(visualState.ExecPulse, 0.06f);
            AccumulatePulse(visualState.FocusPulse, 0.05f);
            lastRecommendationNudgeAt = now;
        }
    }

    void OnSceneEvent(const SceneEvent& evt)
    {
        switch (evt.Type)
        {
        case SceneEventType::EntityCreated:
            AccumulatePulse(visualState.ExecPulse, 0.20f * evt.EventStrength);
            break;
        case SceneEventType::MaterialChanged:
            AccumulatePulse(visualState.ExecPulse, 0.15f * evt.EventStrength);
            visualState.ColorBias = evt.ColorBias;
            break;
        case SceneEventType::LightCreated:
            AccumulatePulse(visualState.AdminPulse, 0.15f * evt.EventStrength);
            visualState.LightInfluence = (std::max)(visualState.LightInfluence, evt.LightInfluence);
            break;
        case SceneEventType::EntitySelected:
            AccumulatePulse(visualState.ExecPulse, 0.04f * evt.EventStrength);
            AccumulatePulse(visualState.FocusPulse, 0.25f * evt.EventStrength);
            break;
        default:
            break;
        }
    }

    void OnEngineEvent(EngineEventType evt)
    {
        switch (evt)
        {
        case EngineEventType::DeviceLost:
            visualState.DenyPulse = (std::max)(visualState.DenyPulse, 2.0f);
            visualState.GlitchIntensity = (std::max)(visualState.GlitchIntensity, 2.5f);
            visualState.Stability = (std::min)(visualState.Stability, 0.2f);
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 1.2f);
            PushMemory("Engine device lost. The flame destabilized.");
            break;
        case EngineEventType::GPUCrash:
            visualState.DenyPulse = (std::max)(visualState.DenyPulse, 2.2f);
            visualState.GlitchIntensity = (std::max)(visualState.GlitchIntensity, 2.8f);
            visualState.Stability = (std::min)(visualState.Stability, 0.1f);
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 1.3f);
            PushMemory("GPU crash detected.");
            break;
        case EngineEventType::AllocatorResetWarning:
            AccumulatePulse(visualState.DenyPulse, 0.6f);
            visualState.GlitchIntensity = (std::min)(3.0f, visualState.GlitchIntensity + 0.5f);
            visualState.Stability = (std::max)(0.0f, visualState.Stability - 0.08f);
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.8f);
            PushMemory("Allocator reset warning detected.");
            break;
        case EngineEventType::FenceSyncIssue:
            AccumulatePulse(visualState.DenyPulse, 0.6f);
            visualState.GlitchIntensity = (std::min)(3.0f, visualState.GlitchIntensity + 0.55f);
            visualState.Stability = (std::max)(0.0f, visualState.Stability - 0.10f);
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.85f);
            PushMemory("Fence synchronization issue detected.");
            break;
        case EngineEventType::ResourceBarrierMismatch:
            AccumulatePulse(visualState.DenyPulse, 0.8f);
            visualState.GlitchIntensity = (std::min)(3.0f, visualState.GlitchIntensity + 0.8f);
            visualState.Stability = (std::max)(0.0f, visualState.Stability - 0.12f);
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.95f);
            PushMemory("Resource barrier mismatch detected.");
            break;
        case EngineEventType::DeviceRecovered:
            AccumulatePulse(visualState.AdminPulse, 1.2f);
            visualState.GlitchIntensity *= 0.35f;
            visualState.Stability = 1.0f;
            BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Clean, 0.8f);
            PushMemory("Graphics device recovered.");
            break;
        case EngineEventType::NormalFrame:
            visualState.Stability = std::clamp(visualState.Stability + 0.02f, 0.0f, 1.0f);
            break;
        }
    }

    static bool ValidateCommand(const BlackFlameCommand& cmd)
    {
        switch (cmd.Type)
        {
        case BlackFlameCommandType::ResetAllocatorSafely:
            return true;
        case BlackFlameCommandType::CreateEntity:
            return cmd.StringValue == "Empty" || cmd.StringValue == "Cube" || cmd.StringValue == "Sphere" || cmd.StringValue == "Plane";
        case BlackFlameCommandType::SetMaterialProperty:
            if (cmd.StringValue == "Metallic" || cmd.StringValue == "Roughness")
                return cmd.FloatValue >= 0.0f && cmd.FloatValue <= 1.0f;
            if (cmd.StringValue == "NormalFlipY")
                return cmd.FloatValue == 0.0f || cmd.FloatValue == 1.0f;
            if (cmd.StringValue == "BaseColor")
                return cmd.FloatValue >= 0.0f && cmd.FloatValue <= 1.0f &&
                    cmd.FloatValue2 >= 0.0f && cmd.FloatValue2 <= 1.0f &&
                    cmd.FloatValue3 >= 0.0f && cmd.FloatValue3 <= 1.0f;
            return false;
        case BlackFlameCommandType::CreateLight:
            return cmd.StringValue == "Directional" || cmd.StringValue == "Point";
        case BlackFlameCommandType::Unknown:
        default:
            return false;
        }
    }

    BlackFlameDebugContext GetDebugContext() const
    {
        BlackFlameDebugContext ctx{};
        ctx.HasSelection = contextHasSelection;
        ctx.HasMaterial = selectedHasMaterial;
        ctx.SelectedName = contextSelectionName;
        ctx.SelectedType = contextSelectionType;
        ctx.MaterialName = contextMaterialName;
        ctx.LastCreatedType = lastCreatedEntityType;
        ctx.AccessLevel = contextAccessLevel;
        ctx.RecentMemories = recentMemories;
        ctx.Roughness = selectedMaterialRoughness;
        ctx.Metallic = selectedMaterialMetallic;
        ctx.LightCount = contextLightCount;
        if (!recentMemories.empty())
            ctx.LastAction = recentMemories.back();

        std::stringstream summary;
        summary << ((contextAccessLevel == BlackFlameAccessLevel::Admin) ? "Admin" : "User") << " access";
        summary << " | Mode: " << ((currentMode == BlackFlameMode::Conversation) ? "Conversation" : (currentMode == BlackFlameMode::Hybrid) ? "Hybrid" : "Engine");
        if (contextHasSelection)
        {
            summary << " | Selected ";
            summary << (contextSelectionType.empty() ? "object" : contextSelectionType);
            if (!contextSelectionName.empty())
                summary << " '" << contextSelectionName << "'";
            if (!contextMaterialName.empty())
                summary << " using material '" << contextMaterialName << "'";
        }
        else
        {
            summary << " | No selection";
        }

        if (!lastCreatedEntityType.empty())
            summary << " | Last created " << lastCreatedEntityType;
        if (!ctx.LastAction.empty())
            summary << " | Last action: " << ctx.LastAction;

        ctx.ContextSummary = summary.str();
        return ctx;
    }

private:
    void SetState(BlackFlameState newState, std::chrono::milliseconds duration = std::chrono::milliseconds(0))
    {
        const BlackFlameState previousState = state;
        state = newState;
        if (newState == BlackFlameState::Idle || newState == BlackFlameState::Ready)
            visualProfile = BlackFlameVisualProfile::Normal;

        if (previousState == BlackFlameState::Thinking && newState != BlackFlameState::Thinking)
            BlackFlameAudio::Get().StopLoop();

        if (previousState != newState)
        {
            if (newState == BlackFlameState::Thinking)
            {
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::ThinkingStart, BlackFlameSoundStyle::Arcane, 0.6f);
                BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Warm, 0.35f);
                BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Arcane, 0.10f);
                BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Glitch, 0.0f);
            }
            else if (newState == BlackFlameState::Ready)
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Clean, 0.55f);
            else if (newState == BlackFlameState::Error)
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.9f);
        }

        if (newState == BlackFlameState::Thinking)
        {
            thinkingStartedAt = std::chrono::steady_clock::now();
            if (duration.count() > 0)
                thinkingDuration = duration;
        }
        else
        {
            thinkingStartedAt = {};
        }

        if (duration.count() > 0)
            stateUntil = std::chrono::steady_clock::now() + duration;
        else
            stateUntil = {};
    }

    static float Approach(float value, float target, float factor)
    {
        return value + (target - value) * factor;
    }

    static void AccumulatePulse(float& pulse, float amount)
    {
        pulse = (std::min)(1.5f, pulse + amount);
    }

    void DecayVisualState(std::chrono::steady_clock::time_point now)
    {
        const float dt = std::chrono::duration<float>(now - lastVisualUpdate).count();
        lastVisualUpdate = now;
        const float frameScale = (dt > 0.0f) ? (dt / (1.0f / 60.0f)) : 1.0f;
        visualState.ExecPulse *= std::pow(0.92f, frameScale);
        visualState.DenyPulse *= std::pow(0.90f, frameScale);
        visualState.AdminPulse *= std::pow(0.94f, frameScale);
        visualState.FocusPulse *= std::pow(0.90f, frameScale);
        visualState.GlitchIntensity *= std::pow(0.92f, frameScale);

        const float settleFactor = (std::min)(1.0f, 0.06f * frameScale);
        visualState.ColorBias.x = Approach(visualState.ColorBias.x, 1.0f, settleFactor);
        visualState.ColorBias.y = Approach(visualState.ColorBias.y, 1.0f, settleFactor);
        visualState.ColorBias.z = Approach(visualState.ColorBias.z, 1.0f, settleFactor);
        visualState.LightInfluence = Approach(visualState.LightInfluence, 0.0f, (std::min)(1.0f, 0.08f * frameScale));
        visualState.Stability = std::clamp(Approach(visualState.Stability, 1.0f, (std::min)(1.0f, 0.01f * frameScale)), 0.0f, 1.0f);

        if (visualState.ExecPulse < 0.001f) visualState.ExecPulse = 0.0f;
        if (visualState.DenyPulse < 0.001f) visualState.DenyPulse = 0.0f;
        if (visualState.AdminPulse < 0.001f) visualState.AdminPulse = 0.0f;
        if (visualState.FocusPulse < 0.001f) visualState.FocusPulse = 0.0f;
        if (visualState.GlitchIntensity < 0.001f) visualState.GlitchIntensity = 0.0f;
    }

    void TriggerExecPulse() { visualState.ExecPulse = 1.0f; }
    void TriggerDenyPulse() { visualState.DenyPulse = 1.0f; }
    void TriggerAdminPulse() { visualState.AdminPulse = 1.0f; }

    static std::string BuildSimpleDiff(const std::string& before, const std::string& after)
    {
        std::stringstream ss;
        ss << "--- Before ---\n" << before << "\n\n";
        ss << "+++ After +++\n" << after << "\n";
        return ss.str();
    }

    static bool ContainsPromptToken(const std::string& prompt, const std::string& token)
    {
        if (token.empty())
            return false;
        std::string loweredPrompt = prompt;
        std::string loweredToken = token;
        for (char& c : loweredPrompt)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : loweredToken)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return loweredPrompt.find(loweredToken) != std::string::npos;
    }

    static std::string EscapeJsonString(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8);
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

    void PushMemory(const std::string& memory)
    {
        if (memory.empty())
            return;
        recentMemories.push_back(memory);
        constexpr size_t kMaxMemories = 6;
        if (recentMemories.size() > kMaxMemories)
            recentMemories.erase(recentMemories.begin(), recentMemories.begin() + (recentMemories.size() - kMaxMemories));
    }

    static bool PromptUsesSelectionReference(const std::string& prompt)
    {
        return ContainsPromptToken(prompt, " it") ||
            ContainsPromptToken(prompt, "it ") ||
            ContainsPromptToken(prompt, " that") ||
            ContainsPromptToken(prompt, "that ") ||
            ContainsPromptToken(prompt, "selected") ||
            ContainsPromptToken(prompt, "current object");
    }

    std::string BuildCurrentTargetDescription() const
    {
        if (!contextHasSelection)
            return {};
        std::string target = contextSelectionType.empty() ? std::string("selection") : contextSelectionType;
        if (!contextSelectionName.empty())
            target += " '" + contextSelectionName + "'";
        if (!contextMaterialName.empty())
            target += " using material '" + contextMaterialName + "'";
        return target;
    }

    std::string BuildRuntimeContextBlock() const
    {
        std::stringstream ss;
        ss << "AccessLevel: " << ((contextAccessLevel == BlackFlameAccessLevel::Admin) ? "Admin" : "User") << "\n";
        ss << "Mode: " << ((currentMode == BlackFlameMode::Conversation) ? "Conversation" : (currentMode == BlackFlameMode::Hybrid) ? "Hybrid" : "Engine") << "\n";
        if (contextHasSelection)
            ss << "Selection: " << BuildCurrentTargetDescription() << "\n";
        else
            ss << "Selection: none\n";
        if (!lastCreatedEntityType.empty())
            ss << "LastCreatedEntity: " << lastCreatedEntityType << "\n";
        if (!recentMemories.empty())
        {
            ss << "RecentMemory:\n";
            for (const std::string& memory : recentMemories)
                ss << "- " << memory << "\n";
        }
        return ss.str();
    }

    static std::string BuildBlackFlamePrompt(const std::string& userPrompt, const std::string& runtimeContext)
    {
        return
            "You are The Black Flame, an AI integrated into a DirectX 12 game engine.\n"
            "Respond ONLY in valid JSON with fields:\n"
            "- code\n"
            "- changes\n"
            "- explanation\n"
            "- commands (array)\n\n"
            "Command types allowed:\n"
            "- ResetAllocatorSafely (Admin)\n"
            "- CreateEntity (User)\n"
            "- SetMaterialProperty (User)\n"
            "- CreateLight (User)\n\n"
            "Material properties allowed:\n"
            "- Metallic\n"
            "- Roughness\n"
            "- NormalFlipY\n"
            "- BaseColor\n\n"
            "Entity types allowed:\n"
            "- Empty\n"
            "- Cube\n"
            "- Sphere\n"
            "- Plane\n\n"
            "Light types allowed:\n"
            "- Directional\n"
            "- Point\n\n"
            "Current editor context:\n" + runtimeContext +
            "User prompt:\n" + userPrompt;
    }

    static std::string BuildConversationContextPrefix(bool hasSelection, const std::string& selectionName, const std::string& selectionType, const std::string& materialName, BlackFlameAccessLevel accessLevel, const std::vector<std::string>& recentMemories)
    {
        std::stringstream ss;
        ss << "Access: " << ((accessLevel == BlackFlameAccessLevel::Admin) ? "Admin" : "User");
        if (hasSelection)
        {
            ss << " | Selected ";
            ss << (selectionType.empty() ? "object" : selectionType);
            if (!selectionName.empty())
                ss << " '" << selectionName << "'";
            if (!materialName.empty())
                ss << " with material '" << materialName << "'";
        }
        else
        {
            ss << " | No selection";
        }
        if (!recentMemories.empty())
            ss << " | Last action: " << recentMemories.back();
        return ss.str();
    }

    static bool IsHybridActionablePrompt(const std::string& prompt)
    {
        return ContainsPromptToken(prompt, "make") ||
            ContainsPromptToken(prompt, "set") ||
            ContainsPromptToken(prompt, "create") ||
            ContainsPromptToken(prompt, "spawn") ||
            ContainsPromptToken(prompt, "darker") ||
            ContainsPromptToken(prompt, "darken") ||
            ContainsPromptToken(prompt, "lighter") ||
            ContainsPromptToken(prompt, "roughness") ||
            ContainsPromptToken(prompt, "metallic") ||
            ContainsPromptToken(prompt, "color") ||
            ContainsPromptToken(prompt, "light") ||
            ContainsPromptToken(prompt, "reset");
    }

    std::string MockHybridExplanation(const std::string& prompt, bool hasCommands, const BlackFlameResponse& engineResponse) const
    {
        const std::string contextPrefix = BuildConversationContextPrefix(
            contextHasSelection,
            contextSelectionName,
            contextSelectionType,
            contextMaterialName,
            contextAccessLevel,
            recentMemories);

        if (!hasCommands)
        {
            if (ContainsPromptToken(prompt, "why") || ContainsPromptToken(prompt, "how") || ContainsPromptToken(prompt, "what"))
                return contextPrefix + "\nI can explain what I see, but I do not sense a concrete action to propose yet.";
            return contextPrefix + "\nThe flame understands the question, but no safe engine action is obvious. Ask for a specific change if you want a proposal.";
        }

        if (ContainsPromptToken(prompt, "darker") || ContainsPromptToken(prompt, "darken"))
            return contextPrefix + "\nThe surface burns too bright. I can temper it by lowering the base color intensity. Shall I apply the change?";
        if (ContainsPromptToken(prompt, "roughness"))
            return contextPrefix + "\nThe material can be tempered by adjusting roughness. I have prepared a safe proposal.";
        if (ContainsPromptToken(prompt, "metallic"))
            return contextPrefix + "\nI can shift the material toward a stronger metallic response. The proposal is ready for your approval.";
        if (ContainsPromptToken(prompt, "create") || ContainsPromptToken(prompt, "spawn") || ContainsPromptToken(prompt, "make"))
            return contextPrefix + "\nI understand the intent and have prepared a safe engine action. Review the proposal, then choose whether to apply it.";
        if (!engineResponse.Explanation.empty())
            return contextPrefix + "\n" + engineResponse.Explanation;
        return contextPrefix + "\nI have prepared a safe proposal. Review it, then decide whether the flame should act.";
    }

    std::string MockResponseJSON(const std::string& prompt) const
    {
        const bool hasSelectionReference = PromptUsesSelectionReference(prompt);
        const bool wantsEntity = ContainsPromptToken(prompt, "entity") || ContainsPromptToken(prompt, "spawn") || ContainsPromptToken(prompt, "cube") || ContainsPromptToken(prompt, "sphere") || ContainsPromptToken(prompt, "plane") || (ContainsPromptToken(prompt, "another") && !lastCreatedEntityType.empty());
        const bool wantsMaterial = ContainsPromptToken(prompt, "roughness") || ContainsPromptToken(prompt, "metallic") || ContainsPromptToken(prompt, "material") || ContainsPromptToken(prompt, "normal flip") || ContainsPromptToken(prompt, "base color") || ContainsPromptToken(prompt, "color") || ContainsPromptToken(prompt, "red") || ContainsPromptToken(prompt, "green") || ContainsPromptToken(prompt, "blue") || ContainsPromptToken(prompt, "darker") || ContainsPromptToken(prompt, "darken");
        const bool wantsLight = ContainsPromptToken(prompt, "light") || ContainsPromptToken(prompt, "directional") || ContainsPromptToken(prompt, "point");
        const bool wantsAllocator = ContainsPromptToken(prompt, "allocator") || ContainsPromptToken(prompt, "fence") || ContainsPromptToken(prompt, "reset");

        std::string code = "// No code generated.";
        std::vector<std::string> changes;
        std::string explanation = "No actionable command matched the prompt.";
        std::vector<std::string> commands;

        if (wantsAllocator)
        {
            code = "WaitForFence(frameFence);\ncommandAllocator->Reset();";
            changes.push_back("Inserted fence wait before allocator reset");
            explanation = "Ensures GPU safety before resetting allocator.";
            commands.push_back("{\"type\":\"ResetAllocatorSafely\",\"access\":\"Admin\"}");
        }

        if (wantsEntity)
        {
            std::string entityType = "Cube";
            if (ContainsPromptToken(prompt, "sphere")) entityType = "Sphere";
            else if (ContainsPromptToken(prompt, "plane")) entityType = "Plane";
            else if (ContainsPromptToken(prompt, "empty")) entityType = "Empty";
            else if (ContainsPromptToken(prompt, "another") && !lastCreatedEntityType.empty()) entityType = lastCreatedEntityType;

            code = "Scene::Create" + entityType + "();";
            changes.push_back("Prepared scene entity creation command");
            explanation = ContainsPromptToken(prompt, "another") && !lastCreatedEntityType.empty()
                ? ("Creates another " + entityType + " based on recent Black Flame memory.")
                : "Creates a visible scene primitive using validated editor-safe commands.";
            commands.push_back("{\"type\":\"CreateEntity\",\"access\":\"User\",\"stringValue\":\"" + EscapeJsonString(entityType) + "\"}");
        }

        if (wantsMaterial)
        {
            if (hasSelectionReference && !contextHasSelection)
            {
                changes.push_back("Material edit was blocked because no object is selected");
                explanation = "Select an object before using pronouns like 'it' or 'that'.";
            }
            else
            {
                std::string property = "Roughness";
                float value = 0.25f;
                float value2 = 0.0f;
                float value3 = 0.0f;
                bool isBaseColor = false;

                if (ContainsPromptToken(prompt, "metallic"))
                {
                    property = "Metallic";
                    value = 1.0f;
                }
                if (ContainsPromptToken(prompt, "roughness"))
                {
                    property = "Roughness";
                    value = 0.15f;
                }
                if (ContainsPromptToken(prompt, "normal flip"))
                {
                    property = "NormalFlipY";
                    value = 1.0f;
                }
                if (ContainsPromptToken(prompt, "darker") || ContainsPromptToken(prompt, "darken"))
                {
                    property = "BaseColor";
                    isBaseColor = true;
                    value = 0.30f;
                    value2 = 0.12f;
                    value3 = 0.12f;
                }
                else if (ContainsPromptToken(prompt, "base color") || ContainsPromptToken(prompt, "color") || ContainsPromptToken(prompt, "red") || ContainsPromptToken(prompt, "green") || ContainsPromptToken(prompt, "blue"))
                {
                    property = "BaseColor";
                    isBaseColor = true;
                    value = ContainsPromptToken(prompt, "red") ? 1.0f : 0.78f;
                    value2 = ContainsPromptToken(prompt, "green") ? 1.0f : 0.80f;
                    value3 = ContainsPromptToken(prompt, "blue") ? 1.0f : 0.84f;
                    if (ContainsPromptToken(prompt, "red") && !ContainsPromptToken(prompt, "green") && !ContainsPromptToken(prompt, "blue"))
                    {
                        value2 = 0.20f;
                        value3 = 0.20f;
                    }
                }

                if (isBaseColor)
                {
                    std::stringstream codeBuilder;
                    codeBuilder << "selectedMaterial->SetBaseColor({ " << value << "f, " << value2 << "f, " << value3 << "f });";
                    code = codeBuilder.str();
                }
                else
                {
                    code = property == "NormalFlipY"
                        ? "graphics.SetFlipNormalGreenChannel(true);"
                        : ("selectedMaterial->SetFloat(\"" + property + "\", " + std::to_string(value) + "f);");
                }

                changes.push_back("Prepared material property update command");
                const std::string target = BuildCurrentTargetDescription();
                explanation = target.empty() ? "Applies a validated material property change to the selected object." : ("Applies a validated material property change to " + target + ".");
                std::stringstream cmd;
                cmd << "{\"type\":\"SetMaterialProperty\",\"access\":\"User\",\"stringValue\":\"" << EscapeJsonString(property) << "\",\"floatValue\":" << value;
                if (isBaseColor)
                    cmd << ",\"floatValue2\":" << value2 << ",\"floatValue3\":" << value3;
                cmd << "}";
                commands.push_back(cmd.str());
            }
        }

        if (wantsLight)
        {
            const bool warm = ContainsPromptToken(prompt, "warm");
            const std::string lightType = ContainsPromptToken(prompt, "point") ? "Point" : "Directional";
            code = "graphics.GetDirectionalLight().intensity = 1.25f;";
            changes.push_back("Prepared light creation/configuration command");
            explanation = "Configures a validated editor lighting command using safe engine-side defaults.";
            std::stringstream cmd;
            cmd << "{\"type\":\"CreateLight\",\"access\":\"User\",\"stringValue\":\"" << lightType << "\",\"floatValue\":1.25,\"floatValue2\":" << (warm ? 1.0f : 1.0f) << ",\"floatValue3\":" << (warm ? 0.85f : 1.0f) << ",\"floatValue4\":" << (warm ? 0.65f : 1.0f) << "}";
            commands.push_back(cmd.str());
        }

        if (changes.empty())
            changes.push_back("No recognized engine-safe action was found");

        std::stringstream ss;
        ss << "{";
        ss << "\"code\":\"" << EscapeJsonString(code) << "\",";
        ss << "\"changes\":[";
        for (size_t i = 0; i < changes.size(); ++i)
        {
            if (i > 0) ss << ",";
            ss << "\"" << EscapeJsonString(changes[i]) << "\"";
        }
        ss << "],";
        ss << "\"explanation\":\"" << EscapeJsonString(explanation) << "\",";
        ss << "\"commands\":[";
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (i > 0) ss << ",";
            ss << commands[i];
        }
        ss << "]}";
        return ss.str();
    }

    BlackFlameChatResponse SendToAI_Conversation(const std::string& rawUserPrompt) const
    {
        BlackFlameChatResponse response{};
        response.Message = MockConversation(rawUserPrompt);
        return response;
    }

    std::string MockConversation(const std::string& prompt) const
    {
        const std::string contextPrefix = BuildConversationContextPrefix(
            contextHasSelection,
            contextSelectionName,
            contextSelectionType,
            contextMaterialName,
            contextAccessLevel,
            recentMemories);

        if (ContainsPromptToken(prompt, "why") && (ContainsPromptToken(prompt, "shiny") || ContainsPromptToken(prompt, "glossy") || ContainsPromptToken(prompt, "reflect")))
            return contextPrefix + "\nThe surface likely has low roughness or strong lighting, so reflections are appearing sharper than expected.";
        if (ContainsPromptToken(prompt, "what are you doing") || ContainsPromptToken(prompt, "what are you") || ContainsPromptToken(prompt, "what is happening"))
            return contextPrefix + "\nThe Black Flame is watching your scene, tracking context, and waiting for either a command or a question.";
        if (ContainsPromptToken(prompt, "lighting") || ContainsPromptToken(prompt, "light"))
            return contextPrefix + "\nLighting defines how strongly surfaces reveal form. Higher intensity and lower roughness usually make materials feel more reflective.";
        if (ContainsPromptToken(prompt, "hello") || ContainsPromptToken(prompt, "hi") || ContainsPromptToken(prompt, "hey"))
            return contextPrefix + "\nThe Black Flame listens. Ask about the scene, materials, lighting, or what changed most recently.";
        if (ContainsPromptToken(prompt, "thoughts") || ContainsPromptToken(prompt, "looks off") || ContainsPromptToken(prompt, "weird"))
            return contextPrefix + "\nSomething may be unbalanced between material values, lighting, or scene context. If you want, ask specifically about color, roughness, metallic, or lighting.";
        if (contextHasSelection)
            return contextPrefix + "\nYou can speak casually. Ask what the selected object is doing, why it looks a certain way, or how to improve it.";
        return contextPrefix + "\nThe Black Flame listens... but the intent is unclear. Ask about the current scene, selection, materials, or lighting.";
    }

    std::string SendToAI_Engine(const std::string& prompt, const std::string& rawUserPrompt) const
    {
        (void)prompt;
        return MockResponseJSON(rawUserPrompt);
    }

    BlackFlameResponse BuildHybridResponse(const std::string& rawUserPrompt) const
    {
        BlackFlameResponse hybrid{};
        if (IsHybridActionablePrompt(rawUserPrompt))
        {
            BlackFlameAI temp = *this;
            const std::string fullPrompt = BuildBlackFlamePrompt(rawUserPrompt, BuildRuntimeContextBlock());
            const std::string jsonResponse = temp.SendToAI_Engine(fullPrompt, rawUserPrompt);
            if (temp.ParseResponse(jsonResponse))
                hybrid = temp.GetLastResponse();
        }

        hybrid.IsConversation = false;
        hybrid.ChatMessage.clear();
        hybrid.HasProposedCommands = !hybrid.Commands.empty();
        hybrid.ProposalExplanation = MockHybridExplanation(rawUserPrompt, hybrid.HasProposedCommands, hybrid);
        if (!hybrid.HasProposedCommands)
            hybrid.Explanation = hybrid.ProposalExplanation;
        return hybrid;
    }

    static bool TryParseQuotedString(const std::string& text, size_t& pos, std::string& outValue)
    {
        if (pos >= text.size() || text[pos] != '"')
            return false;
        ++pos;
        outValue.clear();
        while (pos < text.size())
        {
            const char c = text[pos++];
            if (c == '\\')
            {
                if (pos >= text.size())
                    return false;
                const char escaped = text[pos++];
                switch (escaped)
                {
                case 'n': outValue += '\n'; break;
                case 'r': outValue += '\r'; break;
                case 't': outValue += '\t'; break;
                case '\\': outValue += '\\'; break;
                case '"': outValue += '"'; break;
                default: outValue += escaped; break;
                }
                continue;
            }
            if (c == '"')
                return true;
            outValue += c;
        }
        return false;
    }

    static bool TryExtractFieldRegion(const std::string& jsonStr, const std::string& key, size_t& valuePos)
    {
        const size_t keyPos = jsonStr.find("\"" + key + "\"");
        if (keyPos == std::string::npos)
            return false;
        valuePos = jsonStr.find(':', keyPos);
        if (valuePos == std::string::npos)
            return false;
        ++valuePos;
        while (valuePos < jsonStr.size() && std::isspace(static_cast<unsigned char>(jsonStr[valuePos])))
            ++valuePos;
        return valuePos < jsonStr.size();
    }

    static bool TryExtractStringField(const std::string& jsonStr, const std::string& key, std::string& outValue)
    {
        size_t valuePos = 0;
        if (!TryExtractFieldRegion(jsonStr, key, valuePos))
            return false;
        return TryParseQuotedString(jsonStr, valuePos, outValue);
    }

    static bool TryExtractStringArrayField(const std::string& jsonStr, const std::string& key, std::vector<std::string>& outValues)
    {
        size_t valuePos = 0;
        if (!TryExtractFieldRegion(jsonStr, key, valuePos) || valuePos >= jsonStr.size() || jsonStr[valuePos] != '[')
            return false;
        ++valuePos;
        outValues.clear();
        while (valuePos < jsonStr.size())
        {
            while (valuePos < jsonStr.size() && std::isspace(static_cast<unsigned char>(jsonStr[valuePos])))
                ++valuePos;
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ']')
            {
                ++valuePos;
                return true;
            }
            std::string value;
            if (!TryParseQuotedString(jsonStr, valuePos, value))
                return false;
            outValues.push_back(value);
            while (valuePos < jsonStr.size() && std::isspace(static_cast<unsigned char>(jsonStr[valuePos])))
                ++valuePos;
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ',')
            {
                ++valuePos;
                continue;
            }
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ']')
            {
                ++valuePos;
                return true;
            }
            return false;
        }
        return false;
    }

    static bool TryExtractFloatField(const std::string& jsonStr, const std::string& key, float& outValue)
    {
        size_t valuePos = 0;
        if (!TryExtractFieldRegion(jsonStr, key, valuePos))
            return false;
        size_t endPos = valuePos;
        while (endPos < jsonStr.size())
        {
            const char c = jsonStr[endPos];
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.'))
                break;
            ++endPos;
        }
        if (endPos == valuePos)
            return false;
        outValue = std::stof(jsonStr.substr(valuePos, endPos - valuePos));
        return true;
    }

    static bool TryExtractIntField(const std::string& jsonStr, const std::string& key, int& outValue)
    {
        size_t valuePos = 0;
        if (!TryExtractFieldRegion(jsonStr, key, valuePos))
            return false;
        size_t endPos = valuePos;
        while (endPos < jsonStr.size())
        {
            const char c = jsonStr[endPos];
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+'))
                break;
            ++endPos;
        }
        if (endPos == valuePos)
            return false;
        outValue = std::stoi(jsonStr.substr(valuePos, endPos - valuePos));
        return true;
    }

    static bool TryExtractCommands(const std::string& jsonStr, std::vector<BlackFlameCommand>& outCommands)
    {
        size_t valuePos = 0;
        if (!TryExtractFieldRegion(jsonStr, "commands", valuePos) || valuePos >= jsonStr.size() || jsonStr[valuePos] != '[')
            return false;
        ++valuePos;
        outCommands.clear();
        while (valuePos < jsonStr.size())
        {
            while (valuePos < jsonStr.size() && std::isspace(static_cast<unsigned char>(jsonStr[valuePos])))
                ++valuePos;
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ']')
            {
                ++valuePos;
                return true;
            }
            if (valuePos >= jsonStr.size() || jsonStr[valuePos] != '{')
                return false;

            const size_t objectStart = valuePos;
            int depth = 0;
            do
            {
                if (jsonStr[valuePos] == '{') ++depth;
                else if (jsonStr[valuePos] == '}') --depth;
                ++valuePos;
            } while (valuePos < jsonStr.size() && depth > 0);

            if (depth != 0)
                return false;

            const std::string objectJson = jsonStr.substr(objectStart, valuePos - objectStart);
            BlackFlameCommand cmd{};
            std::string type;
            std::string access;
            if (!TryExtractStringField(objectJson, "type", type) || !TryExtractStringField(objectJson, "access", access))
                return false;

            if (type == "ResetAllocatorSafely") cmd.Type = BlackFlameCommandType::ResetAllocatorSafely;
            else if (type == "CreateEntity") cmd.Type = BlackFlameCommandType::CreateEntity;
            else if (type == "SetMaterialProperty") cmd.Type = BlackFlameCommandType::SetMaterialProperty;
            else if (type == "CreateLight") cmd.Type = BlackFlameCommandType::CreateLight;
            else cmd.Type = BlackFlameCommandType::Unknown;

            cmd.RequiredAccess = (access == "Admin") ? BlackFlameAccessLevel::Admin : BlackFlameAccessLevel::User;
            TryExtractStringField(objectJson, "stringValue", cmd.StringValue);
            TryExtractStringField(objectJson, "stringValue2", cmd.StringValue2);
            TryExtractFloatField(objectJson, "floatValue", cmd.FloatValue);
            TryExtractFloatField(objectJson, "floatValue2", cmd.FloatValue2);
            TryExtractFloatField(objectJson, "floatValue3", cmd.FloatValue3);
            TryExtractFloatField(objectJson, "floatValue4", cmd.FloatValue4);
            TryExtractIntField(objectJson, "intValue", cmd.IntValue);
            outCommands.push_back(cmd);

            while (valuePos < jsonStr.size() && std::isspace(static_cast<unsigned char>(jsonStr[valuePos])))
                ++valuePos;
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ',')
            {
                ++valuePos;
                continue;
            }
            if (valuePos < jsonStr.size() && jsonStr[valuePos] == ']')
            {
                ++valuePos;
                return true;
            }
        }
        return false;
    }

    bool ParseResponse(const std::string& jsonStr)
    {
        BlackFlameResponse parsedResponse{};
        if (!TryExtractStringField(jsonStr, "code", parsedResponse.GeneratedCode))
            return false;
        if (!TryExtractStringArrayField(jsonStr, "changes", parsedResponse.Changes))
            return false;
        if (!TryExtractStringField(jsonStr, "explanation", parsedResponse.Explanation))
            return false;
        if (!TryExtractCommands(jsonStr, parsedResponse.Commands))
            return false;

        for (const BlackFlameCommand& cmd : parsedResponse.Commands)
        {
            switch (cmd.Type)
            {
            case BlackFlameCommandType::ResetAllocatorSafely:
                parsedResponse.OriginalCode = "commandAllocator->Reset();";
                break;
            case BlackFlameCommandType::SetMaterialProperty:
                if (cmd.StringValue == "NormalFlipY")
                    parsedResponse.OriginalCode = "graphics.GetFlipNormalGreenChannel();";
                else if (cmd.StringValue == "BaseColor")
                    parsedResponse.OriginalCode = "selectedMaterial->SetBaseColor(currentColor);";
                else
                    parsedResponse.OriginalCode = "selectedMaterial->SetFloat(\"" + cmd.StringValue + "\", currentValue);";
                break;
            case BlackFlameCommandType::CreateEntity:
                parsedResponse.OriginalCode = "// Scene unchanged";
                break;
            case BlackFlameCommandType::CreateLight:
                parsedResponse.OriginalCode = "graphics.GetDirectionalLight();";
                break;
            case BlackFlameCommandType::Unknown:
            default:
                break;
            }
            if (!parsedResponse.OriginalCode.empty())
                break;
        }

        parsedResponse.DiffView = BuildSimpleDiff(parsedResponse.OriginalCode, parsedResponse.GeneratedCode);
        parsedResponse.IsConversation = false;
        parsedResponse.ChatMessage.clear();
        parsedResponse.HasProposedCommands = false;
        parsedResponse.ProposalExplanation.clear();
        lastResponse = std::move(parsedResponse);
        return true;
    }

    static BlackFlameCommand MakeSetMaterialPropertyCommand(const std::string& property, float value, float value2 = 0.0f, float value3 = 0.0f)
    {
        BlackFlameCommand cmd{};
        cmd.Type = BlackFlameCommandType::SetMaterialProperty;
        cmd.RequiredAccess = BlackFlameAccessLevel::User;
        cmd.StringValue = property;
        cmd.FloatValue = value;
        cmd.FloatValue2 = value2;
        cmd.FloatValue3 = value3;
        return cmd;
    }

    bool HasSuggestionMessage(const std::string& message) const
    {
        for (const BlackFlameSuggestion& suggestion : activeSuggestions)
        {
            if (suggestion.Message == message)
                return true;
        }
        return false;
    }

    void EvaluateSuggestions()
    {
        if (activeSuggestions.size() >= 3)
            return;

        if (contextHasSelection && !selectedHasMaterial)
        {
            static constexpr const char* kNoMaterialSuggestion = "This object lacks form... it has no material.";
            if (!HasSuggestionMessage(kNoMaterialSuggestion))
            {
                BlackFlameSuggestion suggestion{};
                suggestion.Message = kNoMaterialSuggestion;
                suggestion.IsWarning = true;
                activeSuggestions.push_back(std::move(suggestion));
                suggestionCooldown = 3.0f;
                AccumulatePulse(visualState.DenyPulse, 0.2f);
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Glitch, 0.7f);
            }
            return;
        }

        if (contextHasSelection && selectedHasMaterial && selectedMaterialRoughness < 0.1f)
        {
            static constexpr const char* kRoughnessSuggestion = "The surface burns too sharply... I can soften it.";
            if (!HasSuggestionMessage(kRoughnessSuggestion))
            {
                BlackFlameSuggestion suggestion{};
                suggestion.Message = kRoughnessSuggestion;
                suggestion.ProposedCommands.push_back(MakeSetMaterialPropertyCommand("Roughness", 0.4f));
                activeSuggestions.push_back(std::move(suggestion));
                suggestionCooldown = 2.5f;
                AccumulatePulse(visualState.AdminPulse, 0.2f);
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Arcane, 0.6f);
            }
            return;
        }

        size_t instabilityMentions = 0;
        for (const std::string& memory : recentMemories)
        {
            if (memory.find("warning") != std::string::npos || memory.find("issue") != std::string::npos || memory.find("mismatch") != std::string::npos)
                ++instabilityMentions;
        }

        if (instabilityMentions >= 2)
        {
            static constexpr const char* kInstabilitySuggestion = "The flame senses repeated instability in the engine... proceed carefully.";
            if (!HasSuggestionMessage(kInstabilitySuggestion))
            {
                BlackFlameSuggestion suggestion{};
                suggestion.Message = kInstabilitySuggestion;
                suggestion.IsWarning = true;
                activeSuggestions.push_back(std::move(suggestion));
                suggestionCooldown = 4.0f;
                AccumulatePulse(visualState.DenyPulse, 0.2f);
                BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Glitch, 0.8f);
            }
        }
    }

    std::string currentPrompt;
    std::string pendingPrompt;
    BlackFlameResponse lastResponse;
    bool hasResponse = false;
    BlackFlameState state = BlackFlameState::Idle;
    BlackFlameVisualProfile visualProfile = BlackFlameVisualProfile::Normal;
    BlackFlameVisualState visualState{};
    std::chrono::steady_clock::time_point lastVisualUpdate = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point stateUntil{};
    std::chrono::steady_clock::time_point thinkingStartedAt{};
    std::chrono::milliseconds thinkingDuration{ 350 };
    bool contextHasSelection = false;
    std::string contextSelectionName;
    std::string contextSelectionType;
    std::string contextMaterialName;
    std::vector<std::string> recentMemories;
    std::string lastCreatedEntityType;
    BlackFlameAccessLevel contextAccessLevel = BlackFlameAccessLevel::Admin;
    BlackFlameMode currentMode = BlackFlameMode::Engine;
    bool selectedHasMaterial = false;
    float selectedMaterialRoughness = 1.0f;
    float selectedMaterialMetallic = 0.0f;
    int contextLightCount = 0;
    std::vector<BlackFlameSuggestion> activeSuggestions;
    float suggestionCooldown = 0.0f;
    std::chrono::steady_clock::time_point lastRecommendationNudgeAt{};
};
