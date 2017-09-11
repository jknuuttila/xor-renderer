#ifndef TerrainShadowFiltering_SIG_H
#define TerrainShadowFiltering_SIG_H

#include "Xor/Shaders.h"

#ifndef TSF_THREADGROUP_SIZE
#define TSF_THREADGROUP_SIZE 8
#endif

XOR_BEGIN_SIGNATURE(TerrainShadowFiltering)

XOR_CBUFFER(Constants, 0)
{
    int2 resolution;
    float shadowHistoryBlend;
};

XOR_UAV(RWTexture2D<float>, shadowOut,     0)
XOR_SRV(Texture2D<float>,   shadowIn,      0)
XOR_SRV(Texture2D<float>,   shadowHistory, 1)
XOR_SRV(Texture2D<float2>,  motionVectors, 2)

XOR_SAMPLER_BILINEAR(bilinearSampler);

XOR_THREADGROUP_SIZE_2D(TSF_THREADGROUP_SIZE, TSF_THREADGROUP_SIZE)

XOR_END_SIGNATURE

#define TERRAINSHADOWFILTERING_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CSU(1, 3, 1)

#endif

