#pragma once
#pragma once

#include <cstdint>

enum class GameplayAudioEvent : uint8_t
{
    NodeActivated = 0,
    NodeWarning,
    NodeDecayed,
    CoreUnlocked,
    CoreStabilized,
    ExitOpened,
    EscapeTriggered,
    FailureTriggered
};

void GA_Play(GameplayAudioEvent eventType);
float GA_GetMasterVolume();
void GA_SetMasterVolume(float volume);
void GA_SetTension(float tension);
void GA_Reset();
