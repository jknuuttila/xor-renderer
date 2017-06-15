#ifndef RENDERTERRAINAO_SIG_H
#define RENDERTERRAINAO_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrainAO)

XOR_CBUFFER(Constants, 0)
{
    float4x4 viewProj;
	float2 worldMin;
	float2 worldMax;
    float2 aoTextureSize;
    uint aoBitMask;
};

XOR_UAV(RWTexture2D<uint>, terrainAOVisibleBits, 0)

XOR_END_SIGNATURE

#define RENDERTERRAINAO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CU(1, 1)

#endif
