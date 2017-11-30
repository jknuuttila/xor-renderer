#ifndef RENDERTERRAINAO_SIG_H
#define RENDERTERRAINAO_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(RenderTerrainAO)

XOR_CBUFFER(Constants, 1)
{
    float4x4 viewProj;
    float2 aoTextureSize;
    uint aoBitMask;
};

XOR_TEXTURE_UAV(RWTexture2D<uint>, terrainAOVisibleBits, 0)
XOR_TEXTURE_SRV(Texture2D<float>, tileLODs, 0)

XOR_SAMPLER_BILINEAR(tileLODSampler)

XOR_END_SIGNATURE

#include "TerrainPatchConstants.h"

#define RENDERTERRAINAO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CSU(2, 1, 1)

#endif
