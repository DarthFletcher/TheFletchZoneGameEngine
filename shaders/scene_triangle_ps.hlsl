cbuffer MaterialCB : register(b1)
{
    float gMetallic;
    float gRoughness;
    float gUseAlbedoTexture;
    float gMaterialPad[61];
};

Texture2D gAlbedoTexture : register(t1);
SamplerState gLinearSampler : register(s0);

struct PSInput
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    if (gUseAlbedoTexture > 0.5f)
        return gAlbedoTexture.Sample(gLinearSampler, input.uv);

    return input.color;
}
