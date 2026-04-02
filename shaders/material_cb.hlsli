#ifndef TFZ_MATERIAL_CB_INCLUDED
#define TFZ_MATERIAL_CB_INCLUDED

cbuffer MaterialCB : register(b1)
{
    float4 gMaterialBaseColor;
    float4 gMaterialScalars;      // x=metallic, y=roughness, z=flipNormalGreen, w=reserved
    float4 gMaterialTextureFlags; // x=albedo, y=normal, z=metallic, w=roughness
    float4 gMaterialReserved[13];
};

#endif

