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
        std::array<Clock::time_point, 7> lastPlayTimes{};
    };

    GameplayAudioState g_GameplayAudioState{};

    constexpr std::chrono::milliseconds kGameplayEventCooldown{ 100 };

    constexpr size_t GameplayAudioEventIndex(GameplayAudioEvent eventType)
    {
        return static_cast<size_t>(eventType);
    }

    float GetEventVolumeScale(GameplayAudioEvent eventType)
    {
        switch (eventType)
        {
        case GameplayAudioEvent::NodeActivated: return 0.60f;
        case GameplayAudioEvent::NodeWarning: return 0.40f;
        case GameplayAudioEvent::NodeDecayed: return 0.70f;
        case GameplayAudioEvent::CoreUnlocked: return 1.00f;
        case GameplayAudioEvent::CoreStabilized: return 1.00f;
        case GameplayAudioEvent::ExitOpened: return 0.90f;
        case GameplayAudioEvent::EscapeTriggered: return 1.10f;
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

        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Warm, 0.14f * g_GameplayAudioState.masterVolume);
        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Arcane, 0.05f * g_GameplayAudioState.masterVolume);
        BlackFlameAudio::Get().PlayLoop(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Glitch, 0.0f);
        g_GameplayAudioState.loopsStarted = true;
    }
}

void GA_Play(GameplayAudioEvent eventType)
{
    if (!CanPlayEvent(eventType))
        return;

    const float volume = g_GameplayAudioState.masterVolume * GetEventVolumeScale(eventType);

    switch (eventType)
    {
    case GameplayAudioEvent::NodeActivated:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Execute, BlackFlameSoundStyle::Warm, 0.38f * volume);
        break;
    case GameplayAudioEvent::NodeWarning:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::SuggestionAppear, BlackFlameSoundStyle::Glitch, 0.28f * volume);
        break;
    case GameplayAudioEvent::NodeDecayed:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Denied, BlackFlameSoundStyle::Glitch, 0.46f * volume);
        break;
    case GameplayAudioEvent::CoreUnlocked:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::HighConfidence, BlackFlameSoundStyle::Arcane, 0.58f * volume);
        break;
    case GameplayAudioEvent::CoreStabilized:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Clean, 0.62f * volume);
        break;
    case GameplayAudioEvent::ExitOpened:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Execute, BlackFlameSoundStyle::Arcane, 0.48f * volume);
        break;
    case GameplayAudioEvent::EscapeTriggered:
        BlackFlameAudio::Get().Play(BlackFlameSoundEvent::Ready, BlackFlameSoundStyle::Arcane, 0.70f * volume);
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

void GA_SetTension(float tension)
{
    const float clampedTension = std::clamp(tension, 0.0f, 1.0f);
    EnsureGameplayLoopsStarted();
    g_GameplayAudioState.currentTension = clampedTension;

    const float mixScale = 0.70f * g_GameplayAudioState.masterVolume;
    const float execPulse = (0.06f + (1.0f - clampedTension) * 0.11f) * mixScale;
    const float adminPulse = (0.05f + (1.0f - clampedTension) * 0.07f) * mixScale;
    const float denyPulse = (0.08f + clampedTension * 0.28f) * mixScale;
    const float focusPulse = ((1.0f - clampedTension) * 0.10f) * mixScale;
    const float glitchIntensity = clampedTension * 0.65f * mixScale;
    const float stability = 1.0f - clampedTension;

    BlackFlameAudio::Get().UpdateReactiveMix(execPulse, adminPulse, denyPulse, focusPulse, glitchIntensity, stability, 1.0f / 60.0f);
}

void GA_Reset()
{
    BlackFlameAudio::Get().StopLoop();
    g_GameplayAudioState = {};
}
