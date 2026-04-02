#include "material_cb.hlsli"

Texture2D gAlbedoTexture    : register(t1);
Texture2D gNormalTexture    : register(t2);
Texture2D gMetallicTexture  : register(t3);
Texture2D gRoughnessTexture : register(t4);
SamplerState gLinearSampler : register(s0);

#include "scene_cb.hlsli"

struct PSInput
{
    float4 pos       : SV_POSITION;
    float4 color     : COLOR;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float2 uv        : TEXCOORD2;
};

float3 BuildTangent(float3 n)
{
    const float3 up = abs(n.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    return normalize(cross(up, n));
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float4 main(PSInput input) : SV_Target
{
    const uint debugViewMode = (uint)round(gSceneDebugParams.x);
    const bool selected = input.color.a > 1.5f;
    const float4 selectedTint = float4(1.0f, 0.45f, 0.20f, 1.0f);

    const float metallicScalar = saturate(gMaterialScalars.x);
    const float roughnessScalar = saturate(gMaterialScalars.y);
    const bool flipNormalGreen = gMaterialScalars.z > 0.5f;
    const bool useAlbedoTexture = gMaterialTextureFlags.x > 0.5f;
    const bool useNormalTexture = gMaterialTextureFlags.y > 0.5f;
    const bool useMetallicTexture = gMaterialTextureFlags.z > 0.5f;
    const bool useRoughnessTexture = gMaterialTextureFlags.w > 0.5f;

    float4 baseSample = useAlbedoTexture
        ? (gAlbedoTexture.Sample(gLinearSampler, input.uv) * gMaterialBaseColor)
        : gMaterialBaseColor;
    float3 baseColor = saturate(baseSample.rgb);

    float metallic = metallicScalar;
    if (useMetallicTexture)
        metallic *= saturate(gMetallicTexture.Sample(gLinearSampler, input.uv).r);

    float roughness = roughnessScalar;
    if (useRoughnessTexture)
        roughness *= saturate(gRoughnessTexture.Sample(gLinearSampler, input.uv).r);
    roughness = clamp(roughness, 0.04f, 1.0f);

    float3 worldNormal = normalize(input.worldNorm);
    float3 tangent = BuildTangent(worldNormal);
    float3 bitangent = normalize(cross(worldNormal, tangent));

    float3 normalSample = useNormalTexture
        ? (gNormalTexture.Sample(gLinearSampler, input.uv).xyz * 2.0f - 1.0f)
        : float3(0.0f, 0.0f, 1.0f);
    if (flipNormalGreen)
        normalSample.y = -normalSample.y;
    float3 shadedNormal = normalize(
        tangent * normalSample.x +
        bitangent * normalSample.y +
        worldNormal * normalSample.z);

    float3 lightDir = normalize(-gSceneLightDirection);
    float3 viewDir = normalize(gSceneCameraPosition - input.worldPos);
    float3 halfVec = normalize(lightDir + viewDir);

    float NdotL = saturate(dot(shadedNormal, lightDir));
    float NdotV = saturate(dot(shadedNormal, viewDir));
    float NdotH = saturate(dot(shadedNormal, halfVec));
    float VdotH = saturate(dot(viewDir, halfVec));

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
    float3 fresnel = FresnelSchlick(VdotH, f0);

    float shininess = lerp(192.0f, 10.0f, roughness);
    float specularLobe = pow(max(NdotH, 1e-4f), shininess);
    float specularVisibility = NdotL * lerp(1.6f, 0.55f, roughness);
    float3 specular = fresnel * specularLobe * specularVisibility * gSceneLightColor * gSceneLightIntensity;

    float3 diffuseColor = baseColor * (1.0f - metallic);
    float3 ambient = diffuseColor * gSceneLightColor * (gSceneAmbientIntensity * 1.15f + 0.02f);
    float3 diffuse = diffuseColor * gSceneLightColor * (NdotL * gSceneLightIntensity);

    float grazing = pow(1.0f - NdotV, 2.0f);
    float3 finalColor = ambient + diffuse + specular + fresnel * (0.03f * grazing * (1.0f - roughness));

    const float deviceDepth = saturate(input.pos.z);
    const float viewDepth = max(0.0f, input.pos.w);
    const float linearDepth = saturate((viewDepth - gSceneNearZ) / max(gSceneFarZ - gSceneNearZ, 1e-4f));
    const float3 worldPositionColor = saturate(abs(input.worldPos) / max(gSceneGridFadeDistance, 1.0f));
    const float2 uvWrapped = frac(input.uv);
    const float3 lightDirectionColor = normalize(-gSceneLightDirection) * 0.5f + 0.5f;

    switch (debugViewMode)
    {
    case TFZ_DEBUG_VIEW_ALBEDO:
        return float4(baseColor, 1.0f);
    case TFZ_DEBUG_VIEW_NORMALS:
        return float4(shadedNormal * 0.5f + 0.5f, 1.0f);
    case TFZ_DEBUG_VIEW_METALLIC:
        return float4(metallic.xxx, 1.0f);
    case TFZ_DEBUG_VIEW_ROUGHNESS:
        return float4(roughness.xxx, 1.0f);
    case TFZ_DEBUG_VIEW_LIGHTING_ONLY:
        return float4(saturate(ambient + diffuse + specular), 1.0f);
    case TFZ_DEBUG_VIEW_AMBIENT_ONLY:
        return float4(saturate(ambient), 1.0f);
    case TFZ_DEBUG_VIEW_SPECULAR_ONLY:
        return float4(saturate(specular), 1.0f);
    case TFZ_DEBUG_VIEW_SELECTION_MASK:
        return selected ? float4(1.0f, 0.45f, 0.20f, 1.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
    case TFZ_DEBUG_VIEW_DEPTH:
        return float4(deviceDepth.xxx, 1.0f);
    case TFZ_DEBUG_VIEW_LINEAR_DEPTH:
        return float4(linearDepth.xxx, 1.0f);
    case TFZ_DEBUG_VIEW_WORLD_POSITION:
        return float4(worldPositionColor, 1.0f);
    case TFZ_DEBUG_VIEW_UVS:
        return float4(uvWrapped, 0.0f, 1.0f);
    case TFZ_DEBUG_VIEW_LIGHT_DIRECTION:
        return float4(lightDirectionColor, 1.0f);
    case TFZ_DEBUG_VIEW_LIT:
    default:
        break;
    }

    float4 litColor = float4(saturate(finalColor), baseSample.a);
    return selected ? lerp(litColor, selectedTint, 0.35f) : litColor;
}
