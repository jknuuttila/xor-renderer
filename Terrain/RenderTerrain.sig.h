#ifndef RENDERTERRAIN_SIG_H
#define RENDERTERRAIN_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrain)

XOR_CBUFFER(Constants, 0)
{
    float4x4 viewProj;
	float2 worldMin;
	float2 worldMax;
    float heightMin;
    float heightMax;
};

XOR_CBUFFER(LightingConstants, 1)
{
    float4 sunDirection;
	float4 sunColor;
};

XOR_SRV(Texture2D<float4>, terrainColor,  0)
XOR_SRV(Texture2D<float4>, terrainNormal, 1)
XOR_SRV(Texture2D<float>,  terrainAO,     2)

XOR_SAMPLER_BILINEAR(bilinearSampler)

XOR_END_SIGNATURE

#define RENDERTERRAIN_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(2, 3)

#endif

