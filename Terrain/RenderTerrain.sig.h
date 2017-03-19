#ifndef RENDERTERRAIN_SIG_H
#define RENDERTERRAIN_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrain)

XOR_CBUFFER(Constants, 0)
{
    float4x4 viewProj;
    float4x4 shadowViewProj;
    float4x4 prevViewProj;
	float2 worldMin;
	float2 worldMax;
    float heightMin;
    float heightMax;
    float2 noiseResolution;
    float  noiseAmplitude;
    float  padding;
    float2 resolution;
    float2 shadowResolution;
    float  shadowHistoryBlend;
    float  shadowBias;
};

XOR_CBUFFER(LightingConstants, 1)
{
    float4 sunDirection;
	float4 sunColor;
    float4 ambient;
};

XOR_SRV(Texture2D<float4>, terrainColor,   0)
XOR_SRV(Texture2D<float4>, terrainNormal,  1)
XOR_SRV(Texture2D<float>,  terrainAO,      2)
XOR_SRV(Texture2D<float>,  terrainShadows, 3)
XOR_SRV(Texture2D<float4>, noiseTexture,   4)
XOR_SRV(Texture2D<float>,  shadowTerm,     5)

XOR_SAMPLER_BILINEAR(bilinearSampler)
XOR_SAMPLER_POINT(pointSampler)
XOR_SAMPLER_POINT_WRAP(pointWrapSampler)
XOR_SAMPLER_PCF_GE(pcfSampler)

XOR_END_SIGNATURE

#define RENDERTERRAIN_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(2, 6)

#endif

