#ifndef TFZ_SCENE_CB_V2_INCLUDED
#define TFZ_SCENE_CB_V2_INCLUDED

cbuffer SceneCB : register(b0)
{
    float4x4 gSceneView;
    float4x4 gSceneProjection;
    float4x4 gSceneViewProjection;
    float4x4 gSceneInvViewProjection;

    float3 gSceneCameraPosition;
    float  gSceneNearZ;

    float2 gSceneViewportSize;
    float2 gSceneInvViewportSize;

    float  gSceneFarZ;
    float  gSceneFovY;
    float  gSceneAspect;
    float  gSceneTimeSeconds;

    float  gSceneDeltaTime;
    float  gSceneRenderScale;
    uint   gSceneFlags;
    float  gScenePad0;

    float  gSceneGridFadeDistance;
    float  gSceneGridVisibility;
    float  gSceneGridMajorLineBoost;
    float  gSceneGridAxisEmphasis;

    float3 gSceneLightDirection;
    float  gSceneLightIntensity;

    float3 gSceneLightColor;
    float  gSceneAmbientIntensity;

    float4x4 gSceneShadowViewProjection;
    float4  gSceneShadowParams;
    float4  gScenePostProcessParams;
    float4  gSceneDebugParams;

    uint    gSceneInstanceOffset;
    uint3   gScenePadU32;

    float4  gSceneReserved0;
};

#define TFZ_SCENE_FLAG_GRID_FADE_ENABLED   (1u << 0)
#define TFZ_SCENE_FLAG_GRID_MAJOR_ENABLED  (1u << 1)

#define TFZ_DEBUG_VIEW_LIT             0u
#define TFZ_DEBUG_VIEW_ALBEDO          1u
#define TFZ_DEBUG_VIEW_NORMALS         2u
#define TFZ_DEBUG_VIEW_METALLIC        3u
#define TFZ_DEBUG_VIEW_ROUGHNESS       4u
#define TFZ_DEBUG_VIEW_LIGHTING_ONLY   5u
#define TFZ_DEBUG_VIEW_AMBIENT_ONLY    6u
#define TFZ_DEBUG_VIEW_SPECULAR_ONLY   7u
#define TFZ_DEBUG_VIEW_SELECTION_MASK  8u
#define TFZ_DEBUG_VIEW_DEPTH           9u
#define TFZ_DEBUG_VIEW_LINEAR_DEPTH    10u
#define TFZ_DEBUG_VIEW_WORLD_POSITION  11u
#define TFZ_DEBUG_VIEW_UVS             12u
#define TFZ_DEBUG_VIEW_LIGHT_DIRECTION 13u

#endif
