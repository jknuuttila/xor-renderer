#ifndef ComputeNormalMap_SIG_H
#define ComputeNormalMap_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(ComputeNormalMap)

XOR_CBUFFER(Constants, 0)
{
    uint2 size;
    float2 axisMultiplier;
    float heightMultiplier;
};

XOR_TEXTURE_SRV(Texture2D<float>,    heightMap, 0)
XOR_TEXTURE_UAV(RWTexture2D<float4>, normalMap, 0)

XOR_SAMPLER_POINT(pointSampler)

XOR_THREADGROUP_SIZE_2D(16, 16)

XOR_END_SIGNATURE

#define ComputeNormalMap_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CSU(1,1,1)

#endif
