#pragma once

#include <d3d12.h>

#if defined(_DEBUG) && __has_include(<pix3.h>)
#include <pix3.h>
#define TFZ_HAS_PIX 1
#else
#define TFZ_HAS_PIX 0
#endif

inline void BeginGpuEvent(ID3D12GraphicsCommandList* cmd, const char* name)
{
#if TFZ_HAS_PIX
    if (cmd && name)
        PIXBeginEvent(cmd, 0, name);
#else
    (void)cmd;
    (void)name;
#endif
}

inline void EndGpuEvent(ID3D12GraphicsCommandList* cmd)
{
#if TFZ_HAS_PIX
    if (cmd)
        PIXEndEvent(cmd);
#else
    (void)cmd;
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
