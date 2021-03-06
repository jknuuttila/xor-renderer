#ifndef RENDERTERRAIN_SIG_H
#define RENDERTERRAIN_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrain)

XOR_CBUFFER(Constants, 1)
{
    float4x4 viewProj;
    float4x4 shadowViewProj;
    float4x4 prevViewProj;
    float4 cameraPos3D;
    float2 cameraPos2D;
    float2 padding0;
    float4 sunDirection;
	float4 sunColor;
    float4 ambient;
    float2 noiseResolution;
    float  noiseAmplitude;
    float  padding1;
    float2 resolution;
    float2 shadowResolution;
    float  shadowHistoryBlend;
    float  shadowBias;
};

XOR_TEXTURE_SRV(Texture2D<float4>, terrainColor,   0)
XOR_TEXTURE_SRV(Texture2D<float4>, terrainNormal,  1)
XOR_TEXTURE_SRV(Texture2D<float>,  terrainAO,      2)
XOR_TEXTURE_SRV(Texture2D<float>,  terrainShadows, 3)
XOR_TEXTURE_SRV(Texture2D<float4>, noiseTexture,   4)
XOR_TEXTURE_SRV(Texture2D<float>,  shadowTerm,     5)

XOR_SAMPLER_BILINEAR(bilinearSampler)
XOR_SAMPLER_POINT(pointSampler)
XOR_SAMPLER_POINT_WRAP(pointWrapSampler)
XOR_SAMPLER_PCF_GE(pcfSampler)
XOR_SAMPLER_BILINEAR(tileLODSampler)

XOR_END_SIGNATURE

#include "TerrainRendering.h"

#define RENDERTERRAIN_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(2, 6)

#endif

