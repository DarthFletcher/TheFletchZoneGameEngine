#pragma once

#include <d3d12.h>
#include <cstring>

#if defined(_DEBUG) && __has_include(<pix3.h>)
#include <pix3.h>
#define TFZ_HAS_PIX 1
#else
#define TFZ_HAS_PIX 0
#endif

inline void BeginGpuEvent(ID3D12GraphicsCommandList* cmd, const char* name)
{
    if (!cmd || !name)
        return;

#if TFZ_HAS_PIX
    PIXBeginEvent(cmd, 0, name);
#else
    // Fallback: use native D3D12 event markers (still visible in PIX captures)
    cmd->BeginEvent(0, name, (UINT)std::strlen(name));
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
