#include "scene_cb.hlsli"

Texture2D gSkyTexture : register(t1);
SamplerState gLinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

static const float TFZ_PI = 3.14159265359f;
static const float TFZ_TWO_PI = 6.28318530718f;

float3 RotateAroundY(float3 dir, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    return float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c);
}

float3 RotateAroundX(float3 dir, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    return float3(
        dir.x,
        dir.y * c - dir.z * s,
        dir.y * s + dir.z * c);
}

float2 DirectionToEquirectUv(float3 dir)
{
    dir = normalize(dir);
    const float u = atan2(dir.x, dir.z) / TFZ_TWO_PI + 0.5f;
    const float v = acos(clamp(dir.y, -1.0f, 1.0f)) / TFZ_PI;
    return float2(u, v);
}

float4 main(PSInput input) : SV_Target
{
    float4 clipPos = float4(input.ndc, 1.0f, 1.0f);
    float4 worldPos = mul(clipPos, gSceneInvViewProjection);
    worldPos.xyz /= max(worldPos.w, 1e-5f);

    float3 viewDir = normalize(worldPos.xyz - gSceneCameraPosition);
    viewDir = RotateAroundY(viewDir, gSceneShadowParams.x);
    viewDir = RotateAroundX(viewDir, gSceneShadowParams.y);

    float2 uv = DirectionToEquirectUv(viewDir);
    uv.y = clamp(uv.y + gSceneShadowParams.z, 0.001f, 0.999f);
    float3 color = gSkyTexture.Sample(gLinearSampler, uv).rgb;
    color *= gSceneReserved0.rgb * gSceneReserved0.a;
    color *= exp2(gSceneShadowParams.w);
    return float4(color, 1.0f);
}
