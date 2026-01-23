struct PSInput
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD0;
};

cbuffer SceneCB : register(b0)
{
    float4x4 gViewProj;
    float3 gCameraPos;
    float  gGridFadeDist;
};

float4 main(PSInput input) : SV_Target
{
    float dist = length(input.worldPos.xz - gCameraPos.xz);
    float fade = saturate(1.0 - dist / gGridFadeDist);
    float4 color = input.color;
    color.a *= fade;
    return color;
}
