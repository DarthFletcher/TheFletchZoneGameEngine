cbuffer SceneCB : register(b0)
{
    float4x4 gViewProj;
    float3 gCameraPos;
    float  gGridFadeDist;
};

struct VSInput
{
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    o.pos = mul(float4(input.pos, 1.0f), gViewProj);
    o.color = input.color;
    return o;
}
