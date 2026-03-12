#pragma once

#include <d3d12.h>
#include <cstring>
#include <string>
#include "Logger.h"

#if defined(_DEBUG) && __has_include(<pix3.h>)
#include <pix3.h>
#if defined(_MSC_VER)
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif
#define TFZ_HAS_PIX 1
#else
#define TFZ_HAS_PIX 0
#endif

namespace tfz::detail
{
    inline std::wstring ToWideAscii(const char* s)
    {
        if (!s)
            return {};

        const size_t n = std::strlen(s);
        std::wstring w;
        w.reserve(n);
        for (size_t i = 0; i < n; ++i)
            w.push_back(static_cast<unsigned char>(s[i]));
        return w;
    }
}

inline void BeginGpuEvent(ID3D12GraphicsCommandList* cmd, const char* name)
{
    if (!cmd || !name)
        return;

#if TFZ_HAS_PIX
    PIXBeginEvent(cmd, 0, name);
#else
    // Fallback: legacy D3D12 event markers (PIX will show a deprecation notice, but names remain readable).
    const std::wstring w = tfz::detail::ToWideAscii(name);
    cmd->BeginEvent(0, w.c_str(), (UINT)(w.size() * sizeof(wchar_t)));
#endif
}

inline void EndGpuEvent(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd)
        return;

#if TFZ_HAS_PIX
    PIXEndEvent(cmd);
#else
    cmd->EndEvent();
#endif
}

class ScopedGpuEvent
{
public:
    ScopedGpuEvent(ID3D12GraphicsCommandList* cmd, const char* name) : m_cmd(cmd)
    {
        BeginGpuEvent(m_cmd, name);
    }

    ~ScopedGpuEvent()
    {
        EndGpuEvent(m_cmd);
    }

    ScopedGpuEvent(const ScopedGpuEvent&) = delete;
    ScopedGpuEvent& operator=(const ScopedGpuEvent&) = delete;

private:
    ID3D12GraphicsCommandList* m_cmd{};
};

class ScopedRenderPass
{
public:
    ScopedRenderPass(ID3D12GraphicsCommandList* cmd, const char* name)
        : m_cmd(cmd), m_name(name ? name : "RenderPass")
    {
        Logger::LogPassBegin(m_name);
        BeginGpuEvent(m_cmd, m_name.c_str());
    }

    ~ScopedRenderPass()
    {
        EndGpuEvent(m_cmd);
        Logger::LogPassEnd(m_name);
    }

    ScopedRenderPass(const ScopedRenderPass&) = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;

private:
    ID3D12GraphicsCommandList* m_cmd{};
    std::string m_name;
};
