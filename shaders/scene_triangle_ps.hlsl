cbuffer MaterialCB : register(b1)
{
    float4 gBaseColor;
    float gMetallic;
    float gRoughness;
    float gUseAlbedoTexture;
    float gUseMetallicTexture;
    float gUseRoughnessTexture;
    float gFlipNormalGreen;
    float gMaterialPad[54];
};

Texture2D gAlbedoTexture    : register(t1);
Texture2D gNormalTexture    : register(t2);
Texture2D gMetallicTexture  : register(t3);
Texture2D gRoughnessTexture : register(t4);
SamplerState gLinearSampler : register(s0);

cbuffer SceneCB : register(b0)
{
    float4x4 gViewProj;
    float3 gCameraPos;
    float  gGridFadeDist;
    float3 gLightDirection;
    float  gLightIntensity;
    float3 gLightColor;
    float  gAmbientIntensity;
};

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
    const bool selected = input.color.a > 1.5f;
    const float4 selectedTint = float4(1.0f, 0.45f, 0.20f, 1.0f);

    float4 baseSample = (gUseAlbedoTexture > 0.5f)
        ? (gAlbedoTexture.Sample(gLinearSampler, input.uv) * gBaseColor)
        : gBaseColor;
    float3 baseColor = saturate(baseSample.rgb);

    float metallic = saturate(gMetallic);
    if (gUseMetallicTexture > 0.5f)
        metallic *= saturate(gMetallicTexture.Sample(gLinearSampler, input.uv).r);

    float roughness = saturate(gRoughness);
    if (gUseRoughnessTexture > 0.5f)
        roughness *= saturate(gRoughnessTexture.Sample(gLinearSampler, input.uv).r);
    roughness = clamp(roughness, 0.04f, 1.0f);

    float3 worldNormal = normalize(input.worldNorm);
    float3 tangent = BuildTangent(worldNormal);
    float3 bitangent = normalize(cross(worldNormal, tangent));

    float3 normalSample = gNormalTexture.Sample(gLinearSampler, input.uv).xyz * 2.0f - 1.0f;
    if (gFlipNormalGreen > 0.5f)
        normalSample.y = -normalSample.y;
    float3 shadedNormal = normalize(
        tangent * normalSample.x +
        bitangent * normalSample.y +
        worldNormal * normalSample.z);

    float3 lightDir = normalize(-gLightDirection);
    float3 viewDir = normalize(gCameraPos - input.worldPos);
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
    float3 specular = fresnel * specularLobe * specularVisibility * gLightColor * gLightIntensity;

    float3 diffuseColor = baseColor * (1.0f - metallic);
    float3 ambient = diffuseColor * gLightColor * (gAmbientIntensity * 1.15f + 0.02f);
    float3 diffuse = diffuseColor * gLightColor * (NdotL * gLightIntensity);

    float grazing = pow(1.0f - NdotV, 2.0f);
    float3 finalColor = ambient + diffuse + specular + fresnel * (0.03f * grazing * (1.0f - roughness));

    float4 litColor = float4(saturate(finalColor), baseSample.a);
    return selected ? lerp(litColor, selectedTint, 0.35f) : litColor;
}
