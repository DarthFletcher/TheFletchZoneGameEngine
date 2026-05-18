#include "GameplayAudio.h"

#include "GameplayAudio.h"

#include "BlackFlameAudio.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace
{
    using Clock = std::chrono::steady_clock;

    struct GameplayAudioState
    {
        bool loopsStarted = false;
        float currentTension = 0.0f;
        float masterVolume = 0.60f;
        GameplayAudioMood mood = GameplayAudioMood::Calm;
        std::array<Clock::time_point, 11> lastPlayTimes{};
    };

    struct AudioMoodProfile
    {
        float loopWarm = 0.12f;
        float loopArcane = 0.05f;
        float loopGlitch = 0.0f;
        float tensionBias = 0.0f;
        float glitchBias = 0.0f;
        float focusBias = 0.0f;
        float eventGain = 1.0f;
    };

    GameplayAudioState g_GameplayAudioState{};

    constexpr std::chrono::milliseconds kGameplayEventCooldown{ 100 };

    AudioMoodProfile GetAudioMoodProfile()
    {
        switch (g_GameplayAudioState.mood)
        {
        case GameplayAudioMood::Tension:
            return { 0.11f, 0.08f, 0.03f, 0.10f, 0.08f, -0.02f, 1.05f };
        case GameplayAudioMood::Critical:
            return { 0.08f, 0.10f, 0.06f, 0.22f, 0.16f, -0.06f, 1.12f };
        case GameplayAudioMood::Calm:
        default:
            return { 0.14f, 0.04f, 0.0f, -0.08f, -0.05f, 0.05f, 0.94f };
        }
    }

    constexpr size_t GameplayAudioEventIndex(GameplayAudioEvent eventType)
    {
        return static_cast<size_t>(eventType);
    }

    float GetEventVolumeScale(GameplayAudioEvent eventType)
    {
        switch (eventType)
        {
        case GameplayAudioEvent::NodeActivated: return 0.60f;
        case GameplayAudioEvent::SlowNodeStarted: return 0.52f;
        case GameplayAudioEvent::SlowNodeCompleted: return 0.72f;
        case GameplayAudioEvent::NodeWarning: return 0.40f;
        case GameplayAudioEvent::NodeDecayed: return 0.70f;
        case GameplayAudioEvent::CoreUnlocked: return 1.00f;
        case GameplayAudioEvent::CoreStabilized: return 1.00f;
        case GameplayAudioEvent::ExitOpened: return 0.90f;
        case GameplayAudioEvent::EscapeTriggered: return 1.10f;
        case GameplayAudioEvent::FailureTriggered: return 1.15f;
        case GameplayAudioEvent::CampaignCompleted: return 1.22f;
        default: return 1.00f;
        }
    }

    bool CanPlayEvent(GameplayAudioEvent eventType)
    {
        const auto now = Clock::now();
        auto& lastPlayTime = g_GameplayAudioState.lastPlayTimes[GameplayAudioEventIndex(eventType)];
        if (lastPlayTime != Clock::time_point{} && now - lastPlayTime < kGameplayEventCooldown)
            return false;
        lastPlayTime = now;
        return true;
    }

    void EnsureGameplayLoopsStarted()
    {
        if (g_GameplayAudioState.loopsStarted)
            return;

        const AudioMoodProfile profile = GetAudioMoodProfile();
        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Warm, profile.loopWarm * g_GameplayAudioState.masterVolume);
        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Arcane, profile.loopArcane * g_GameplayAudioState.masterVolume);
        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Glitch, profile.loopGlitch * g_GameplayAudioState.masterVolume);
        g_GameplayAudioState.loopsStarted = true;
    }
}

