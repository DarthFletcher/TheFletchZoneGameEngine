// placeholder (file intentionally empty until project is updated to treat shaders as runtime-compiled)

cbuffer CameraCB : register(b0)
{
    float4x4 ViewProj;
};

struct VSIn
{
    float3 pos : POSITION;
    uint col : COLOR0;
};

struct VSOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

static float4 UnpackRGBA(uint c)
{
    float r = ((c >> 0) & 255) / 255.0;
    float g = ((c >> 8) & 255) / 255.0;
    float b = ((c >> 16) & 255) / 255.0;
    float a = ((c >> 24) & 255) / 255.0;
    return float4(r, g, b, a);
}

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0f), ViewProj);
    o.col = UnpackRGBA(i.col);
    return o;
}
