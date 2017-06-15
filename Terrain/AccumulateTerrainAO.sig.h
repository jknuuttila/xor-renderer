#ifndef ACCUMULATETERRAINAO_SIG_H
#define ACCUMULATETERRAINAO_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(AccumulateTerrainAO)

XOR_CBUFFER(Constants, 0)
{
    uint2 size;
};

XOR_UAV(RWTexture2D<uint>, terrainAOVisibleSamples, 0)
XOR_UAV(RWTexture2D<uint>, terrainAOVisibleBits,    1)

XOR_THREADGROUP_SIZE_2D(16, 16)

XOR_END_SIGNATURE

#define ACCUMULATETERRAINAO_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CU(1, 2)

#endif
