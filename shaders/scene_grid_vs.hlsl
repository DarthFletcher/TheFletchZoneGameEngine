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
    float3 worldPos : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;
    // Snap grid to camera XZ position
    float3 snapped = input.pos;
    snapped.xz += (gCameraPos.xz - fmod(gCameraPos.xz, 1.0));
    o.worldPos = snapped;
    o.pos = mul(float4(snapped, 1.0f), gViewProj);
    o.color = input.color;
    return o;
}
