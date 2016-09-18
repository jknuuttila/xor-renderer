#ifndef BLIT_SIG_H
#define BLIT_SIG_H

#include "Xor/Shaders.h"

XOR_BEGIN_SIGNATURE(BlitShader)


XOR_CBUFFER(Constants, 0)
{
    float4 multiplier;
    float4 bias;
    float2 posBegin;
    float2 posEnd;
    float2 uvBegin;
    float2 uvEnd;
    float mip;
};

XOR_SRV(Texture2D<float4>, src, 0)

XOR_SAMPLER_POINT(pointSampler)


XOR_END_SIGNATURE

#define BLIT_ROOT_SIGNATURE XOR_ROOT_SIGNATURE_CS(1, 1)

#endif
