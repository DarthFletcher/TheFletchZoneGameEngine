struct PSInput
{
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
    float3 worldPos : TEXCOORD0;
};

#include "scene_cb.hlsli"

float4 main(PSInput input) : SV_Target
{
    float dist = length(input.worldPos.xz - gSceneCameraPosition.xz);
    float fade = ((gSceneFlags & TFZ_SCENE_FLAG_GRID_FADE_ENABLED) != 0u) ? saturate(1.0 - dist / max(gSceneGridFadeDistance, 1.0)) : 1.0f;

    float nearestMultipleX = abs(input.worldPos.x - round(input.worldPos.x / 5.0f) * 5.0f);
    float nearestMultipleZ = abs(input.worldPos.z - round(input.worldPos.z / 5.0f) * 5.0f);
    float majorMask = (min(nearestMultipleX, nearestMultipleZ) < 0.05f && ((gSceneFlags & TFZ_SCENE_FLAG_GRID_MAJOR_ENABLED) != 0u)) ? 1.0f : 0.0f;
    float axisMask = (abs(input.color.r - input.color.g) > 0.15f || abs(input.color.b - input.color.g) > 0.15f) ? 1.0f : 0.0f;

    float4 color = input.color;
    color.rgb *= lerp(1.0f, gSceneGridMajorLineBoost, majorMask);
    color.a *= lerp(1.0f, gSceneGridMajorLineBoost, majorMask);
    color.rgb *= lerp(1.0f, gSceneGridAxisEmphasis, axisMask);
    color.a *= lerp(1.0f, gSceneGridAxisEmphasis, axisMask);
    color.a *= gSceneGridVisibility;
    color.a *= fade;
    return color;
}
