struct PSInput
{
    float4 pos   : SV_Position;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD0;
};

float3 gCameraPos;
float  gGridFadeDist;

float4 main(PSInput input) : SV_Target
{
    float dist = length(input.worldPos.xz - gCameraPos.xz);
    float fade = saturate(1.0 - dist / gGridFadeDist);
    float4 color = input.color;
    color.a *= fade;
    return color;
}
