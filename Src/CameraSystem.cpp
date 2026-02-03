#include "CameraSystem.h"

#include "Camera.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <format>

using namespace DirectX;

namespace
{
    static bool g_inited = false;
    static bool g_loggedInit = false;

    static Camera g_active;
    static CameraData g_activeData{};

    static bool ShouldLog(std::chrono::milliseconds interval)
    {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last >= interval)
        {
            last = now;
            return true;
        }
        return false;
    }
}

void CameraSystem::InitializeOnce()
{
    if (g_inited)
        return;

    g_inited = true;

    if (!g_loggedInit)
    {
        g_loggedInit = true;
        Logger::Log(LogLevel::Info, "Phase 3C: CameraSystem initialized", "[Camera]");
    }

    // Reasonable defaults.
    g_active.SetLookAt(XMFLOAT3(0.0f, 3.0f, -5.0f), XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
    g_active.SetPerspective(XMConvertToRadians(60.0f), 1.0f, 0.1f, 100.0f);
    g_activeData = g_active.BuildDataLH();
}

void CameraSystem::Update(uint64_t frameIndex, float /*dt*/, uint32_t viewportW, uint32_t viewportH)
{
    InitializeOnce();

    const float w = (float)(viewportW != 0 ? viewportW : 1u);
    const float h = (float)(viewportH != 0 ? viewportH : 1u);
    const float aspect = w / (std::max)(h, 1.0f);

    // Deterministic validation motion: orbit around origin.
    const float angle = (float)frameIndex * 0.01f;
    const float radius = 5.0f;

    const XMFLOAT3 pos{
        std::cos(angle) * radius,
        3.0f,
        std::sin(angle) * radius
    };

    g_active.SetLookAt(pos, XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
    g_active.SetPerspective(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);

    g_activeData = g_active.BuildDataLH();

    if (ShouldLog(std::chrono::milliseconds(1000)))
    {
        Logger::Log(LogLevel::Debug, std::format("frame={} pos=({:.2f},{:.2f},{:.2f}) aspect={:.3f}",
            frameIndex, pos.x, pos.y, pos.z, aspect),
            "[Camera]");
    }
}

const CameraData& CameraSystem::GetActiveData()
{
    InitializeOnce();
    return g_activeData;
}
