#pragma once

#include "logger.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "xaudio2.lib")

enum class BlackFlameSoundEvent : uint8_t
{
    ThinkingStart = 0,
    ThinkingLoop,
    Ready,
    Execute,
    Denied,
    HighConfidence,
    SuggestionAppear
};

enum class BlackFlameSoundStyle : uint8_t
{
    Clean = 0,
    Warm,
    Arcane,
    Glitch
};

class BlackFlameAudio
{
public:
    struct BlackFlameAudioParams
    {
        float Volume = 1.0f;
        float Pitch = 1.0f;
    };

    static BlackFlameAudio& Get()
    {
        static BlackFlameAudio instance;
        return instance;
    }

    void Play(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style = BlackFlameSoundStyle::Warm, float intensity = 1.0f)
    {
        std::lock_guard lock(m_Mutex);
        const float clampedIntensity = std::clamp(intensity, 0.0f, 1.5f);
        const auto now = Clock::now();
        const auto key = MakeKey(eventType, style);
        const auto cooldown = GetCooldown(eventType, clampedIntensity);
        const auto it = m_LastPlayTimes.find(key);
        if (it != m_LastPlayTimes.end() && now - it->second < cooldown)
            return;
        m_LastPlayTimes[key] = now;

        Logger::Log(LogLevel::Debug,
            std::string("[BlackFlameAudio] Event=") + EventLabel(eventType) + " Style=" + StyleLabel(style),
            "BlackFlame");

        if (!EnsureInitialized())
            return;

        const AudioClip* clip = GetClip(eventType, style);
        if (!clip || clip->Data.empty())
            return;

        PlayClip(*clip, false, eventType, style, clampedIntensity);
    }

    void PlayLoop(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style = BlackFlameSoundStyle::Warm, float intensity = 1.0f)
    {
        std::lock_guard lock(m_Mutex);
        const float clampedIntensity = std::clamp(intensity, 0.0f, 1.5f);
        const uint32_t key = MakeKey(eventType, style);
        if (m_LoopVoices.contains(key))
            return;

        if (!EnsureInitialized())
            return;

        const AudioClip* clip = GetClip(eventType, style);
        if (!clip || clip->Data.empty())
            return;

        IXAudio2SourceVoice* voice = PlayClip(*clip, true, eventType, style, clampedIntensity);
        if (!voice)
            return;

        m_LoopVoices.emplace(key, voice);
    }

    void UpdateReactiveMix(float execPulse, float adminPulse, float denyPulse, float focusPulse, float glitchIntensity, float stability, float dt)
    {
        std::lock_guard lock(m_Mutex);
        const uint32_t baseKey = MakeKey(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Warm);
        const uint32_t arcaneKey = MakeKey(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Arcane);
        const uint32_t glitchKey = MakeKey(BlackFlameSoundEvent::ThinkingLoop, BlackFlameSoundStyle::Glitch);

        float instability = std::clamp((1.0f - stability) + glitchIntensity * 0.25f, 0.0f, 1.5f);
        float baseTarget = std::clamp(0.20f + execPulse * 0.12f + adminPulse * 0.08f + focusPulse * 0.04f, 0.0f, 0.40f);
        float arcaneTarget = std::clamp(adminPulse * 0.42f + execPulse * 0.05f, 0.0f, 0.50f);
        float glitchTarget = std::clamp(denyPulse * 0.45f + instability * 0.12f, 0.0f, 0.60f);

        NormalizeLoopVolumes(baseTarget, arcaneTarget, glitchTarget, 1.2f);

        UpdateLoopLayer(baseKey, baseTarget, std::clamp(0.96f + execPulse * 0.05f + focusPulse * 0.03f, 0.85f, 1.18f), dt);
        UpdateLoopLayer(arcaneKey, arcaneTarget, std::clamp(1.00f + adminPulse * 0.08f, 0.90f, 1.22f), dt);
        UpdateLoopLayer(glitchKey, glitchTarget, std::clamp(0.92f + denyPulse * 0.08f - instability * 0.04f, 0.78f, 1.12f), dt);
    }

    void StopLoop()
    {
        std::lock_guard lock(m_Mutex);
        StopLoopLocked();
    }

private:
    using Clock = std::chrono::steady_clock;

    struct AudioClip
    {
        WAVEFORMATEX Format{};
        std::vector<uint8_t> Data;
    };

    struct VoiceCallback final : public IXAudio2VoiceCallback
    {
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}

