#include "scene_cb.hlsli"

TextureCube gSkyTexture : register(t1);
SamplerState gLinearSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

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

float4 main(PSInput input) : SV_Target
{
    float4 clipPos = float4(input.ndc, 1.0f, 1.0f);
    float4 worldPos = mul(clipPos, gSceneInvViewProjection);
    worldPos.xyz /= max(worldPos.w, 1e-5f);

    float3 viewDir = normalize(worldPos.xyz - gSceneCameraPosition);
    viewDir = RotateAroundY(viewDir, gSceneShadowParams.x);
    viewDir = RotateAroundX(viewDir, gSceneShadowParams.y);

    float3 color = gSkyTexture.Sample(gLinearSampler, normalize(viewDir)).rgb;
    color *= gSceneReserved0.rgb * gSceneReserved0.a;
    color *= exp2(gSceneShadowParams.w);
    return float4(color, 1.0f);
}
