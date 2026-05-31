#pragma once
#pragma once

#include <cstdint>

enum class GameplayAudioEvent : uint8_t
{
    NodeActivated = 0,
    SlowNodeStarted,
    SlowNodeCompleted,
    NodeWarning,
    NodeDecayed,
    CoreUnlocked,
    CoreStabilized,
    ExitOpened,
    EscapeTriggered,
    FailureTriggered,
    CampaignCompleted,
    VaultLordDetected,
    VaultLordThreatPulse,
    Count
};

enum class GameplayAudioMood : uint8_t
{
    Calm = 0,
    Tension,
    Critical
};

void GA_Play(GameplayAudioEvent eventType);
float GA_GetMasterVolume();
void GA_SetMasterVolume(float volume);
void GA_SetMood(GameplayAudioMood mood);
void GA_SetTension(float tension);
void GA_Reset();