        void STDMETHODCALLTYPE OnBufferEnd(void* context) override
        {
            auto* voice = static_cast<IXAudio2SourceVoice*>(context);
            if (voice)
                voice->DestroyVoice();
        }
    };

    struct LoopMixState
    {
        float CurrentVolume = 0.0f;
        float CurrentPitch = 1.0f;
        float PitchJitter = 0.0f;
    };

    BlackFlameAudio() = default;

    ~BlackFlameAudio()
    {
        std::lock_guard lock(m_Mutex);
        StopLoopLocked();
        if (m_MasteringVoice)
        {
            m_MasteringVoice->DestroyVoice();
            m_MasteringVoice = nullptr;
        }
        if (m_XAudio)
        {
            m_XAudio->Release();
            m_XAudio = nullptr;
        }
    }

    static uint32_t MakeKey(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style)
    {
        return (static_cast<uint32_t>(eventType) << 8u) | static_cast<uint32_t>(style);
    }

    static std::chrono::milliseconds GetCooldown(BlackFlameSoundEvent eventType, float intensity)
    {
        switch (eventType)
        {
        case BlackFlameSoundEvent::ThinkingLoop:
            return std::chrono::milliseconds(1400);
        case BlackFlameSoundEvent::HighConfidence:
            return std::chrono::milliseconds(1800);
        case BlackFlameSoundEvent::SuggestionAppear:
            return std::chrono::milliseconds(900);
        case BlackFlameSoundEvent::Denied:
            return std::chrono::milliseconds(500);
        default:
            return std::chrono::milliseconds(intensity > 1.0f ? 300 : 220);
        }
    }

    bool EnsureInitialized()
    {
        if (m_XAudio && m_MasteringVoice)
            return true;

        const HRESULT hr = XAudio2Create(&m_XAudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr) || !m_XAudio)
        {
            Logger::Log(LogLevel::Warning, "[BlackFlameAudio] XAudio2Create failed.", "BlackFlame");
            m_XAudio = nullptr;
            return false;
        }

        const HRESULT hrMaster = m_XAudio->CreateMasteringVoice(&m_MasteringVoice);
        if (FAILED(hrMaster) || !m_MasteringVoice)
        {
            Logger::Log(LogLevel::Warning, "[BlackFlameAudio] CreateMasteringVoice failed.", "BlackFlame");
            if (m_XAudio)
            {
                m_XAudio->Release();
                m_XAudio = nullptr;
            }
            m_MasteringVoice = nullptr;
            return false;
        }

        return true;
    }

    static BlackFlameAudioParams GetParams(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style, float intensity, bool loop)
    {
        BlackFlameAudioParams params{};
        switch (eventType)
        {
        case BlackFlameSoundEvent::Execute:
            params = { 0.88f, style == BlackFlameSoundStyle::Arcane ? 1.04f : 1.0f };
            break;
        case BlackFlameSoundEvent::Ready:
            params = { 0.45f, 1.0f };
            break;
        case BlackFlameSoundEvent::SuggestionAppear:
            params = { 0.30f, style == BlackFlameSoundStyle::Glitch ? 0.94f : 1.0f };
            break;
        case BlackFlameSoundEvent::HighConfidence:
            params = { 0.55f, 1.08f };
            break;
        case BlackFlameSoundEvent::Denied:
            params = { 0.72f, 0.92f };
            break;
        case BlackFlameSoundEvent::ThinkingLoop:
            if (style == BlackFlameSoundStyle::Warm)
                params = { loop ? 0.20f : 0.24f, 0.98f };
            else if (style == BlackFlameSoundStyle::Arcane)
                params = { 0.00f, 1.01f };
            else
                params = { 0.00f, 0.92f };
            break;
        case BlackFlameSoundEvent::ThinkingStart:
        default:
            params = { 0.46f, style == BlackFlameSoundStyle::Arcane ? 1.02f : 1.0f };
            break;
        }

        params.Volume = std::clamp(params.Volume * (0.88f + intensity * 0.12f), 0.0f, 1.0f);
        params.Pitch = std::clamp(params.Pitch + (intensity - 1.0f) * 0.05f, 0.70f, 1.35f);
        return params;
    }

    static std::filesystem::path ResolveAssetPath(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style)
    {
        const std::filesystem::path base = std::filesystem::path("Assets") / "Audio";
        switch (eventType)
        {
        case BlackFlameSoundEvent::ThinkingLoop:
            if (style == BlackFlameSoundStyle::Arcane)
                return base / "arcane_loop.wav";
            if (style == BlackFlameSoundStyle::Glitch)
                return base / "glitch_loop.wav";
            return base / "flame_loop.wav";
        case BlackFlameSoundEvent::ThinkingStart:
            return base / (style == BlackFlameSoundStyle::Arcane ? "arcane.wav" : "ready.wav");
        case BlackFlameSoundEvent::Ready:
            return base / "ready.wav";
        case BlackFlameSoundEvent::Execute:
            return base / "execute.wav";
        case BlackFlameSoundEvent::Denied:
            return base / "glitch.wav";
        case BlackFlameSoundEvent::HighConfidence:
            return base / "arcane.wav";
        case BlackFlameSoundEvent::SuggestionAppear:
            return base / "suggestion.wav";
        default:
            return {};
        }
    }

    static float SmoothValue(float current, float target, float dt)
    {
        const float factor = std::clamp(dt * 6.0f, 0.0f, 1.0f);
        return current + (target - current) * factor;
    }

    static void NormalizeLoopVolumes(float& baseVolume, float& arcaneVolume, float& glitchVolume, float maxTotal)
    {
        const float total = baseVolume + arcaneVolume + glitchVolume;
        if (total <= maxTotal || total <= 0.0001f)
            return;
        const float scale = maxTotal / total;
        baseVolume *= scale;
        arcaneVolume *= scale;
        glitchVolume *= scale;
    }

    void UpdateLoopLayer(uint32_t key, float targetVolume, float targetPitch, float dt)
    {
        auto voiceIt = m_LoopVoices.find(key);
        if (voiceIt == m_LoopVoices.end() || !voiceIt->second)
            return;

        LoopMixState& mix = m_LoopMixStates[key];
        mix.CurrentVolume = SmoothValue(mix.CurrentVolume, targetVolume, dt);
        mix.CurrentPitch = SmoothValue(mix.CurrentPitch, targetPitch + mix.PitchJitter, dt);
        voiceIt->second->SetVolume(std::clamp(mix.CurrentVolume, 0.0f, 1.0f));
        voiceIt->second->SetFrequencyRatio(std::clamp(mix.CurrentPitch, 0.70f, 1.30f));
    }

    static float GetLoopPitchJitter(uint32_t key)
    {
        const float normalized = ((key * 1103515245u + 12345u) & 0xFFu) / 255.0f;
        return -0.02f + normalized * 0.04f;
    }

    const AudioClip* GetClip(BlackFlameSoundEvent eventType, BlackFlameSoundStyle style)
    {
        const uint32_t key = MakeKey(eventType, style);
        auto it = m_ClipCache.find(key);
        if (it != m_ClipCache.end())
            return it->second.Data.empty() ? nullptr : &it->second;

        AudioClip clip;
        const std::filesystem::path path = ResolveAssetPath(eventType, style);
        if (!path.empty() && LoadWaveFile(path, clip))
        {
            auto [insertedIt, _] = m_ClipCache.emplace(key, std::move(clip));
            return &insertedIt->second;
        }

        Logger::Log(LogLevel::Warning,
            std::string("[BlackFlameAudio] Missing or invalid audio asset: ") + path.string(),
            "BlackFlame");
        m_ClipCache.emplace(key, AudioClip{});
        return nullptr;
    }

    IXAudio2SourceVoice* PlayClip(const AudioClip& clip, bool loop, BlackFlameSoundEvent eventType, BlackFlameSoundStyle style, float intensity)
    {
        if (!m_XAudio || !m_MasteringVoice || clip.Data.empty())
            return nullptr;

        IXAudio2SourceVoice* voice = nullptr;
        const HRESULT hrVoice = m_XAudio->CreateSourceVoice(&voice, &clip.Format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_VoiceCallback);
        if (FAILED(hrVoice) || !voice)
        {
            Logger::Log(LogLevel::Warning, "[BlackFlameAudio] CreateSourceVoice failed.", "BlackFlame");
            return nullptr;
        }

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(clip.Data.size());
        buffer.pAudioData = clip.Data.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        buffer.pContext = loop ? nullptr : voice;

        const BlackFlameAudioParams params = GetParams(eventType, style, intensity, loop);
        voice->SetVolume(params.Volume);
        voice->SetFrequencyRatio(params.Pitch);

        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0)))
        {
            voice->DestroyVoice();
            Logger::Log(LogLevel::Warning, "[BlackFlameAudio] SubmitSourceBuffer/Start failed.", "BlackFlame");
            return nullptr;
        }

        if (loop)
        {
            LoopMixState& mix = m_LoopMixStates[MakeKey(eventType, style)];
            mix.CurrentVolume = params.Volume;
            mix.CurrentPitch = params.Pitch;
            mix.PitchJitter = GetLoopPitchJitter(MakeKey(eventType, style));
        }

        return voice;
    }

    void StopLoopLocked()
    {
        for (auto& [key, voice] : m_LoopVoices)
        {
            if (!voice)
                continue;
            voice->Stop(0);
            voice->FlushSourceBuffers();
            voice->DestroyVoice();
        }
        m_LoopVoices.clear();
        m_LoopMixStates.clear();
    }

    static bool LoadWaveFile(const std::filesystem::path& path, AudioClip& outClip)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;

        auto readU32 = [&](uint32_t& value) -> bool
        {
            file.read(reinterpret_cast<char*>(&value), sizeof(value));
            return (bool)file;
        };
        auto readU16 = [&](uint16_t& value) -> bool
        {
            file.read(reinterpret_cast<char*>(&value), sizeof(value));
            return (bool)file;
        };

        char riff[4]{};
        char wave[4]{};
        uint32_t riffSize = 0;
        file.read(riff, 4);
        if (!file || std::string_view(riff, 4) != "RIFF")
            return false;
        if (!readU32(riffSize))
            return false;
        file.read(wave, 4);
        if (!file || std::string_view(wave, 4) != "WAVE")
            return false;

        bool foundFmt = false;
        bool foundData = false;
        WAVEFORMATEX format{};
        std::vector<uint8_t> data;

        while (file && (!foundFmt || !foundData))
        {
            char chunkId[4]{};
            uint32_t chunkSize = 0;
            file.read(chunkId, 4);
            if (!file)
                break;
            if (!readU32(chunkSize))
                return false;

            const std::string_view id(chunkId, 4);
            if (id == "fmt ")
            {
                if (chunkSize < 16)
                    return false;
                uint16_t formatTag = 0;
                uint16_t channels = 0;
                uint32_t sampleRate = 0;
                uint32_t avgBytesPerSec = 0;
                uint16_t blockAlign = 0;
                uint16_t bitsPerSample = 0;
                if (!readU16(formatTag) || !readU16(channels) || !readU32(sampleRate) || !readU32(avgBytesPerSec) || !readU16(blockAlign) || !readU16(bitsPerSample))
                    return false;

                format.wFormatTag = formatTag;
                format.nChannels = channels;
                format.nSamplesPerSec = sampleRate;
                format.nAvgBytesPerSec = avgBytesPerSec;
                format.nBlockAlign = blockAlign;
                format.wBitsPerSample = bitsPerSample;
                format.cbSize = 0;

                if (chunkSize > 16)
                    file.seekg(chunkSize - 16, std::ios::cur);
                foundFmt = true;
            }
            else if (id == "data")
            {
                data.resize(chunkSize);
                file.read(reinterpret_cast<char*>(data.data()), chunkSize);
                if (!file)
                    return false;
                foundData = true;
            }
            else
            {
                file.seekg(chunkSize, std::ios::cur);
            }

            if (chunkSize & 1u)
                file.seekg(1, std::ios::cur);
        }

        if (!foundFmt || !foundData || data.empty())
            return false;
        if (format.wFormatTag != WAVE_FORMAT_PCM)
            return false;

        outClip.Format = format;
        outClip.Data = std::move(data);
        return true;
    }

    static const char* EventLabel(BlackFlameSoundEvent eventType)
    {
        switch (eventType)
        {
        case BlackFlameSoundEvent::ThinkingStart: return "ThinkingStart";
        case BlackFlameSoundEvent::ThinkingLoop: return "ThinkingLoop";
        case BlackFlameSoundEvent::Ready: return "Ready";
        case BlackFlameSoundEvent::Execute: return "Execute";
        case BlackFlameSoundEvent::Denied: return "Denied";
        case BlackFlameSoundEvent::HighConfidence: return "HighConfidence";
        case BlackFlameSoundEvent::SuggestionAppear: return "SuggestionAppear";
        default: return "Unknown";
        }
    }

    static const char* StyleLabel(BlackFlameSoundStyle style)
    {
        switch (style)
        {
        case BlackFlameSoundStyle::Clean: return "Clean";
        case BlackFlameSoundStyle::Warm: return "Warm";
        case BlackFlameSoundStyle::Arcane: return "Arcane";
        case BlackFlameSoundStyle::Glitch: return "Glitch";
        default: return "Unknown";
        }
    }

    std::mutex m_Mutex;
    std::unordered_map<uint32_t, Clock::time_point> m_LastPlayTimes;
    std::unordered_map<uint32_t, AudioClip> m_ClipCache;
    IXAudio2* m_XAudio = nullptr;
    IXAudio2MasteringVoice* m_MasteringVoice = nullptr;
    std::unordered_map<uint32_t, IXAudio2SourceVoice*> m_LoopVoices;
    std::unordered_map<uint32_t, LoopMixState> m_LoopMixStates;
    VoiceCallback m_VoiceCallback{};
};
