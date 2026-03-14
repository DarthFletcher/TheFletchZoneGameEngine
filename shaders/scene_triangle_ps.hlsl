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
    const bool selected = input.color.a > 1.5f;
    const float4 selectedTint = float4(1.0f, 0.45f, 0.20f, 1.0f);

    if (gUseAlbedoTexture > 0.5f)
    {
        float4 albedo = gAlbedoTexture.Sample(gLinearSampler, input.uv);
        return selected ? lerp(albedo, selectedTint, 0.35f) : albedo;
    }

    return selected ? selectedTint : input.color;
}
