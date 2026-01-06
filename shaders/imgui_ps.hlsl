cbuffer vertexBuffer : register(b0)
{
    float4x4 ProjectionMatrix;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

// Toggle to validate that ImGui draw calls are reaching the backbuffer.
// When set to 1, the shader ignores textures/vertex colors and outputs a solid color.
#ifndef IMGUI_PS_DEBUG_SOLID
#define IMGUI_PS_DEBUG_SOLID 0
#endif

float4 main(PSInput input) : SV_Target
{
#if IMGUI_PS_DEBUG_SOLID
    // Solid magenta with full alpha (highly visible)
    return float4(1.0, 0.0, 1.0, 1.0);
#else
    float4 out_col = texture0.Sample(sampler0, input.uv) * input.col;
    return out_col;
#endif
}
