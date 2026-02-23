cbuffer SceneCB : register(b0)
{
    float4x4 gViewProj;
    float3 gCameraPos;
    float  gGridFadeDist;
};

struct InstanceData
{
    float4x4 World;
    float4 Color;
};

StructuredBuffer<InstanceData> gInstances : register(t0);

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

VSOutput main(VSInput input, uint instanceID : SV_InstanceID)
{
    InstanceData inst = gInstances[instanceID];

    float4 worldPos = mul(float4(input.pos, 1.0f), inst.World);
    float4 clipPos = mul(worldPos, gViewProj);

    VSOutput o;
    o.pos = clipPos;
    o.color = inst.Color;
    return o;
}
