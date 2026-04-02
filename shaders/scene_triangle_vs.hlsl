#include "scene_cb.hlsli"

struct InstanceData
{
    float4x4 World;
    float4 Color;
};

StructuredBuffer<InstanceData> gInstances : register(t0);

struct VSInput
{
    float3 pos    : POSITION;
    float4 color  : COLOR;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct VSOutput
{
    float4 pos       : SV_POSITION;
    float4 color     : COLOR;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    InstanceData inst = gInstances[gSceneInstanceOffset + instanceID];

    float4 worldPos = mul(float4(input.pos, 1.0f), inst.World);
    float4 clipPos = mul(worldPos, gSceneViewProjection);
    float3x3 world3x3 = (float3x3)inst.World;

    VSOutput o;
    o.pos = clipPos;
    o.color = inst.Color;
    o.worldPos = worldPos.xyz;
    o.worldNorm = normalize(mul(input.normal, world3x3));
    o.uv = input.uv;
    return o;
}