void GA_Play(GameplayAudioEvent eventType)
{
    if (!CanPlayEvent(eventType))
        return;

    const AudioMoodProfile profile = GetAudioMoodProfile();
    const float volume = g_GameplayAudioState.masterVolume * GetEventVolumeScale(eventType) * profile.eventGain;

    switch (eventType)
    {
    case GameplayAudioEvent::NodeActivated:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Execute, g_GameplayAudioState.mood == GameplayAudioMood::Calm ? BlackFlameSoundStyle::Clean : BlackFlameSoundStyle::Warm, 0.38f * volume);
        break;
    case GameplayAudioEvent::SlowNodeStarted:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Arcane, 0.32f * volume);
        break;
    case GameplayAudioEvent::SlowNodeCompleted:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Arcane, 0.48f * volume);
        break;
    case GameplayAudioEvent::NodeWarning:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, g_GameplayAudioState.mood == GameplayAudioMood::Calm ? BlackFlameSoundStyle::Warm : BlackFlameSoundStyle::Glitch, 0.28f * volume);
        break;
    case GameplayAudioEvent::NodeDecayed:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.46f * volume);
        break;
    case GameplayAudioEvent::CoreUnlocked:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::HighConfidence, g_GameplayAudioState.mood == GameplayAudioMood::Critical ? BlackFlameSoundStyle::Clean : BlackFlameSoundStyle::Arcane, 0.58f * volume);
        break;
    case GameplayAudioEvent::CoreStabilized:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Clean, 0.62f * volume);
        break;
    case GameplayAudioEvent::ExitOpened:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Execute, g_GameplayAudioState.mood == GameplayAudioMood::Calm ? BlackFlameSoundStyle::Warm : BlackFlameSoundStyle::Arcane, 0.48f * volume);
        break;
    case GameplayAudioEvent::EscapeTriggered:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, g_GameplayAudioState.mood == GameplayAudioMood::Critical ? BlackFlameSoundStyle::Clean : BlackFlameSoundStyle::Arcane, 0.70f * volume);
        break;
    case GameplayAudioEvent::FailureTriggered:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.82f * volume);
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Arcane, 0.36f * volume);
        break;
    case GameplayAudioEvent::CampaignCompleted:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::HighConfidence, BlackFlameSoundStyle::Clean, 0.78f * volume);
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Arcane, 0.56f * volume);
        break;
    default:
        break;
    }
}

float GA_GetMasterVolume()
{
    return g_GameplayAudioState.masterVolume;
}

void GA_SetMasterVolume(float volume)
{
    g_GameplayAudioState.masterVolume = std::clamp(volume, 0.0f, 1.0f);
}

void GA_SetMood(GameplayAudioMood mood)
{
    if (g_GameplayAudioState.mood == mood)
        return;
    const float preservedTension = g_GameplayAudioState.currentTension;
    g_GameplayAudioState.mood = mood;
    if (g_GameplayAudioState.loopsStarted)
    {
        BlackFlameAudio::Get().StopLoop();
        g_GameplayAudioState.loopsStarted = false;
    }
    EnsureGameplayLoopsStarted();
    GA_SetTension(preservedTension);
}

void GA_SetTension(float tension)
{
    const AudioMoodProfile profile = GetAudioMoodProfile();
    const float clampedTension = std::clamp(tension + profile.tensionBias, 0.0f, 1.0f);
    EnsureGameplayLoopsStarted();
    g_GameplayAudioState.currentTension = clampedTension;

    const float mixScale = 0.70f * g_GameplayAudioState.masterVolume;
    const float execPulse = (0.06f + (1.0f - clampedTension) * 0.11f) * mixScale;
    const float adminPulse = (0.05f + (1.0f - clampedTension) * 0.07f) * mixScale;
    const float denyPulse = (0.08f + clampedTension * 0.28f) * mixScale;
    const float focusPulse = ((1.0f - clampedTension) * (0.10f + profile.focusBias)) * mixScale;
    const float glitchIntensity = std::clamp(clampedTension * (0.65f + profile.glitchBias), 0.0f, 1.2f) * mixScale;
    const float stability = 1.0f - clampedTension;

    BlackFlameAudio::Get().UpdateReactiveMix(execPulse, adminPulse, denyPulse, focusPulse, glitchIntensity, stability, 1.0f / 60.0f);
}

void GA_Reset()
{
    BlackFlameAudio::Get().StopLoop();
    g_GameplayAudioState = {};
}
