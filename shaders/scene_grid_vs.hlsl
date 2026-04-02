#include "scene_cb.hlsli"

struct VSInput
{
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 pos      : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // Pass through world position for fade calculations in PS.
    o.worldPos = input.pos;

    // Transform to clip space.
    o.pos = mul(float4(input.pos, 1.0f), gSceneViewProjection);

    o.color = input.color;
    return o;
}
