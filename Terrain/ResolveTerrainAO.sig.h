#ifndef RESOLVETERRAINAO_SIG_H
#define RESOLVETERRAINAO_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(ResolveTerrainAO)

XOR_CBUFFER(Constants, 0)
{
    int2 size;
    float maxVisibleSamples;
    int blurKernelSize;
    float4 blurWeights[9];
};

XOR_SRV(Texture2D<uint>,    terrainAOVisibleSamples, 0)
XOR_UAV(RWTexture2D<float>, terrainAO, 0)

XOR_THREADGROUP_SIZE_2D(16, 16)

XOR_END_SIGNATURE

#define RESOLVETERRAINAO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CSU(1, 1, 1)

#endif
